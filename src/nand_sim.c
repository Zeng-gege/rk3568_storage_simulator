#include "nand_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

FtlNandContext g_ftl_nand;

int nand_sim_init(void) {
    ssize_t ret;
    pthread_mutex_init(&g_ftl_nand.mutex, NULL); 
    g_ftl_nand.inject_write_fail = 0;
    g_ftl_nand.inject_bit_flip = 0;

    g_ftl_nand.fd = open(MEDIA_FILE_PATH, O_RDWR | O_CREAT, 0644);
    if (g_ftl_nand.fd < 0) {
        return -1;
    }

    off_t file_size = lseek(g_ftl_nand.fd, 0, SEEK_END);
    size_t target_size = TOTAL_PAGES * PAGE_SIZE;
    if (file_size < (off_t)target_size) {
        if (ftruncate(g_ftl_nand.fd, target_size) < 0) {
            close(g_ftl_nand.fd);
            return -1;
        }
        uint8_t *blank_page = malloc(PAGE_SIZE);
        if (blank_page) {
            memset(blank_page, 0xFF, PAGE_SIZE);
            for (size_t i = 0; i < TOTAL_PAGES; i++) {
                ret = pwrite(g_ftl_nand.fd, blank_page, PAGE_SIZE, i * PAGE_SIZE);
                (void)ret;
            }
            free(blank_page);
        }
    }

    for (uint32_t i = 0; i < TOTAL_SECTORS; i++) {
        g_ftl_nand.lba_to_pba[i].physical_page = i;
        g_ftl_nand.lba_to_pba[i].seq_num = 0;
    }

    for (uint32_t i = 0; i < TOTAL_BLOCKS; i++) {
        g_ftl_nand.block_erase_counts[i] = 0;
        g_ftl_nand.block_states[i] = BLOCK_STATE_USED;
    }

    return 0;
}

void nand_sim_deinit(void) {
    pthread_mutex_lock(&g_ftl_nand.mutex);
    if (g_ftl_nand.fd >= 0) {
        close(g_ftl_nand.fd);
        g_ftl_nand.fd = -1;
    }
    pthread_mutex_unlock(&g_ftl_nand.mutex);
    pthread_mutex_destroy(&g_ftl_nand.mutex);
}

int nand_erase_block(uint32_t block_num) {
    ssize_t ret;
    if (block_num >= TOTAL_BLOCKS) return -1;

    pthread_mutex_lock(&g_ftl_nand.mutex);

    if (g_ftl_nand.block_states[block_num] == BLOCK_STATE_BAD) {
        pthread_mutex_unlock(&g_ftl_nand.mutex);
        return -1;
    }

    uint8_t *blank_block = malloc(PAGES_PER_BLOCK * PAGE_SIZE);
    if (blank_block) {
        memset(blank_block, 0xFF, PAGES_PER_BLOCK * PAGE_SIZE);
        off_t offset = (off_t)block_num * PAGES_PER_BLOCK * PAGE_SIZE;
        ret = pwrite(g_ftl_nand.fd, blank_block, PAGES_PER_BLOCK * PAGE_SIZE, offset);
        (void)ret;
        free(blank_block);
    }

    g_ftl_nand.block_erase_counts[block_num]++;
    g_ftl_nand.block_states[block_num] = BLOCK_STATE_FREE;

    pthread_mutex_unlock(&g_ftl_nand.mutex);
    return 0;
}

int nand_sim_lba_read(uint32_t lba, uint8_t *buf, uint32_t sectors) {
    ssize_t ret;
    if (lba + sectors > TOTAL_SECTORS) return -1;

    pthread_mutex_lock(&g_ftl_nand.mutex);

    for (uint32_t i = 0; i < sectors; i++) {
        uint32_t curr_lba = lba + i;
        uint32_t pba_page = g_ftl_nand.lba_to_pba[curr_lba].physical_page;
        off_t offset = (off_t)pba_page * PAGE_SIZE;
        ret = pread(g_ftl_nand.fd, buf + (i * PAGE_SIZE), PAGE_SIZE, offset);
        (void)ret;
    }

    pthread_mutex_unlock(&g_ftl_nand.mutex);
    return 0;
}

int nand_sim_lba_write(uint32_t lba, const uint8_t *buf, uint32_t sectors) {
    ssize_t ret;
    if (lba + sectors > TOTAL_SECTORS) return -1;

    pthread_mutex_lock(&g_ftl_nand.mutex);

    if (g_ftl_nand.inject_write_fail) {
        pthread_mutex_unlock(&g_ftl_nand.mutex);
        return -1;
    }

    for (uint32_t i = 0; i < sectors; i++) {
        uint32_t curr_lba = lba + i;
        uint32_t target_page = g_ftl_nand.lba_to_pba[curr_lba].physical_page;
        if (target_page >= TOTAL_PAGES) {
            target_page = curr_lba; 
        }

        off_t offset = (off_t)target_page * PAGE_SIZE;
        uint8_t write_buf[PAGE_SIZE];
        memcpy(write_buf, buf + (i * PAGE_SIZE), PAGE_SIZE);
        if (g_ftl_nand.inject_bit_flip) {
            write_buf[0] ^= 0x01;
        }

        ret = pwrite(g_ftl_nand.fd, write_buf, PAGE_SIZE, offset);
        (void)ret;
        g_ftl_nand.lba_to_pba[curr_lba].seq_num++;
    }

    pthread_mutex_unlock(&g_ftl_nand.mutex);
    return 0;
}
