#ifndef SCSI_CDB_H
#define SCSI_CDB_H

#include <stdint.h>

typedef enum {
	STATE_IDLE,
	STATE_RECV_CDB,
	STATE_DATA_OUT
} ScsiState;


typedef struct {
	ScsiState state;
	uint8_t cdb[16];
	int cdb_len;
	int cdb_expected_len;

	uint32_t lba;
	uint32_t sectors;
	int data_offset;
	int data_expected_len;
	uint8_t data_buffer[4096];
} ScsiContext;

extern ScsiContext g_scsi_ctx;

void scsi_init(void);
void scsi_process_byte(int uart_fd, uint8_t byte);

#endif

