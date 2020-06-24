#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include "common.h"

#define PAGE_SIZE 4096
#define BUF_SIZE 512
size_t get_filesize(const char* filename);//get the size of the input file

/**
*	argv[1]: method, argv[2]: # of files, argv[3...]: file names
**/
int main (int argc, char* argv[])
{
	char buf[BUF_SIZE];
	int dev_fd, file_fd, file_num=0;// the fd for the device and the fd for the input file
	size_t ret, file_size, block_size = 0, len_sent = 0, len_package = 0, offset = 0;
	size_t total_file_size=0;
	struct timeval start;
	struct timeval end;
	double trans_time; //calulate the time between the device is opened and it is closed
	double total_trans_time=0;
	void *device_addr = NULL, *file_addr = NULL;


	for (int i = 0; i < strlen(argv[2]); i++)
	{
		file_num = file_num*10;
		file_num = file_num + (argv[2][i] - '0');
	}
	// char file_name[file_num][100];
	// for (int i = 0; i < file_num; i++)
	// {
	// 	strcpy(file_name[i], argv[i + 3]);
	// }
	for (int i=0; i<file_num;i++)
	{
		block_size = 0, len_sent = 0, len_package = 0, offset = 0;
		if( (dev_fd = open("/dev/master_device", O_RDWR)) < 0)
		{
			perror("failed to open /dev/master_device\n");
			return 1;
		}
		gettimeofday(&start ,NULL);
		if( (file_fd = open(argv[i + 3], O_RDWR)) < 0 )
		{
			perror("failed to open input file\n");
			return 1;
		}

		if( (file_size = get_filesize(argv[i + 3])) < 0)
		{
			perror("failed to get filesize\n");
			return 1;
		} else {
			printf("master: file size %d bytes\n", file_size);
		}


		if(ioctl(dev_fd, 0x12345677) == -1) //0x12345677 : create socket and accept the connection from the slave
		{
			perror("ioclt server create socket error\n");
			return 1;
		}


		switch(argv[1][0]) {
			case 'f': //fcntl : read()/write()
				do
				{
					ret = read(file_fd, buf, sizeof(buf)); // read from the input file
					write(dev_fd, buf, ret);//write to the the device
					len_sent += ret;
				}while(ret > 0);
				break;
			case 'm':
				block_size = (1 << SHIFT_ORDER) * PAGE_SIZE;
				while(len_sent < file_size) {
					if(file_size - len_sent < block_size) {
						block_size = file_size - len_sent;
					} else {
						block_size = (1 << SHIFT_ORDER) * PAGE_SIZE;
					}
					if((file_addr=mmap(NULL,block_size,PROT_READ,MAP_SHARED,file_fd, offset))==MAP_FAILED) {
						perror("master: input file error\n");
						return 1;
					}
					if((device_addr=mmap(NULL,block_size,PROT_WRITE,MAP_SHARED,dev_fd,0))==MAP_FAILED) {
						perror("master: mmap device error\n");
						return 1;
					}

					memcpy(device_addr,file_addr, block_size);

					len_package = ioctl(dev_fd, 0x12345678, block_size);
					len_sent += len_package;
					offset += block_size;

					munmap(file_addr,block_size);
					munmap(device_addr,block_size);
				}
				// Signal of transmission done
				len_package = ioctl(dev_fd, 0x12345678, 0);
				if(len_sent < file_size) {
					perror("file size inconsistency\n");
					return 1;
				}
				break;
		}

		if(ioctl(dev_fd, 0x12345679) == -1) { // end sending data, close the connection
			perror("ioclt server exits error\n");
			return 1;
		}
		gettimeofday(&end, NULL);
		trans_time = (end.tv_sec - start.tv_sec)*1000 + (end.tv_usec - start.tv_usec)*0.0001;
		total_trans_time+=trans_time;
		total_file_size+=len_sent;
		printf("Transmission time: %lf ms, File size: %d bytes\n", trans_time, len_sent);

		close(file_fd);
		close(dev_fd);
	}
	printf("\n");
	printf("Total transmission time: %lf ms,Total file size: %lu bytes\n", total_trans_time, total_file_size);
	return 0;
}

size_t get_filesize(const char* filename)
{
    struct stat st;
    stat(filename, &st);
    return st.st_size;
}