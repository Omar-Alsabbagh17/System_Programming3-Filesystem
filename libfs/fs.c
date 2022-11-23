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
#define MAX_FD 32  //maximum of 32 file descriptors that can be open simultaneously.

#define MIN(a,b) (((a)<(b))?(a):(b)) // find minuim of two


/* Function declarations */
int file_locator(const char* );
int current_block_loactor(uint64_t offset,  int first_blk_index);
int free_db_entries_locator();


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
struct file_descriptor_t fd_table[MAX_FD]; // we can have up to 32 FS

// ======= PHASE 1   ====================================================================================

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

	FAT  = malloc(superblock.n_FAT_blks * BLOCK_SIZE);
	if (!FAT)
		return -1;

	// read all FAT entries as a chunks of blocks
	for (int i = 0; i < superblock.n_FAT_blks; i++)
	{   // read from i+1, since the first blk is superblock
		if ( block_read(i+1, (void*) FAT+(i*BLOCK_SIZE)) == -1)
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
	for (int i = 0; i < MAX_FD; ++i)
		fd_table[i].is_free = 1; // mark every in fd_table as free
	
	return 0; //everything was succesful
}

int fs_umount(void)
{
	//update FAT entries
	for (int i = 0; i < superblock.n_FAT_blks; i++)
	{   // write to i+1, since the first blk is superblock
		if ( block_write(i+1, (void*)FAT+(i*BLOCK_SIZE)) == -1)
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
		if (FAT[i] == 0) // Entries marked as 0 correspond to free data blocks
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


// ======= PHASE 2   ====================================================================================


int fs_create(const char *filename)
{
	/* create new empty file named filename in the root directory */

	if (block_disk_count() == -1)
		// no disk is open
		return -1;

	int count = 0;
	if(filename == NULL || filename[strlen(filename)] != '\0' || strlen(filename) > FS_FILENAME_LEN){
		return -1;
	}

	for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if(strlen(root[i].filename) != 0) {
			if(strcmp(root[i].filename, filename) == 0) {
				// file named @filename already exists
				return -1;
			}
			else {
				count += 1;
			}
		}
		if(count == FS_FILE_MAX_COUNT) {
			// root already contains %FS_FILE_MAX_COUNT
			return -1;
		}
	}
	
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (root[i].filename[0] == '\0') { // look for empty entry

			/* empty file, would have its size be 0, 
			and the index of the first data block be FAT_EOC. */
			memcpy(root[i].filename,filename,sizeof(root[i].filename));
			root[i].file_size = 0;
			root[i].idx_first_blk = FAT_EOC;
			// update root entries
			if (block_write(superblock.root_dir_index, root) == -1)
				// if failed to update root
				return -1;
			break;
		}
	}
	return 0;
}

int fs_delete(const char *filename)
{
	
	if (block_disk_count() == -1)
		// no disk is open
		return -1;

	if(filename == NULL || filename[strlen(filename)] != '\0' || strlen(filename) > FS_FILENAME_LEN){
		// invalid filename
		return -1; 
	}

	int file_idx = file_locator(filename);
	if (file_idx == -1)
		return -1;  // no such file exists
	
	for (int i =0; i < MAX_FD; i++)
	{
		if (!strcmp(root[file_idx].filename, fd_table[i].file_name))
		{  // one of entries in FD table, matches this filename

			if (!fd_table[i].is_free)
			{
				// printf("Can't remove the file. The file is still Open\n");
				return -1;
			}
		}
	}


	uint16_t data_index = FAT_EOC;
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(strcmp(root[i].filename, filename) == 0) {
			//free the root entry
			data_index = root[i].idx_first_blk;
			root[i].filename[0] = '\0';// if entry doesn't contain file, then first char will be NULL
			root[i].file_size = 0;
			root[i].idx_first_blk = FAT_EOC;
			block_write(superblock.root_dir_index,&root);
			break;
		}
	}

	// free Data blocks by setting their FAT to 0
	uint16_t next_data_index = data_index;
	uint16_t next = data_index;
	while(next != FAT_EOC){ // loop until we reach end-of-file
		next_data_index = FAT[next];
		FAT[next] = 0;
		next = next_data_index;
	}
	return 0;
}

