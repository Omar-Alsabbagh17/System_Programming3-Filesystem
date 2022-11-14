#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define SIG_LEN 8
#define SUPER_BLOCK_PADDING 4079
#define ROOT_PADDING 10
#define BLOCK_SIZE 4096

/*  n_ stands for "number of"  */
struct superblock_t {
    char     signature[SIG_LEN];  // must equal "ECS150FS"
    uint16_t n_blks;   // number of all blocks (super + fat + root + data)
    uint16_t root_dir_index;
    uint16_t data_blk_start_index;
    uint16_t n_data_blks;
    uint8_t n_FAT_blks; 
    uint8_t not_used[SUPER_BLOCK_PADDING]; // ignore
} __attribute__((packed));



struct root_t{
	char     filename[FS_FILENAME_LEN];  // including NULL
	uint32_t file_size;
	uint16_t idx_first_blk;
	uint8_t  not_used[ROOT_PADDING];
} __attribute__((packed));


struct superblock_t  superblock;
struct root_t root[FS_FILE_MAX_COUNT]; // 128 entries. each entry is 32byte 
uint16_t* FAT; // used to traverse FAT entries


int fs_mount(const char *diskname)
{
	if (block_disk_open(diskname) == -1) {
		return -1;
	}

	if (block_read(0, &superblock) == -1) /* read onto superblock*/
	{
		return -1;
	}

	if (strncmp((superblock.signature), "ECS150FS", SIG_LEN) != 0) {
		// signature doesn't match
		return -1;
	}

	if (superblock.n_blks != block_disk_count()) {
		return -1;
	}

	FAT  = malloc(superblock.n_data_blks * sizeof(uint16_t));

	// read all FAT entries as a chunks of blocks
	for (uint8_t i = 0; i < superblock.n_FAT_blks; i++)
	{   // read from i+1, since the first blk is superblock
		if ( block_read(i+1, FAT+(i*BLOCK_SIZE)) == -1)
		{
			free(FAT);
			return -1;
		}
	}

	// read Root entries
	if (block_read(superblock.root_dir_index, root) == -1)
	{
		free(FAT);
		return -1;
	}
	
	return 0; //everything was succesful
}

int fs_umount(void)
{
	//update FAT entries
	for (uint8_t i = 0; i < superblock.n_FAT_blks; i++)
	{   // write to i+1, since the first blk is superblock
		if ( block_write(i+1, FAT+(i*BLOCK_SIZE)) == -1)
		{
			free(FAT);
			return -1;
		}
	}
	// update root entries
	if (block_write(superblock.root_dir_index, root) == -1)
	{
		free(FAT);
		return -1;
	}

	free(FAT);
	block_disk_close(); 
	return 0; //everything was sucessful
}
 

int fs_info(void)
{
	uint8_t root_dir_free_count = 0;  // num of free directories in root
	for (uint8_t i = 0; i < (uint8_t) FS_FILE_MAX_COUNT; i++) 
	{
		if (root[i].filename[0] == '\0') 
			root_dir_free_count++;
	}

	int num_free_blks = 0;
	//read each entry from FAT
	for (uint16_t i =1; i < superblock.n_data_blks; i++) // i=1, since first entry is always FAT_EOC
	{
		if (*(FAT+i) == 0) // Entries marked as 0 correspond to free data blocks
			num_free_blks++;
	}


	fprintf(stdout, "FS Info:\n");
	fprintf(stdout,"total_blk_count=%u\n", superblock.n_blks);
	fprintf(stdout,"fat_blk_count=%u\n", superblock.n_FAT_blks);
	fprintf(stdout,"rdir_blk=%u\n", superblock.root_dir_index);
	fprintf(stdout,"data_blk=%u\n", superblock.data_blk_start_index);
	fprintf(stdout,"data_blk_count=%u\n", superblock.n_data_blks);
	fprintf(stdout,"fat_free_ratio=%u/%u\n", num_free_blks, superblock.n_data_blks);
	fprintf(stdout,"rdir_free_ratio=%u/%u\n", root_dir_free_count, FS_FILE_MAX_COUNT);
	return 0;
}

// int fs_create(const char *filename)
// {
// 	/* TODO: Phase 2 */
// }

// int fs_delete(const char *filename)
// {
// 	/* TODO: Phase 2 */
// }

// int fs_ls(void)
// {
// 	/* TODO: Phase 2 */
// }

// int fs_open(const char *filename)
// {
// 	/* TODO: Phase 3 */
// }

// int fs_close(int fd)
// {
// 	/* TODO: Phase 3 */
// }

// int fs_stat(int fd)
// {
// 	/* TODO: Phase 3 */
// }

// int fs_lseek(int fd, size_t offset)
// {
// 	/* TODO: Phase 3 */
// }

// int fs_write(int fd, void *buf, size_t count)
// {
// 	/* TODO: Phase 4 */
// }

// int fs_read(int fd, void *buf, size_t count)
// {
// 	/* TODO: Phase 4 */
// }

