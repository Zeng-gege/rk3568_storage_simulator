#ifndef NAND_SIM_H
#define NAND_SIM_H

#include <stdint.h>
#include <pthread.h>

#define PAGE_SIZE 	512
#define PAGES_PER_BLOCK 32
#define TOTAL_BLOCKS 	128
#define TOTAL_PAGES 	(TOTAL_BLOCKS * PAGES_PER_BLOCK)
#define TOTAL_SECTORS 	TOTAL_PAGES
#define MEDIA_FILE_PATH "/userdata/storage_disk.img"

#define BLOCK_STATE_FREE 0
#define BLOCK_STATE_USED 1
#define BLOCK_STATE_BAD 2

typedef struct {
	uint32_t physical_page;
	uint32_t seq_num;
} LbaMapEntry;

typedef struct {
	int fd;
	pthread_mutex_t mutex;
	LbaMapEntry lba_to_pba[TOTAL_SECTORS];
	uint32_t block_erase_counts[TOTAL_BLOCKS];
	uint8_t block_states[TOTAL_BLOCKS];
	int inject_write_fail;
	int inject_bit_flip;
} FtlNandContext;

extern FtlNandContext g_ftl_nand;

int nand_sim_init(void);
void nand_sim_deinit(void);
int nand_sim_lba_read(uint32_t lba, uint8_t *buf, uint32_t sectors);
int nand_sim_lba_write(uint32_t lba, const uint8_t *buf, uint32_t sectors);
int nand_erase_block(uint32_t block_num);

#endif
