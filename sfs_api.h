#ifndef SFS_API_H
#define SFS_API_H

#include <stdbool.h>
#include <stdint.h>

#define BLOCK_SIZE 1024
#define INDIRECT_LIST_SIZE (BLOCK_SIZE / 4)

typedef struct super_block_t {
    uint32_t magic;                 // magic number 0xACBD0005
    uint32_t block_size;
    uint32_t file_sys_size;         // number of blocks
    uint32_t inode_table_length;    // number of blocks
    uint32_t root_dir;              // i-node number
} super_block_t;

typedef struct inode_t {
    uint32_t mode;
    uint32_t link_cnt;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;  // This can be used to see how many blocks are occupied (and if it's free)
    uint32_t data_ptrs[12];
    uint32_t indirect[INDIRECT_LIST_SIZE];
} inode_t;

// Use an array of these as our file descriptor table
typedef struct file_descriptor_entry_t {
    uint32_t inode_num;
    uint32_t read_write_ptr;
} file_descriptor_entry_t;

typedef struct directory_entry_t {
    char *file_name;
    uint32_t inode_num;
} directory_entry_t;

void mksfs(int);

int sfs_getnextfilename(char *);

int sfs_getfilesize(const char *);

int sfs_fopen(char *);

int sfs_fclose(int);

int sfs_fwrite(int, const char *, int);

int sfs_fread(int, char *, int);

int sfs_fseek(int, int);

int sfs_remove(char *);

#endif