int fs_ls(void)
{
	printf("FS Ls:\n");
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (root[i].filename[0] != '\0') {
			printf("file: %s, size: %i, data_blk: %i\n", root[i].filename, root[i].file_size, root[i].idx_first_blk);
		}
	}
	return 0;
}

// ======= PHASE 3   ====================================================================================

// properly set FD, then return it
int fs_open(const char *filename)
{
	if (block_disk_count() == -1)
		// no disk is open
		return -1;
	
	if (strlen(filename) > FS_FILENAME_LEN)
	{
		// printf("file name is too long\n");
		return -1;
	}

	int f_index = file_locator(filename);
	if (f_index == -1)
	{
		// printf("There is no such file with name %s\n", filename);
		return -1;
	}
	
	for( uint8_t i = 0; i < MAX_FD; i++)
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
	
	// printf("Couldn't open the file. All FD entries have been used\n");
	return -1;

}

int fs_close(int fd)
{
	if (block_disk_count() == -1)
		return -1;
	
	if (fd >= MAX_FD)
	{
		//printf("Invalid FD\n");
		return -1;
	}
	
	if (fd_table[fd].is_free)
	{
		// printf("the file with FD=%d is already closed\n", fd);
		return -1;
	}

	if (file_locator(fd_table[fd].file_name) == -1)
		return -1;
	
	/* reset FD entry */
	fd_table[fd].is_free = 1;
	fd_table[fd].offset = 0;
	fd_table[fd].file_name[0] = '\0';
	return 0; //success

}

