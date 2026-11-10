#ifndef UART_H
#define UART_H

int uart_open(const char *device, int baudrate);
void uart_close(int fd);
int uart_send(int fd, const unsigned char *buf, int len);
int uart_recv(int fd, unsigned char *buf, int len, int timeout_ms);

#endif
