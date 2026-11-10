#include "nand_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

FtlNandContext g_ftl_nand;
static uint32_t current_active_block = 0;
static uint32_t current_page_offset = 0;

static int run_garbage_collection(void) {
    int victim_block = -1;
    int min_valid_pages = PAGES_PER_BLOCK + 1;

    for (uint32_t i = 0; i < TOTAL_BLOCKS; i++) {
        if (g_ftl_nand.block_states[i] == BLOCK_STATE_USED && i != current_active_block) {
            int valid_count = 0;
            for (uint32_t p = 0; p < PAGES_PER_BLOCK; p++) {
                uint32_t pba = i * PAGES_PER_BLOCK + p;
                for (uint32_t lba = 0; lba < TOTAL_SECTORS; lba++) {
                    if (g_ftl_nand.lba_to_pba[lba].physical_page == pba) {
                        valid_count++;
                        break;
                    }
                }
            }
            if (valid_count < min_valid_pages) {
                min_valid_pages = valid_count;
                victim_block = (int)i;
            }
        }
    }

    if (victim_block == -1) {
        return -1;
    }

    printf("[FTL-GC] Victim block selected: %u (Valid pages: %d)\n", victim_block, min_valid_pages);

    uint8_t *page_buf = malloc(PAGE_SIZE);
    if (!page_buf) return -1;

    for (uint32_t p = 0; p < PAGES_PER_BLOCK; p++) {
        uint32_t old_pba = (uint32_t)victim_block * PAGES_PER_BLOCK + p;
        int found_lba = -1;

        for (uint32_t lba = 0; lba < TOTAL_SECTORS; lba++) {
            if (g_ftl_nand.lba_to_pba[lba].physical_page == old_pba) {
                found_lba = (int)lba;
                break;
            }
        }

        if (found_lba != -1) {
            if (current_page_offset >= PAGES_PER_BLOCK) {
                free(page_buf);
                return -1;
            }

            ssize_t ret = pread(g_ftl_nand.fd, page_buf, PAGE_SIZE, (off_t)old_pba * PAGE_SIZE);
            (void)ret;

            uint32_t new_pba = current_active_block * PAGES_PER_BLOCK + current_page_offset;
            current_page_offset++;

            ret = pwrite(g_ftl_nand.fd, page_buf, PAGE_SIZE, (off_t)new_pba * PAGE_SIZE);
            (void)ret;

            g_ftl_nand.lba_to_pba[found_lba].physical_page = new_pba;
            g_ftl_nand.lba_to_pba[found_lba].seq_num++;
        }
    }
    free(page_buf);

    uint8_t *blank_block = malloc(PAGES_PER_BLOCK * PAGE_SIZE);
    if (blank_block) {
        memset(blank_block, 0xFF, PAGES_PER_BLOCK * PAGE_SIZE);
        ssize_t ret = pwrite(g_ftl_nand.fd, blank_block, PAGES_PER_BLOCK * PAGE_SIZE, (off_t)victim_block * PAGES_PER_BLOCK * PAGE_SIZE);
        (void)ret;
        free(blank_block);
    }

    g_ftl_nand.block_erase_counts[victim_block]++;
    g_ftl_nand.block_states[victim_block] = BLOCK_STATE_FREE;

    current_active_block = (uint32_t)victim_block;
    g_ftl_nand.block_states[current_active_block] = BLOCK_STATE_USED;
    current_page_offset = 0;

    printf("[FTL-GC] Garbage collection completed. Block %u erased and recycled.\n", victim_block);
    return 0;
}
static int allocate_new_free_block(void) {
    for (uint32_t i = 0; i < TOTAL_BLOCKS; i++) {
        if (g_ftl_nand.block_states[i] == BLOCK_STATE_FREE) {
            g_ftl_nand.block_states[i] = BLOCK_STATE_USED;
            current_active_block = i;
            current_page_offset = 0;
            printf("[FTL] Block allocated: %u (Erase count: %u)\n", i, g_ftl_nand.block_erase_counts[i]);
            return 0;
        }
    }
    return run_garbage_collection();
}

int nand_sim_init(void) {
    ssize_t ret;
    pthread_mutex_init(&g_ftl_nand.mutex, NULL);

    g_ftl_nand.inject_write_fail = 0;
    g_ftl_nand.inject_bit_flip = 0;

    g_ftl_nand.fd = open(MEDIA_FILE_PATH, O_RDWR | O_CREAT, 0644);
    if (g_ftl_nand.fd < 0) return -1;

    off_t file_size = lseek(g_ftl_nand.fd, 0, SEEK_END);
    size_t target_size = TOTAL_PAGES * PAGE_SIZE;

    if (file_size < (off_t)target_size) {
        if (ftruncate(g_ftl_nand.fd, target_size) < 0) return -1;
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
        g_ftl_nand.lba_to_pba[i].physical_page = 0xFFFFFFFF; 
        g_ftl_nand.lba_to_pba[i].seq_num = 0;
    }

    for (uint32_t i = 0; i < TOTAL_BLOCKS; i++) {
        g_ftl_nand.block_erase_counts[i] = 0;
        g_ftl_nand.block_states[i] = BLOCK_STATE_FREE;
    }

    allocate_new_free_block();

    return 0;
}

void nand_sim_deinit(void) {
    pthread_mutex_lock(&g_ftl_nand.mutex);
    if (g_ftl_nand.fd >= 0) close(g_ftl_nand.fd);
    pthread_mutex_unlock(&g_ftl_nand.mutex);
    pthread_mutex_destroy(&g_ftl_nand.mutex);
}

int nand_sim_lba_read(uint32_t lba, uint8_t *buf, uint32_t sectors) {
    ssize_t ret;
    if (lba + sectors > TOTAL_SECTORS) return -1;

    pthread_mutex_lock(&g_ftl_nand.mutex);

    for (uint32_t i = 0; i < sectors; i++) {
        uint32_t curr_lba = lba + i;
        uint32_t pba_page = g_ftl_nand.lba_to_pba[curr_lba].physical_page;

        if (pba_page == 0xFFFFFFFF) {
            memset(buf + (i * PAGE_SIZE), 0x00, PAGE_SIZE);
        } else {
            off_t offset = (off_t)pba_page * PAGE_SIZE;
            ret = pread(g_ftl_nand.fd, buf + (i * PAGE_SIZE), PAGE_SIZE, offset);
            (void)ret;
        }
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

        if (current_page_offset >= PAGES_PER_BLOCK) {
            if (allocate_new_free_block() < 0) {
                pthread_mutex_unlock(&g_ftl_nand.mutex);
                return -1;
            }
        }

        uint32_t new_pba_page = (current_active_block * PAGES_PER_BLOCK) + current_page_offset;
        current_page_offset++;

        off_t offset = (off_t)new_pba_page * PAGE_SIZE;
        uint8_t write_buf[PAGE_SIZE];
        memcpy(write_buf, buf + (i * PAGE_SIZE), PAGE_SIZE);

        if (g_ftl_nand.inject_bit_flip) {
            write_buf[0] ^= 0x01;
        }

        ret = pwrite(g_ftl_nand.fd, write_buf, PAGE_SIZE, offset);
        (void)ret;

        g_ftl_nand.lba_to_pba[curr_lba].physical_page = new_pba_page;
        g_ftl_nand.lba_to_pba[curr_lba].seq_num++;
    }

    pthread_mutex_unlock(&g_ftl_nand.mutex);
    return 0;
}
