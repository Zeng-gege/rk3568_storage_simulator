#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "uart.h"
#include "nand_sim.h"
#include "scsi_cdb.h"

static int g_uart_fd = -1;

void handle_sigint(int sig) {
    if (g_uart_fd >= 0) {
        uart_close(g_uart_fd);
    }
    nand_sim_deinit();
    exit(0);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    signal(SIGINT, handle_sigint);

    if (nand_sim_init() < 0) {
        return -1;
    }

    scsi_init();

    const char *uart_device = "/dev/ttyS4";
    int baudrate = 115200;

    if (argc > 1) {
        uart_device = argv[1];
    }
    if (argc > 2) {
        baudrate = atoi(argv[2]);
    }

    g_uart_fd = uart_open(uart_device, baudrate);
    if (g_uart_fd < 0) {
        nand_sim_deinit();
        return -1;
    }

    unsigned char rx_byte;
    while (1) {
        int n = uart_recv(g_uart_fd, &rx_byte, 1, 100);
        if (n > 0) {
            scsi_process_byte(g_uart_fd, rx_byte);
        } else if (n < 0) {
            usleep(100000);
        }
    }

    uart_close(g_uart_fd);
    nand_sim_deinit();
    return 0;
}
