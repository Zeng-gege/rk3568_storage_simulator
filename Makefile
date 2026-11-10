CC = aarch64-linux-gnu-gcc
CFLAGS = -Wall -O2 -Iinclude
TARGET = scsi_sim

SRCS = src/main.c src/uart.c src/nand_sim.c src/scsi_cdb.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) -static -lpthread

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET) storage_disk.img

.PHONY: all clean
