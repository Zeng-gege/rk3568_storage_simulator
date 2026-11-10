#include "scsi_cdb.h"
#include "nand_sim.h"
#include "uart.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

ScsiContext g_scsi_ctx;

void scsi_init(void) {
    memset(&g_scsi_ctx, 0, sizeof(ScsiContext));
    g_scsi_ctx.state = STATE_IDLE;
    g_scsi_ctx.cdb_len = 0;
    g_scsi_ctx.cdb_expected_len = 6;
}

static int get_cdb_expected_length(uint8_t opcode) {
    switch (opcode) {
        case 0x00:
        case 0x12:
        case 0x03:
            return 6;
        case 0x28:
        case 0x2A:
        case 0x25:
            return 10;
        default:
            return 6;
    }
}

static void execute_scsi_command(int uart_fd) {
    uint8_t opcode = g_scsi_ctx.cdb[0];

    switch (opcode) {
        case 0x00: {
            unsigned char resp[] = { 0x00 };
            uart_send(uart_fd, resp, 1);
            break;
        }
        case 0x12: {
            unsigned char inquiry_data[36] = {0};
            inquiry_data[0] = 0x00;
            inquiry_data[1] = 0x80;
            inquiry_data[2] = 0x06;
            inquiry_data[3] = 0x02;
            inquiry_data[4] = 31;
            memcpy(&inquiry_data[8],  "ALIENTEK", 8);
            memcpy(&inquiry_data[16], "RK3568 SCSI SIM", 15);
            memcpy(&inquiry_data[32], "V1.0", 4);

            uart_send(uart_fd, inquiry_data, 36);
            unsigned char status = 0x00;
            uart_send(uart_fd, &status, 1);
            break;
        }
        case 0x25: {
            unsigned char cap_data[8];
            uint32_t last_lba = TOTAL_SECTORS - 1;
            cap_data[0] = (last_lba >> 24) & 0xFF;
            cap_data[1] = (last_lba >> 16) & 0xFF;
            cap_data[2] = (last_lba >> 8) & 0xFF;
            cap_data[3] = last_lba & 0xFF;
            cap_data[4] = 0x00;
            cap_data[5] = 0x00;
            cap_data[6] = 0x02;
            cap_data[7] = 0x00;

            uart_send(uart_fd, cap_data, 8);
            unsigned char status = 0x00;
            uart_send(uart_fd, &status, 1);
            break;
        }
        case 0x28: {
            g_scsi_ctx.lba = ((uint32_t)g_scsi_ctx.cdb[2] << 24) |
                             ((uint32_t)g_scsi_ctx.cdb[3] << 16) |
                             ((uint32_t)g_scsi_ctx.cdb[4] << 8)  |
                             (uint32_t)g_scsi_ctx.cdb[5];
            g_scsi_ctx.sectors = ((uint32_t)g_scsi_ctx.cdb[7] << 8) |
                                  (uint32_t)g_scsi_ctx.cdb[8];

            uint32_t transfer_bytes = g_scsi_ctx.sectors * PAGE_SIZE;
            if (transfer_bytes > sizeof(g_scsi_ctx.data_buffer)) {
                unsigned char status = 0x02;
                uart_send(uart_fd, &status, 1);
                break;
            }

            if (nand_sim_lba_read(g_scsi_ctx.lba, g_scsi_ctx.data_buffer, g_scsi_ctx.sectors) < 0) {
                unsigned char status = 0x02;
                uart_send(uart_fd, &status, 1);
                break;
            }

            uart_send(uart_fd, g_scsi_ctx.data_buffer, transfer_bytes);
            unsigned char status = 0x00;
            uart_send(uart_fd, &status, 1);
            break;
        }
        case 0x2A: {
            g_scsi_ctx.lba = ((uint32_t)g_scsi_ctx.cdb[2] << 24) |
                             ((uint32_t)g_scsi_ctx.cdb[3] << 16) |
                             ((uint32_t)g_scsi_ctx.cdb[4] << 8)  |
                             (uint32_t)g_scsi_ctx.cdb[5];
            g_scsi_ctx.sectors = ((uint32_t)g_scsi_ctx.cdb[7] << 8) |
                                  (uint32_t)g_scsi_ctx.cdb[8];
            g_scsi_ctx.data_expected_len = g_scsi_ctx.sectors * PAGE_SIZE;
            g_scsi_ctx.data_offset = 0;

            if (g_scsi_ctx.data_expected_len > sizeof(g_scsi_ctx.data_buffer)) {
                unsigned char status = 0x02;
                uart_send(uart_fd, &status, 1);
                g_scsi_ctx.state = STATE_IDLE;
                break;
            }

            g_scsi_ctx.state = STATE_DATA_OUT;
            return;
        }
        default: {
            unsigned char status = 0x02;
            uart_send(uart_fd, &status, 1);
            break;
        }
    }

    g_scsi_ctx.state = STATE_IDLE;
    g_scsi_ctx.cdb_len = 0;
}

void scsi_process_byte(int uart_fd, uint8_t byte) {
    switch (g_scsi_ctx.state) {
        case STATE_IDLE:
            g_scsi_ctx.cdb[0] = byte;
            g_scsi_ctx.cdb_len = 1;
            g_scsi_ctx.cdb_expected_len = get_cdb_expected_length(byte);
            g_scsi_ctx.state = STATE_RECV_CDB;
            break;

        case STATE_RECV_CDB:
            g_scsi_ctx.cdb[g_scsi_ctx.cdb_len++] = byte;
            if (g_scsi_ctx.cdb_len >= g_scsi_ctx.cdb_expected_len) {
                execute_scsi_command(uart_fd);
            }
            break;

        case STATE_DATA_OUT:
            g_scsi_ctx.data_buffer[g_scsi_ctx.data_offset++] = byte;
            if (g_scsi_ctx.data_offset >= g_scsi_ctx.data_expected_len) {
                int ret = nand_sim_lba_write(g_scsi_ctx.lba, g_scsi_ctx.data_buffer, g_scsi_ctx.sectors);
                unsigned char status = (ret == 0) ? 0x00 : 0x02;
                uart_send(uart_fd, &status, 1);

                g_scsi_ctx.state = STATE_IDLE;
                g_scsi_ctx.cdb_len = 0;
            }
            break;
    }
}
