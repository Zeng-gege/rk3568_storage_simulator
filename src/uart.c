#include "uart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/types.h>

static speed_t get_baudrate_macro(int baudrate) {
    switch (baudrate) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 921600: return B921600;
        case 1500000: return B1500000;
        default:     return B115200;
    }
}

int uart_open(const char *device, int baudrate) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        return -1;
    }

    fcntl(fd, F_SETFL, 0);

    struct termios options;
    tcgetattr(fd, &options);

    speed_t speed = get_baudrate_macro(baudrate);
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag &= ~CRTSCTS;

    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_iflag &= ~(ICRNL | INLCR | IGNCR);
    options.c_oflag &= ~OPOST;

    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

void uart_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

int uart_send(int fd, const unsigned char *buf, int len) {
    if (fd < 0 || (buf == NULL && len > 0)) return -1; 
    int written = 0;
    while (written < len) {
        int n = write(fd, buf + written, len - written);
        if (n <= 0) {
            break;
        }
        written += n;
    }
    return written;
}

int uart_recv(int fd, unsigned char *buf, int len, int timeout_ms) {
    if (fd < 0 || len <= 0) return -1;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd + 1, &read_fds, NULL, NULL, &timeout);
    if (ret < 0) {
        return -1;
    } else if (ret == 0) {
        return 0;
    }

    if (FD_ISSET(fd, &read_fds)) {
        int n = read(fd, buf, len);
        return n;
    }

    return 0;
}
