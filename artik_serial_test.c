#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <pthread.h>
#include <artik_module.h>
#include <artik_platform.h>
/*
 * This is a loopback test. On Artik 5 development board,
 * connect a wire between "TX" and "RX" pins
 * on connector J26.
 */
static artik_serial_config config = {
        ARTIK_A5_SCOM_XSCOM4,
        "UART3",
        ARTIK_SERIAL_BAUD_115200,
        ARTIK_SERIAL_PARITY_NONE,
        ARTIK_SERIAL_DATA_8BIT,
        ARTIK_SERIAL_STOP_1BIT,
        ARTIK_SERIAL_FLOWCTRL_NONE,
        NULL
};
#define MAX_RX_BUF  64
artik_error test_serial_loopback(int platid)
{
    artik_serial_module* serial = (artik_serial_module*)artik_get_api_module("serial");
    artik_serial_handle handle;
    artik_error ret = S_OK;
    char tx_buf[] = "This is a test buffer containing test data";
    int tx_len = strlen(tx_buf)+1;
    char rx_buf[MAX_RX_BUF];
    int read_bytes = 0;
    int len = tx_len;
    if(platid == ARTIK5) {
        config.port_num = ARTIK_A5_SCOM_XSCOM4;
        config.name = "UART3";
    } else {
        config.port_num = ARTIK_A10_SCOM_XSCOM2;
        config.name = "UART1";
    }
    fprintf(stdout, "TEST: %s\n", __func__);
    ret = serial->request(&handle, &config);
    if (ret != S_OK) {
        fprintf(stderr, "TEST: %s failed to request serial port (%d)\n", __func__, ret);
        return ret;
    }
    /* Send test data */
    ret = serial->write(handle, tx_buf, &tx_len);
    if (ret != S_OK) {
        fprintf(stderr, "TEST: %s failed to send data (%d)\n", __func__, ret);
        goto exit;
    }
    /* Loop until we read all the data we expect */
    do {
        /* Wait for read data to become available */
        ret = serial->wait_for_data(handle);
        if (ret != S_OK) {
            fprintf(stderr, "TEST: %s failed while waiting for RX data (%d)\n", __func__, ret);
            goto exit;
        }
        /* Read data */
        ret = serial->read(handle, &rx_buf[read_bytes], &len);
        if (ret == E_TRY_AGAIN) {
            continue;
        }
        if(ret != S_OK) {
            fprintf(stderr, "TEST: %s failed to read data (%d)\n", __func__, ret);
            goto exit;
        }
        read_bytes += len;
        len = tx_len - read_bytes;
    } while (len > 0);
    /* Compare with sent data */
    if (strncmp(tx_buf, rx_buf, MAX_RX_BUF)) {
        fprintf(stderr, "TEST: %s failed (%d). Tx and Rx data don't match (%s != %s)\n", __func__, ret, tx_buf, rx_buf);
        goto exit;
    }
    fprintf(stdout, "TEST: %s succeeded\n", __func__);
exit:
    serial->release(handle);
    return ret;
}
static void* rx_thread_func(void* param)
{
    artik_serial_module* serial = (artik_serial_module*)artik_get_api_module("serial");
    artik_serial_handle handle = (artik_serial_handle)param;
    artik_error ret = S_OK;
    /* Wait until we get interrupted by a call to "cancel_wait" */
    ret = serial->wait_for_data(handle);
    if (ret == E_INTERRUPTED)
        fprintf(stdout, "TEST: %s Rx blocking wait was interrupted\n", __func__);
    else
        fprintf(stderr, "TEST: %s failed while waiting for RX data (%d)\n", __func__, ret);
    return NULL;
}
void wait_for_space_key(void)
{
    int c;
    static struct termios oldt, newt;
    tcgetattr( STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON);
    tcsetattr( STDIN_FILENO, TCSANOW, &newt);
    while((c=getchar()) != ' ')
        putchar(c);
    tcsetattr( STDIN_FILENO, TCSANOW, &oldt);
}
artik_error test_serial_cancel(int platid)
{
    artik_serial_module* serial = (artik_serial_module*)artik_get_api_module("serial");
    artik_serial_handle handle;
    artik_error ret = S_OK;
    pthread_t rx_thread;
    if(platid == ARTIK5) {
        config.port_num = ARTIK_A5_SCOM_XSCOM4;
        config.name = "UART3";
    } else {
        config.port_num = ARTIK_A10_SCOM_XSCOM2;
        config.name = "UART1";
    }
    fprintf(stdout, "TEST: %s\n", __func__);
    ret = serial->request(&handle, &config);
    if (ret != S_OK) {
        fprintf(stderr, "TEST: %s failed to request serial port (%d)\n", __func__, ret);
        return ret;
    }
    /* Spawn a thread waiting on incoming data */
    pthread_create(&rx_thread, NULL, rx_thread_func, (void*)handle);
    fprintf(stdout, "TEST: %s - press space to cancel the blocking function\n", __func__);
    wait_for_space_key();
    ret = serial->cancel_wait(handle);
    if (ret != S_OK) {
        fprintf(stderr, "TEST: %s failed to cancel blocking call (%d)\n", __func__, ret);
        return ret;
    }
    pthread_join(rx_thread, NULL);
    fprintf(stdout, "TEST: %s succeeded\n", __func__);
exit:
    serial->release(handle);
    return ret;
}
int main(void)
{
    artik_error ret = S_OK;
    int platid = artik_get_platform();
    if (!artik_is_module_available(ARTIK_MODULE_SERIAL)) {
        fprintf(stdout, "TEST: Serial module is not available, skipping test...\n");
        return -1;
    }
    if((platid == ARTIK5) || (platid == ARTIK10)) {
        ret = test_serial_loopback(platid);
        ret = test_serial_cancel(platid);
    }
    return (ret == S_OK) ? 0 : -1;
}