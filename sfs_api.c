#include <stdlib.h>
#include <string.h>
#include "sfs_api.h"
#include "disk_emu.h"

// https://stackoverflow.com/questions/2745074/fast-ceiling-of-an-integer-division-in-c-c
// This is used for when ceiling division is needed
#define CEIL(x, y) ((x + y - 1) / y)

#define DISK_NAME "sfs_disk_miguel.disk"
#define NUM_OF_DATA_BLOCKS (1024 * 16)
#define NUM_OF_INODES NUM_OF_DATA_BLOCKS // At most one inode is needed for each possible file
#define NUM_OF_INODE_BLOCKS CEIL(NUM_OF_INODES * sizeof(inode_t), BLOCK_SIZE)
#define INODE_BLOCKS_OFFSET 1
#define DATA_BLOCKS_OFFSET (INODE_BLOCKS_OFFSET + NUM_OF_INODE_BLOCKS)
#define FREE_BITMAP_OFFSET (DATA_BLOCKS_OFFSET + NUM_OF_DATA_BLOCKS)
#define NUM_OF_FREE_BITMAP_BLOCKS (NUM_OF_DATA_BLOCKS / BLOCK_SIZE)
// Number of blocks needed to store -> super block + inode table + data blocks + free bitmap
#define TOTAL_NUM_OF_BLOCKS (FREE_BITMAP_OFFSET + NUM_OF_FREE_BITMAP_BLOCKS)
#define MAX_FILE_NAME_SIZE 16
#define MAX_DATA_BLOCKS_FOR_FILE (NUM_OF_DATA_PTRS + INDIRECT_LIST_SIZE) // 12 direct pointers + the amount of indirect pointers possible
#define MAX_NUM_OF_DIR_ENTRIES CEIL(MAX_DATA_BLOCKS_FOR_FILE * BLOCK_SIZE, sizeof(directory_entry_t))
#define FREE_BLOCK_MAP_ARR_SIZE (NUM_OF_DATA_BLOCKS / sizeof(uint64_t))

super_block_t super_block;
uint64_t free_block_map[FREE_BLOCK_MAP_ARR_SIZE];
inode_t inode_table[NUM_OF_INODES];
directory_entry_t root_dir[MAX_NUM_OF_DIR_ENTRIES];
file_descriptor_entry_t file_desc_table[NUM_OF_INODES];

/**
 * Initialise the super_block.
 */
void super_block_init() {
    super_block.magic = 0xACBD0005;
    super_block.block_size = BLOCK_SIZE;
    super_block.file_sys_size = TOTAL_NUM_OF_BLOCKS;
    super_block.inode_table_length = NUM_OF_INODES;
    super_block.root_dir = 0;
}

/**
 * Reads the information collected from the inode metadata into the given pointer.
 * @param inode The inode to read from.
 * @param ptr The pointer to read into.
 */
void read_into_ptr(const inode_t inode, const void *ptr) {
    for (int i = 0; i < NUM_OF_DATA_PTRS && i < inode.size; ++i) {
        // Read each data block one by one into the pointer
        read_blocks(DATA_BLOCKS_OFFSET + inode.data_ptrs[i], 1,
                    ((uint8_t *) ptr) + (i * BLOCK_SIZE)); // Use uint8_t instead of void for pointer arithmetic
    }
    if (inode.size > NUM_OF_DATA_PTRS) {
        const uint32_t num_of_ptrs = inode.size - NUM_OF_DATA_PTRS;
        uint32_t ptrs[INDIRECT_LIST_SIZE];
        // Getting the indirect pointers
        read_blocks(DATA_BLOCKS_OFFSET + inode.indirect, 1, ptrs);
        // Read each data block one by one into the pointer
        for (int i = 0; i < num_of_ptrs; ++i) {
            read_blocks(DATA_BLOCKS_OFFSET + ptrs[i], 1,
                        ((uint8_t *) ptr) + ((i + NUM_OF_DATA_PTRS) * BLOCK_SIZE));
        }
    }
}

void mksfs(int fresh) {
    // Initialise file descriptor table to be empty
    for (int i = 0; i < NUM_OF_INODES; ++i) {
        file_desc_table[i].inode_num = NUM_OF_INODES; // Initialise an invalid number
        file_desc_table[i].read_write_ptr = 0;
    }

    if (fresh) {
        super_block_init();
        init_fresh_disk(DISK_NAME, BLOCK_SIZE, TOTAL_NUM_OF_BLOCKS);
        // Write the super block to the disk
        write_blocks(0, INODE_BLOCKS_OFFSET, &super_block);
    } else {
        init_disk(DISK_NAME, BLOCK_SIZE, TOTAL_NUM_OF_BLOCKS);
        // Read super block into memory
        read_blocks(0, INODE_BLOCKS_OFFSET, &super_block);
        // Read inode table into memory
        read_blocks(INODE_BLOCKS_OFFSET, NUM_OF_INODE_BLOCKS, inode_table);
        // Read root directory into memory
        read_into_ptr(inode_table[super_block.root_dir], root_dir);
        // Read free block map into memory
        read_blocks(FREE_BITMAP_OFFSET, NUM_OF_FREE_BITMAP_BLOCKS, free_block_map);
    }
}

int sfs_getnextfilename(char *file_name) {
    return -1;
}

int sfs_getfilesize(const char *path) {
    return -1;
}

int sfs_fopen(char *file_name) {
    return -1;
}

int sfs_fclose(int fileID) {
    return -1;
}

int sfs_fwrite(int fileID, const char *buf, int length) {
    return -1;
}

int sfs_fread(int fileID, char *buf, int length) {
    return -1;
}

int sfs_fseek(int fileID, int location) {
    return -1;
}

int sfs_remove(char *file_name) {
    return -1;
}

