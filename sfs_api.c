#include <string.h>
#include <stdlib.h>
#include "sfs_api.h"
#include "disk_emu.h"

// https://stackoverflow.com/questions/2745074/fast-ceiling-of-an-integer-division-in-c-c
// This is used for when ceiling division is needed
#define CEIL(x, y) ((x + y - 1) / y)

#define DISK_NAME "sfs_disk_miguel.disk"
#define NUM_OF_DATA_BLOCKS (1024 * 16)
#define NUM_OF_INODES NUM_OF_DATA_BLOCKS // At most one inode is needed for each possible file
#define NUM_OF_INODE_BLOCKS (CEIL(NUM_OF_INODES, BLOCK_SIZE) * sizeof(inode_t))
#define INODE_BLOCKS_OFFSET 1
#define DATA_BLOCKS_OFFSET (INODE_BLOCKS_OFFSET + NUM_OF_INODE_BLOCKS)
#define FREE_BITMAP_OFFSET (DATA_BLOCKS_OFFSET + NUM_OF_DATA_BLOCKS)
#define NUM_OF_FREE_BITMAP_BLOCKS (NUM_OF_DATA_BLOCKS / BLOCK_SIZE)
// Number of blocks needed to store -> super block + inode table + data blocks + free bitmap
#define TOTAL_NUM_OF_BLOCKS (FREE_BITMAP_OFFSET + NUM_OF_FREE_BITMAP_BLOCKS)
#define MAX_DATA_BLOCKS_FOR_FILE (NUM_OF_DATA_PTRS + INDIRECT_LIST_SIZE) // 12 direct pointers + the amount of indirect pointers possible
#define MAX_NUM_OF_DIR_ENTRIES (NUM_OF_INODES - 1)
#define FREE_BLOCK_MAP_ARR_SIZE (NUM_OF_DATA_BLOCKS / (sizeof(int) * 8))

super_block_t super_block;
int free_block_map[FREE_BLOCK_MAP_ARR_SIZE];
inode_t inode_table[NUM_OF_INODES];
directory_entry_t root_dir[MAX_NUM_OF_DIR_ENTRIES];
file_descriptor_entry_t file_desc_table[NUM_OF_INODES];

uint32_t current_file_index;

bool allocate_data_blocks_for_inode(uint32_t final_size, inode_t *inode);

/**
 * Initialise the super block.
 */
void super_block_init() {
    super_block.magic = 0xACBD0005;
    super_block.block_size = BLOCK_SIZE;
    super_block.file_sys_size = TOTAL_NUM_OF_BLOCKS;
    super_block.inode_table_length = NUM_OF_INODES;
    super_block.root_dir = 0;
}

/**
 * Initialise the inode table.
 */
void inode_table_init() {
    for (int i = 0; i < NUM_OF_INODES; ++i) {
        inode_table[i].mode = 0;     // Not sure
        inode_table[i].link_cnt = 0; // Not sure
        inode_table[i].uid = 0;      // Not sure
        inode_table[i].gid = 0;      // Not sure
        inode_table[i].size = 0;
        inode_table[i].indirect = NUM_OF_DATA_BLOCKS;  // Initialise an invalid number
        for (int j = 0; j < NUM_OF_DATA_PTRS; ++j) {
            inode_table[i].data_ptrs[j] = NUM_OF_DATA_BLOCKS;  // Initialise an invalid number
        }
    }
}

/**
 * Initialise the root directory.
 */
void root_dir_init() {
    for (int i = 0; i < MAX_NUM_OF_DIR_ENTRIES; ++i) {
        root_dir[i].inode_num = 0;  // Initialise an invalid number
    }
}

/**
 * Initialise the free block map.
 */
void free_block_map_init() {
    const int free = ~((int) 0);  // Set all bits to 1
    for (int i = 0; i < FREE_BLOCK_MAP_ARR_SIZE; ++i) {
        free_block_map[i] = free;
    }
}

/**
 * Initialise the file descriptor table.
 */
void file_desc_table_init() {
    for (int i = 0; i < NUM_OF_INODES; ++i) {
        file_desc_table[i].inode_num = NUM_OF_INODES; // Initialise an invalid number
        file_desc_table[i].read_write_ptr = 0;
    }
}

/**
 * Reads the information collected from the inode metadata into the given pointer.
 * @param inode The inode to read from.
 * @param ptr The pointer to read into.
 */