int fs_stat(int fd)
{
	/* Returns: the size of the file whom @fd is provided */

	if ((fd >= MAX_FD) || (fd < 0))
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

// ======= PHASE 4  ====================================================================================
int fs_write(int fd, void *buf, size_t count)
{
	if (block_disk_count() == -1)
		return -1;
	if (fd_table[fd].is_free || fd >= MAX_FD || fd < 0)
		return -1;
	if (!buf)
		return -1;
	
	uint64_t offset = fd_table[fd].offset;
	int file_index = file_locator(fd_table[fd].file_name);
	if (file_index == -1)
		return -1; 

	uint8_t bounce_buf[BLOCK_SIZE];
	size_t offset_from_blk = offset % BLOCK_SIZE;
	size_t amount_to_write;
	int buf_offset = 0; // tracks how many bytes we wrote

	uint16_t first_blk_index = root[file_index].idx_first_blk;
	if (first_blk_index == FAT_EOC && (count > 0)) //empty file has fist_blk as FAT_EOC
	{
		// no longer empty file
		first_blk_index = free_db_entries_locator();
		root[file_index].idx_first_blk = first_blk_index;
		FAT[first_blk_index] = FAT_EOC;
	}
	// current_blk == blk where offset if located at
	uint16_t current_blk = current_block_loactor(offset, first_blk_index);

	while (1)
	{
		if (offset_from_blk != 0)
		{
			/* case 1: offset is not at the begining of block*/
			amount_to_write = MIN(count, BLOCK_SIZE);
			if (amount_to_write == BLOCK_SIZE)
				amount_to_write -= offset_from_blk;
			else if ((amount_to_write + offset_from_blk) > BLOCK_SIZE)
				amount_to_write = BLOCK_SIZE - offset_from_blk;

			block_read(current_blk, bounce_buf); //read into bounce_buffer
			memcpy(bounce_buf + offset_from_blk, buf+buf_offset ,amount_to_write );
			block_write(current_blk, bounce_buf); //write bounce into file
		}
		else if (count < BLOCK_SIZE)
		{
			/* case 2: less than a block to write and 
				offset aligned with block*/

			block_read(current_blk, bounce_buf);
			amount_to_write = count;
			memcpy(bounce_buf, buf + buf_offset, amount_to_write);
			block_write(current_blk, bounce_buf); // update block
		}
		else
		{
			/* case 3: offset is aligned and more than a block left*/
			block_write(current_blk, buf + buf_offset); // update file block
			amount_to_write = BLOCK_SIZE; 
		}

		offset += amount_to_write;
		buf_offset += amount_to_write;
		count -= amount_to_write;
		offset_from_blk = offset % BLOCK_SIZE;
		if (count == 0)
			break;

		current_blk = FAT[current_blk]; // jump to next block of file
		
		if (FAT[current_blk] == FAT_EOC)
		{ // extend file size if reached end of the file

			int free_blk_index = free_db_entries_locator();
			if (free_blk_index == -1)
				//no more space left in the disk
				break ;
			FAT[current_blk] = free_blk_index;
			FAT[free_blk_index] = FAT_EOC; 

		}
		
	}
	//update file size
	root[file_index].file_size += buf_offset;
	fd_table[fd].offset = offset;
	return buf_offset;
}

int fs_read(int fd, void *buf, size_t count)
{
	/* read @count bytes of data from file into @buf
		returns : num of bytes read  */

	uint64_t offset = 	fd_table[fd].offset;
	uint16_t offset_from_blk = offset % BLOCK_SIZE;
	uint8_t bounce_buf[BLOCK_SIZE];

	uint32_t file_size;
	size_t amount_to_read;
	int buf_offset = 0; // tracks how many bytes we read
	

	if (block_disk_count() == -1)
		return -1;
	
	if (fd_table[fd].is_free || fd >= MAX_FD || fd < 0)
		return -1;

	if (!buf)
		return -1;
	
	int file_index = file_locator(fd_table[fd].file_name);
	if (file_index == -1)
		return -1;
	
	int current_blk = current_block_loactor(offset, root[file_index].idx_first_blk);
	
	
	file_size = root[file_index].file_size;
	if ((offset + count) > file_size)
		// reduce number of bytes to be read, since we reaching end of file
		count = file_size - offset;
	
	while (count > 0)
	{
		if (offset_from_blk != 0)
		{
			/* case 1: offset is not at the begining of block */
			amount_to_read = MIN(count, BLOCK_SIZE); // don't read more than a block
			if (amount_to_read == BLOCK_SIZE)
				amount_to_read -= offset_from_blk;
			else if ((amount_to_read + offset_from_blk) > BLOCK_SIZE)
				amount_to_read = BLOCK_SIZE - offset_from_blk;

			block_read(current_blk, bounce_buf);
			memcpy(buf+buf_offset, bounce_buf + offset_from_blk, amount_to_read);
		}
		else if (count < BLOCK_SIZE)
		{
			// case 2: less than a block  to read and offset aligned with block
			block_read(current_blk, bounce_buf);
			amount_to_read = count;
			memcpy(buf+buf_offset, bounce_buf, amount_to_read);

		}
		else {
			// case 3: offset is aligned to begining of block
			block_read(current_blk, buf+buf_offset);
			amount_to_read = BLOCK_SIZE;
		}
		offset += amount_to_read;
		buf_offset += amount_to_read;
		count -= amount_to_read;
		offset_from_blk = offset % BLOCK_SIZE;
		if (FAT[current_blk] == FAT_EOC)
			break; // end of file
		current_blk = FAT[current_blk]; // jump to next block of the file
	}
	
	// loop finished
	fd_table[fd].offset = offset; // update file offset
	return buf_offset; // # of bytes that we read
}

/* ==========  HELPER FUNCTIONS  ======================================= */
int file_locator(const char* fname)
{
	/* PARAMETRS
		fname: name of the file to search for 

	   RETURN:
		the position of the file in the root directory that matches fname
		-1 otherwise
	*/

	for (uint16_t i =0; i < FS_FILE_MAX_COUNT; ++i)
	{
		if (!strncmp(root[i].filename, fname, FS_FILENAME_LEN))
			return i;
	}
	return -1;
}


int current_block_loactor(uint64_t offset,  int first_blk_index)
{
	/* calculates at which datablock the offset of 
	the file is located at   
	PARAMTERS:
		offset: offset of the file
		first_blk_index: index of first data block of the file
	*/
	int idx = first_blk_index;
	int blocks_to_jump = offset/BLOCK_SIZE;
	for (int i = 0; i < blocks_to_jump; i++)
	{
		idx = FAT[idx];
	}
	return idx + superblock.data_blk_start_index;

}

int free_db_entries_locator()
{
	/* searches the disk for free datablock entries 
	
	Returns:
	the index of the free datablock
	if no datablock left in the disk, returns -1
	   */
	for (u_int16_t i = 0; i < superblock.n_data_blks; i++)
	{
		if (FAT[i] == 0)
		{
			return i;
		}
	}
	return -1; // no more space left in the disk
}
