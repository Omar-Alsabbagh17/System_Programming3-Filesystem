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
#define FAT_EOC 0xFFFF
#define MAX_FS 32  //maximum of 32 file descriptors that can be open simultaneously.

/* Function declarations */
int file_locator(const char* );


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


struct file_descriptor_t {           
        uint64_t offset;  
	char   file_name[FS_FILENAME_LEN];
	uint8_t   is_free;
};

struct superblock_t  superblock;
struct root_t root[FS_FILE_MAX_COUNT]; // 128 entries. each entry is 32byte 
uint16_t* FAT; // used to traverse FAT entries
struct file_descriptor_t fd_table[MAX_FS]; // we can have up to 32 FS


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

int fs_create(const char *filename)
{
	// 	/* TODO: Phase 2 */
	int count = 0;
	if(filename == NULL || filename[strlen(filename)] != '\0' || strlen(filename) > FS_FILENAME_LEN){
		return -1;
	}
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if(strlen(root[i].filename) != 0) {
			if(strcmp(root[i].filename, filename) == 0) {
				return -1;
			}
			else {
				count += 1;
			}
		}
		if(count == FS_FILE_MAX_COUNT) {
			return -1;
		}
	}
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (root.entry[i].filename[0] == '\0') {
			strcpy(root.entry[i].filename, filename);
			root.entry[i].file_size = 0;
			root.entry[i].idx_first_blk = FAT_EOC;
			block_write(superblock.root_dir_index,&root);
			break;
		}
	}
	return 0;
}

int fs_delete(const char *filename)
{
	// 	/* TODO: Phase 2 */
	if(filename == NULL || filename[strlen(filename)] != '\0' || strlen(filename) > FS_FILENAME_LEN){
		return -1;
	}
	int len = strlen(filename);
	int file_exists = 0;
	uint16_t data_index = FAT_EOC;
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(strcmp(root[i].filename, filename) == 0) {
			file_exists = 1;
			data_index = root.entry[i].idx_first_blk;

			root.entry[i].filename[0] = '\0';
			root.entry[i].file_size = 0;
			root.entry[i].idx_first_blk = FAT_EOC;
			block_write(superblock.root_dir_index,&root);
			break;
		}
	}
	if(file_exists == 0) return -1;

	uint16_t next_data_index = data_index;
	uint16_t next = data_index;
	while(next != FAT_EOC){
		next_data_index = FAT[next];
		FAT[next] = 0;
		next = next_data_index;
	}
	return 0;
}

int fs_ls(void)
{
	// 	/* TODO: Phase 2 */
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (root[i].filename[0] != '\0') {
			printf("File: %s, Size: %i, Data_blk: %i\n", root[i].filename, root[i].file_size, root[i].idx_first_blk);
		}
	}
	return 0;
}

// properly set FD, then return it
int fs_open(const char *filename)
{
	if (block_disk_count() == -1)
		// no disk is open
		return -1;
	
	if (strlen(filename) > FS_FILENAME_LEN)
	{
		printf("file name is too long\n");
		return -1;
	}

	int f_index = file_locator(filename);
	if (f_index == -1)
	{
		printf("There is no such file with name %s\n", filename);
		return -1;
	}
	for( uint8_t i = 0; i < MAX_FS; i++)
	{
		if (fd_table[i].is_free)
		{
			// we found free FS entry
			fd_table[i].is_free = 0;  // this FD entry is no longer free
			fd_table[i].offset =  0; // start from the begining of the file
			strcpy(fd_table[i].file_name, filename);
			return i;
		}
	}
	printf("Couldn't open the file. All FD entries have been used\n");
	return -1;

}

int fs_close(int fd)
{
	if (block_disk_count() == -1)
		return -1;
	
	if (fd >= MAX_FS)
	{
		printf("Invalid FD\n");
		return -1;
	}
	
	if (fd_table[fd].is_free)
	{
		printf("the file with FD=%d is already closed\n", fd);
		return -1;
	}

	if (file_locator(fd_table[fd].file_name) == -1)
		return -1;
	
	/* reset FD entry */
	fd_table[fd].is_free = 1;
	fd_table[fd].offset = 0;
	return 0; //success

}

int fs_stat(int fd)
{
	/*  fd: the FD of the file
		Returns: the size of the file whom FD is provided */

	if ((fd > MAX_FS) || (fd < 0))
		return -1;
	
	if (fd_table[fd].is_free)
		return -1;
	
	int idx = file_locator(fd_table[fd].file_name);
	if (idx == -1)
		// no file with such name
		return -1;

	return (int) root[idx].file_size;
	
}

int fs_lseek(int fd, size_t offset)
{
	/* sets the offset of the file to the given offset */
	if (fs_stat(fd) == -1)
		// either fd is invalid, or FD is not used
		return -1;

	if ((int) offset < 0)
		return -1;

	if (fs_stat(fd) < (int)offset)
	// file size is less than the offset
		return -1;
	
	fd_table[fd].offset = offset;
	return 0; //sucess
}

int fs_write(int fd, void *buf, size_t count)
{
	// 	/* TODO: Phase 4 */

}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
	
}

/* ==========  HELPER FUNCTIONS  ======================================= */
int file_locator(const char* fname)
{
	/* PARAMETRS
		fname: name of the file to search for 

	   RETURN:
		the position of the file that matches fname
		-1 otherwise
	*/

	for (uint16_t i =0; i < FS_FILE_MAX_COUNT; ++i)
	{
		if (!strncmp(root[i].filename, fname, FS_FILENAME_LEN))
			return i;
	}
	return -1;

}