void read_into_ptr(const inode_t inode, const void *ptr) {
    const int blocks_used = CEIL(inode.size, BLOCK_SIZE);
    for (int i = 0; i < NUM_OF_DATA_PTRS && i < blocks_used; ++i) {
        // Read each data block one by one into the pointer
        read_blocks(DATA_BLOCKS_OFFSET + inode.data_ptrs[i], 1,
                    ((uint8_t *) ptr) + (i * BLOCK_SIZE)); // Use uint8_t instead of void for pointer arithmetic
    }
    if (blocks_used > NUM_OF_DATA_PTRS) {
        const uint32_t num_of_ptrs = blocks_used - NUM_OF_DATA_PTRS;
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

/**
 * Writes the information in the given pointer into the blocks that the inode points to.
 * @param inode The inode to write into.
 * @param ptr The pointer to write from.
 */
void write_from_ptr(const inode_t inode, const void *ptr) {
    const int blocks_used = CEIL(inode.size, BLOCK_SIZE);
    for (int i = 0; i < NUM_OF_DATA_PTRS && i < blocks_used; ++i) {
        // Read each data block one by one into the pointer
        write_blocks(DATA_BLOCKS_OFFSET + inode.data_ptrs[i], 1,
                     ((uint8_t *) ptr) + (i * BLOCK_SIZE)); // Use uint8_t instead of void for pointer arithmetic
    }
    if (blocks_used > NUM_OF_DATA_PTRS) {
        const uint32_t num_of_ptrs = blocks_used - NUM_OF_DATA_PTRS;
        uint32_t ptrs[INDIRECT_LIST_SIZE];
        // Getting the indirect pointers
        write_blocks(DATA_BLOCKS_OFFSET + inode.indirect, 1, ptrs);
        // Read each data block one by one into the pointer
        for (int i = 0; i < num_of_ptrs; ++i) {
            write_blocks(DATA_BLOCKS_OFFSET + ptrs[i], 1,
                         ((uint8_t *) ptr) + ((i + NUM_OF_DATA_PTRS) * BLOCK_SIZE));
        }
    }
}

/**
 * Algorithm to make sure that all elements in the root directory are contiguous.
 * @param left The starting index to scan from.
 */
void move_invalid_entries_to_back(uint32_t left) {
    uint32_t right = MAX_NUM_OF_DIR_ENTRIES - 1;
    while (left < right) {
        while (left < right && root_dir[right].inode_num == 0) {
            right--;
        }
        if (root_dir[left].inode_num == 0) {
            // swap the two elements
            const directory_entry_t temp = root_dir[left];
            root_dir[left] = root_dir[right];
            root_dir[right] = temp;
        }
        left++;
    }
}

void mksfs(int fresh) {
    current_file_index = 0;
    file_desc_table_init();

    if (fresh) {
        init_fresh_disk(DISK_NAME, BLOCK_SIZE, TOTAL_NUM_OF_BLOCKS);

        super_block_init();
        // Write the super block to the disk
        write_blocks(0, INODE_BLOCKS_OFFSET, &super_block);

        inode_table_init();
        // Write the inode table to the disk
        write_blocks(INODE_BLOCKS_OFFSET, NUM_OF_INODE_BLOCKS, inode_table);

        root_dir_init();
        write_from_ptr(inode_table[super_block.root_dir], root_dir);

        free_block_map_init();
        // Write the free block map to the disk
        write_blocks(FREE_BITMAP_OFFSET, NUM_OF_FREE_BITMAP_BLOCKS, free_block_map);
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

/**
 * Get the next file name.
 * I chose to reset the current file index to 0 if we reach the end of the directory.
 * This was done to allow looping.
 * @param file_name The buffer to copy the file name into
 * @return 1 if successful, 0 otherwise.
 */
int sfs_getnextfilename(char *file_name) {
    if (current_file_index >= MAX_NUM_OF_DIR_ENTRIES || root_dir[current_file_index].inode_num == 0) {
        current_file_index = 0;
        return 0;
    }

    strncpy(file_name, root_dir[current_file_index].file_name, MAX_FILE_NAME_SIZE);
    current_file_index++;

    return 1;
}

/**
 * Get the file size of a given file.
 * @param file_name The file to get the size of.
 * @return The file size in bytes of the given file if the given file exists. Otherwise it returns -1.
 */
int sfs_getfilesize(const char *file_name) {
    for (int i = 0; i < MAX_NUM_OF_DIR_ENTRIES; ++i) {
        const directory_entry_t dir_entry = root_dir[i];
        if (dir_entry.inode_num != 0 && strcmp(file_name, dir_entry.file_name) == 0) {
            return (int) inode_table[dir_entry.inode_num].size;
        }
    }

    return -1;
}

/**
 * Find the inode number for a given file name.
 * @param file_name The file name to check find.
 * @param idx A pointer to be populated by a directory index.
 * @return The inode number if successful and populate idx with the root directory index number. Return MAX_NUM_OF_DIR_ENTRIES if unsuccessful.
 * If the file does not exist and the directory is not full populate idx with the next free index in the root directory.
 * If the file does not exist and the directory is full populate idx with MAX_NUM_OF_DIR_ENTRIES to signal failure.
 */
uint32_t find_inode_num(const char *const file_name, uint32_t *const idx) {
    uint32_t i;
    for (i = 0; i < MAX_NUM_OF_DIR_ENTRIES && root_dir[i].inode_num != 0; ++i) {
        const directory_entry_t dir_entry = root_dir[i];
        if (strcmp(dir_entry.file_name, file_name) == 0) {
            *idx = i;
            return dir_entry.inode_num;
        }
    }

    *idx = i < MAX_NUM_OF_DIR_ENTRIES ? i : MAX_NUM_OF_DIR_ENTRIES;
    return MAX_NUM_OF_DIR_ENTRIES;
}

/**
 * Get the next file descriptor index and populate the given entry.
 * @param inode_num The inode number to populate with.
 * @param read_write_ptr The read and write pointer to populate with.
 * @return The index of the new file descriptor entry if successful. -1 if unsuccessful.
 */
int get_next_file_desc_idx(uint32_t inode_num, uint32_t read_write_ptr) {
    int i;
    for (i = 0; i < NUM_OF_INODES; ++i) {
        if (file_desc_table[i].inode_num >= NUM_OF_INODES) {
            file_desc_table[i].inode_num = inode_num;
            file_desc_table[i].read_write_ptr = read_write_ptr;
            return i;
        }
    }

    return -1;
}

/**
 * Get the lowest inode number that isn't being used.
 * This function uses quite a lot of memory, since I did not feel like implementing a hashset.
 * @return The lowest inode number that isn't being used if successful.
 * Returns MAX_NUM_OF_DIR_ENTRIES if unsuccessful.
 */
uint32_t get_lowest_inode_num() {
    bool is_taken[MAX_NUM_OF_DIR_ENTRIES + 1];
    uint32_t i;
    for (i = 1; i < MAX_NUM_OF_DIR_ENTRIES; ++i) {
        is_taken[i] = false;
    }

    for (i = 0; i < MAX_NUM_OF_DIR_ENTRIES && root_dir[i].inode_num != 0; ++i) {
        is_taken[root_dir[i].inode_num] = true;
    }

    for (i = 1; i < MAX_NUM_OF_DIR_ENTRIES; ++i) {
        if (!is_taken[i]) {
            return i;
        }
    }

    return MAX_NUM_OF_DIR_ENTRIES;
}

bool is_open(const char *const name) {
    for (int i = 0; i < NUM_OF_INODES; ++i) {
        if (file_desc_table[i].inode_num < NUM_OF_INODES) {
            for (int j = 0; j < MAX_NUM_OF_DIR_ENTRIES && root_dir[j].inode_num != 0; ++j) {
                if (file_desc_table[i].inode_num == root_dir[j].inode_num
                    && strcmp(root_dir[j].file_name, name) == 0) {
                    return true;
                }
            }
        }
    }

    return false;
}

int sfs_fopen(char *file_name) {
    if (is_open(file_name)) {
        return -1;
    }

    uint32_t next_free_idx;
    uint32_t inode_num = find_inode_num(file_name, &next_free_idx);

    if (inode_num >= MAX_NUM_OF_DIR_ENTRIES) {
        if (next_free_idx < MAX_NUM_OF_DIR_ENTRIES) {
            inode_num = get_lowest_inode_num();
            if (inode_num >= MAX_NUM_OF_DIR_ENTRIES || strlen(file_name) > MAX_FILE_NAME_SIZE) {
                return -1;
            }

            root_dir[next_free_idx].inode_num = inode_num;
            strncpy(root_dir[next_free_idx].file_name, file_name, MAX_FILE_NAME_SIZE);
            // Set inode size to 0
            inode_table[inode_num].size = 0;

            allocate_data_blocks_for_inode(inode_table[super_block.root_dir].size + sizeof(directory_entry_t),
                                           &inode_table[super_block.root_dir]);
            write_from_ptr(inode_table[super_block.root_dir], root_dir);
            // This could be changed to be more efficient, instead of writing all the inodes
            write_blocks(INODE_BLOCKS_OFFSET, NUM_OF_INODE_BLOCKS, inode_table);
        } else {
            return -1;
        }
    }

    const int result = get_next_file_desc_idx(inode_num, inode_table[inode_num].size);
    return result;
}

int sfs_fclose(int fileID) {
    if (0 > fileID || fileID >= NUM_OF_INODES || file_desc_table[fileID].inode_num >= NUM_OF_INODES) {
        return -1;
    }

    file_desc_table[fileID].inode_num = NUM_OF_INODES;
    file_desc_table[fileID].read_write_ptr = 0;
    return 0;
}

/**
 * Set a given bit from the free bitmap.
 * @param bit bit to set.
 */
void set_bit(uint32_t bit) {
    const uint32_t size_in_bits = sizeof(int) * 8;
    const uint32_t arr_idx = bit / size_in_bits;
    const uint32_t bit_idx = bit % size_in_bits;
    // Set the bit
    free_block_map[arr_idx] |= (((int) 1) << bit_idx);
}

/**
 * Allocate a data block. Returns the data block number allocated if successful.
 * Returns NUM_OF_DATA_BLOCKS if unsuccessful, when the disk is fully allocated.
 */
uint32_t allocate_data_block() {
    const int limit = sizeof(int) * 8;
    for (int i = 0; i < FREE_BLOCK_MAP_ARR_SIZE; ++i) {
        for (int j = 0; j < limit; ++j) {
            // Check the bit
            if ((free_block_map[i] >> j) & ((int) 1)) {
                // Clear the bit
                free_block_map[i] &= ~(((int) 1) << j);
                return limit * i + j;
            }
        }
    }
    return NUM_OF_DATA_BLOCKS;
}

/**
 * Allocate data blocks for an inode as needed.
 * @param final_size The desired size of the file after allocating the data blocks.
 * @param inode The inode to allocate data blocks for.
 * @return True if successful, false if unsuccessful.
 */
bool allocate_data_blocks_for_inode(uint32_t final_size, inode_t *const inode) {
    if (final_size > inode->size) {
        // Number of blocks to allocate
        const uint32_t blocks_used = CEIL(inode->size, BLOCK_SIZE);
        const uint32_t final_blocks_used = CEIL(final_size, BLOCK_SIZE);
        // If that many blocks cannot be allocated return false
        if (final_blocks_used > MAX_DATA_BLOCKS_FOR_FILE) {
            return false;
        }
        uint32_t i;
        // Allocate disk blocks
        for (i = blocks_used; i < final_blocks_used && i < NUM_OF_DATA_PTRS; ++i) {
            const uint32_t data_block_num = allocate_data_block();
            if (data_block_num >= NUM_OF_DATA_BLOCKS) {
                return false;
            }
            inode->data_ptrs[i] = data_block_num;
        }
        if (final_blocks_used > NUM_OF_DATA_PTRS) {
            const uint32_t start = blocks_used > NUM_OF_DATA_PTRS ? blocks_used - NUM_OF_DATA_PTRS : 0;
            const uint32_t limit = final_blocks_used - NUM_OF_DATA_PTRS;
            uint32_t ptrs[INDIRECT_LIST_SIZE];
            if (start > 0) {
                // Getting the indirect pointers
                read_blocks(DATA_BLOCKS_OFFSET + inode->indirect, 1, ptrs);
            } else {
                // Allocate a data block for the indirect pointers
                const uint32_t data_block_num = allocate_data_block();
                if (data_block_num >= NUM_OF_DATA_BLOCKS) {
                    return false;
                }
                inode->indirect = data_block_num;
            }
            // Update the indirect pointer list
            for (i = start; i < limit && i < INDIRECT_LIST_SIZE; ++i) {
                const uint32_t data_block_num = allocate_data_block();
                if (data_block_num >= NUM_OF_DATA_BLOCKS) {
                    return false;
                }
                ptrs[i] = data_block_num;
            }
            // Write the new indirect pinter list to the disk
            write_blocks(DATA_BLOCKS_OFFSET + inode->indirect, 1, ptrs);
        }
        // Update the size of the inode
        inode->size = final_size;
        // Write inode to disk
        write_blocks(INODE_BLOCKS_OFFSET, NUM_OF_INODE_BLOCKS, inode_table);
        // Write free bitmap to disk
        write_blocks(FREE_BITMAP_OFFSET, NUM_OF_FREE_BITMAP_BLOCKS, free_block_map);
    }
    return true;
}

/**
 * Note: When the length of bytes to be written is impossible to write,
 * i.e. when it would cause the file to grow larger than the maximum size for a file,
 * I chose to not write any bytes and not allocate any extra data blocks to the file,
 * returning 0 as the amount of bytes written.
 */
int sfs_fwrite(int fileID, char *buf, int length) {
    if (0 > fileID || fileID >= NUM_OF_INODES) {
        return 0;
    }

    const file_descriptor_entry_t fde = file_desc_table[fileID];
    if (fde.inode_num >= NUM_OF_INODES) {
        return 0;
    }

    if (!allocate_data_blocks_for_inode(fde.read_write_ptr + length, &inode_table[fde.inode_num])) {
        return 0;
    }

    // Need to have an offset
    const int blocks_used = CEIL(inode_table[fde.inode_num].size, BLOCK_SIZE);
    const uint32_t start_block = fde.read_write_ptr / BLOCK_SIZE;
    uint32_t offset = fde.read_write_ptr % BLOCK_SIZE;
    uint32_t result = 0;
    char *const temp_buf = malloc(sizeof(char) * BLOCK_SIZE);
    for (uint32_t i = start_block;
         i < NUM_OF_DATA_PTRS && i < blocks_used && result < length; ++i) {

        read_blocks(DATA_BLOCKS_OFFSET + inode_table[fde.inode_num].data_ptrs[i], 1, temp_buf);
        const uint32_t diff = length - result;
        // Example: offset is 900, but want to write 800 bytes
        // diff = 800
        // bytes_written = (should equal 1024 - 900) 124
        // next time the offset will be 0 and the diff will be (900 - 124)
        const uint32_t bytes_written = diff + offset >= BLOCK_SIZE ? BLOCK_SIZE - offset : diff;

        memcpy(temp_buf + offset, buf + result, bytes_written);
        write_blocks(DATA_BLOCKS_OFFSET + inode_table[fde.inode_num].data_ptrs[i], 1, temp_buf);

        result += bytes_written;
        offset = 0;
    }
    if (blocks_used > NUM_OF_DATA_PTRS && result < length) {
        const uint32_t num_of_ptrs = blocks_used - NUM_OF_DATA_PTRS;
        uint32_t ptrs[INDIRECT_LIST_SIZE];
        // Getting the indirect pointers
        read_blocks(DATA_BLOCKS_OFFSET + inode_table[fde.inode_num].indirect, 1, ptrs);
        for (uint32_t i = start_block > NUM_OF_DATA_PTRS ? start_block - NUM_OF_DATA_PTRS : 0;
             i < num_of_ptrs && result < length; ++i) {

            read_blocks(DATA_BLOCKS_OFFSET + ptrs[i], 1, temp_buf);
            const uint32_t diff = length - result;
            const uint32_t bytes_written = diff + offset >= BLOCK_SIZE ? BLOCK_SIZE - offset : diff;

            memcpy(temp_buf + offset, buf + result, bytes_written);
            write_blocks(DATA_BLOCKS_OFFSET + ptrs[i], 1, temp_buf);

            result += bytes_written;
            offset = 0;
        }
    }

    file_desc_table[fileID].read_write_ptr += result;
    return (int) result;
}

int sfs_fread(int fileID, char *buf, int length) {
    if (0 > fileID || fileID >= NUM_OF_INODES) {
        return 0;
    }

    const file_descriptor_entry_t fde = file_desc_table[fileID];
    if (fde.inode_num >= NUM_OF_INODES) {
        return 0;
    }
    const inode_t inode = inode_table[fde.inode_num];

    const int blocks_used = CEIL(inode.size, BLOCK_SIZE);
    const uint32_t start_block = fde.read_write_ptr / BLOCK_SIZE;
    // This will be set to 0 once it's not the first read
    // The idea is that if the pointer is in the middle of a block,
    // we should offset the first read, and only read into the buffer bytes after the pointer
    uint32_t offset = fde.read_write_ptr % BLOCK_SIZE;
    uint32_t result = 0;
    char *const temp_buf = malloc(sizeof(char) * BLOCK_SIZE);
    for (uint32_t i = start_block;
         i < NUM_OF_DATA_PTRS && i < blocks_used && result < length; ++i) {
        // Read each data block one by one
        read_blocks(DATA_BLOCKS_OFFSET + inode.data_ptrs[i], 1, temp_buf);

        const uint32_t diff = length - result;
        const uint32_t bytes_read = diff + offset >= BLOCK_SIZE ? BLOCK_SIZE - offset : diff;
        memcpy(buf + result, temp_buf + offset, bytes_read);
        result += bytes_read;
        offset = 0;
    }
    if (blocks_used > NUM_OF_DATA_PTRS && result < length) {
        const uint32_t num_of_ptrs = blocks_used - NUM_OF_DATA_PTRS;
        uint32_t ptrs[INDIRECT_LIST_SIZE];
        // Getting the indirect pointers
        read_blocks(DATA_BLOCKS_OFFSET + inode.indirect, 1, ptrs);
        // Read each data block one by one
        for (uint32_t i = start_block > NUM_OF_DATA_PTRS ? start_block - NUM_OF_DATA_PTRS : 0;
             i < num_of_ptrs && result < length; ++i) {
            read_blocks(DATA_BLOCKS_OFFSET + ptrs[i], 1, temp_buf);

            const uint32_t diff = length - result;
            const uint32_t bytes_read = diff + offset >= BLOCK_SIZE ? BLOCK_SIZE - offset : diff;
            memcpy(buf + result, temp_buf + offset, bytes_read);
            result += bytes_read;
            offset = 0;
        }
    }

    file_desc_table[fileID].read_write_ptr += result;
    return (int) result;
}

int sfs_fseek(int fileID, int location) {
    if (0 > fileID || fileID >= NUM_OF_INODES || file_desc_table[fileID].inode_num >= NUM_OF_INODES) {
        return -1;
    }

    file_desc_table[fileID].read_write_ptr = location;
    return 0;
}

/**
 * Release the data blocks held by the given inode.
 * @param inode The inode for which the data blocks must be released.
 */
void release_data_blocks(const inode_t inode) {
    const int blocks_used = CEIL(inode.size, BLOCK_SIZE);
    for (int i = 0; i < NUM_OF_DATA_PTRS && i < blocks_used; ++i) {
        set_bit(inode.data_ptrs[i]);
    }
    if (blocks_used > NUM_OF_DATA_PTRS) {
        const uint32_t num_of_ptrs = blocks_used - NUM_OF_DATA_PTRS;
        uint32_t ptrs[INDIRECT_LIST_SIZE];
        // Getting the indirect pointers
        read_blocks(DATA_BLOCKS_OFFSET + inode.indirect, 1, ptrs);
        for (int i = 0; i < num_of_ptrs; ++i) {
            set_bit(ptrs[i]);
        }
    }
}

int sfs_remove(char *file_name) {
    uint32_t idx;
    const uint32_t inode_num = find_inode_num(file_name, &idx);
    if (inode_num >= MAX_NUM_OF_DIR_ENTRIES) {
        return -1;
    }
    // Remove the entry from the root directory
    root_dir[idx].inode_num = 0;
    move_invalid_entries_to_back(idx);
    inode_table[super_block.root_dir].size -= sizeof(directory_entry_t);
    write_from_ptr(inode_table[super_block.root_dir], root_dir);

    // Release the data blocks
    release_data_blocks(inode_table[inode_num]);
    write_blocks(FREE_BITMAP_OFFSET, NUM_OF_FREE_BITMAP_BLOCKS, free_block_map);

    // Release the inode
    inode_table[inode_num].size = 0;
    write_blocks(INODE_BLOCKS_OFFSET, NUM_OF_INODE_BLOCKS, inode_table);

    return 0;
}

