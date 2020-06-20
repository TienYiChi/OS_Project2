#include <stdio.h>
#include <stdlib.h>
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


int main (int argc, char* argv[])
{
	char buf[BUF_SIZE];
	int i, dev_fd, file_fd, shm_fd;// the fd for the device and the fd for the input file
	size_t ret, file_size, offset = 0, tmp;
	void *shm_address = NULL, *file_address = NULL;
	struct timeval start;
	struct timeval end;
	double trans_time; //calulate the time between the device is opened and it is closed
	struct shm_comm_info info;


	if( (dev_fd = open("/dev/master_device", O_RDWR)) < 0)
	{
		perror("failed to open /dev/master_device\n");
		return 1;
	}
	gettimeofday(&start ,NULL);
	if( (file_fd = open (argv[0], O_RDWR)) < 0 )
	{
		perror("failed to open input file\n");
		return 1;
	}

	if( (file_size = get_filesize(argv[0])) < 0)
	{
		perror("failed to get filesize\n");
		return 1;
	}


	if(ioctl(dev_fd, 0x12345677) == -1) //0x12345677 : create socket and accept the connection from the slave
	{
		perror("ioclt server create socket error\n");
		return 1;
	}


	switch(argv[1][0])
	{
		case 'f': //fcntl : read()/write()
			do
			{
				ret = read(file_fd, buf, sizeof(buf)); // read from the input file
				write(dev_fd, buf, ret);//write to the the device
			}while(ret > 0);
			break;
		case 'm':
			shm_fd = shm_open(SHM_ID, O_CREAT | O_EXCL | O_RDWR, 0600);
			if (fd < 0) {
				perror("shm_open()");
    			return EXIT_FAILURE;
  			}
			ftruncate(shm_fd, SHM_SIZE);

			// This is Linux default mmap.
			if((file_address = mmap(NULL,file_size,PROT_READ,MAP_SHARED,file_fd, 0)) == MAP_FAILED)
			{
				perror("mmap input file error\n");
				return 1;
			}

			if((shm_address = mmap(NULL,file_size,PROT_WRITE,MAP_SHARED,shm_fd, 0)) == MAP_FAILED)
			{
				perror("mmap device error\n");
				return 1;
			}
			info.from_addr = file_address;
			info.to_addr = shm_address;
			info.len = file_size;
			if(ioctl(dev_fd,0x12345678,file_size, &info) < 0)
			{
				perror("ioctl error\n");
				return 1;
			}
			// size_t len = file_size;
			// if ((file_size - offset) < PAGE_SIZE)
			// {
			// 	len = file_size - offset;
			// }
			// memcpy(shm_address,file_address,file_size);
			// offset = offset + len;

			munmap(file_address, file_size);
			munmap(shm_address, file_size);
			break;
	}

	if(ioctl(dev_fd, 0x12345679) == -1) // end sending data, close the connection
	{
		perror("ioclt server exits error\n");
		return 1;
	}
	gettimeofday(&end, NULL);
	trans_time = (end.tv_sec - start.tv_sec)*1000 + (end.tv_usec - start.tv_usec)*0.0001;
	printf("Transmission time: %lf ms, File size: %d bytes\n", trans_time, file_size / 8);

	close(file_fd);
	close(dev_fd);

	return 0;
}

size_t get_filesize(const char* filename)
{
    struct stat st;
    stat(filename, &st);
    return st.st_size;
}
