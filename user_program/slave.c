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

size_t get_filesize(int fd);//get the size of the shared memory file


/**
*	argv[1]: method, argv[2]: ip, argv[3]: # of files, argv[4...]: file names
**/
int main (int argc, char* argv[])
{
	char buf[BUF_SIZE];
	int i, dev_fd, file_fd, shm_fd;// the fd for the device and the fd for the input file
	size_t ret, data_size = -1;
	size_t file_size = 0;
	// char file_name[50];
	// char method[20];
	// // Note: INADDR_ANY means all IPs on this machine is being listened to.(See master_device.c, line 152)
	// // Note: since socket server was bound to INADDR_ANY, simply use 127.0.0.1 in argv.
	// char ip[20];
	struct timeval start;
	struct timeval end;
	double trans_time; //calulate the time between the device is opened and it is closed
	void *shm_address = NULL, *file_address = NULL;
	struct shm_comm_info info;


	if( (dev_fd = open("/dev/slave_device", O_RDWR)) < 0)//should be O_RDWR for PROT_WRITE when mmap()
	{
		perror("failed to open /dev/slave_device\n");
		return 1;
	}
	gettimeofday(&start ,NULL);
	if( (file_fd = open (argv[4], O_RDWR | O_CREAT | O_TRUNC)) < 0)
	{
		perror("failed to open input file\n");
		return 1;
	}


	switch(argv[1][0])
	{
		case 'f'://fcntl : read()/write()
			if(ioctl(dev_fd, 0x12345677, argv[2]) == -1) { //0x12345677 : connect to master in the device
				perror("ioclt create slave socket error\n");
				return 1;
			}
			do {
				ret = read(dev_fd, buf, sizeof(buf)); // read from the the device
				write(file_fd, buf, ret); //write to the input file
				file_size += ret;
			} while(ret > 0);

			if(ioctl(dev_fd, 0x12345679) == -1) { // end receiving data, close the connection 
				perror("ioclt client exits error\n");
				return 1;
			}
			break;
		case 'm'://mmap+shm
			shm_fd = shm_open(SHM_ID, O_RDWR, 0666);
			file_size = get_filesize(shm_fd);
			printf("shm size: %d\n", file_size);
			if (shm_fd < 0) {
				perror("shm_open()");
    			return EXIT_FAILURE;
  			}
			ftruncate(shm_fd, file_size);
			ftruncate(file_fd, file_size);

			// This is Linux default mmap.
			if((file_address = mmap(NULL,file_size,PROT_WRITE | PROT_READ,MAP_SHARED,file_fd, 0)) == MAP_FAILED)
			{
				perror("mmap input file error\n");
				return 1;
			}

			if((shm_address = mmap(NULL,file_size,PROT_WRITE | PROT_READ,MAP_SHARED,shm_fd, 0)) == MAP_FAILED)
			{
				perror("mmap device error\n");
				return 1;
			}
			// info.from_addr = shm_address;
			// info.to_addr = file_address;
			// info.len = file_size;
			// if(ioctl(dev_fd,0x12345678, &info) < 0)
			// {
			// 	perror("ioctl error\n");
			// 	return 1;
			// }
			//printk("Ready to copy from %p to %p.\n", (void*)info->from_addr, (void*)info->to_addr);
			memcpy((void *)file_address, (void *)shm_address, file_size);
			//printk("File copied.");

			munmap(file_address, file_size);
			munmap(shm_address, file_size);
			shm_unlink(SHM_ID);
			break;
	}
	write(1, "ioctl success\n", 14);

	gettimeofday(&end, NULL);
	trans_time = (end.tv_sec - start.tv_sec)*1000 + (end.tv_usec - start.tv_usec)*0.0001;
	printf("Transmission time: %lf ms, File size: %d bytes\n", trans_time, file_size);

	close(file_fd);
	close(dev_fd);
	return 0;
}


size_t get_filesize(int fd)
{
    struct stat st;
    fstat(fd, &st);
    return st.st_size;
}
