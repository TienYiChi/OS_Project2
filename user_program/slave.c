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

/**
*	argv[1]: method, argv[2]: ip, argv[3]: # of files, argv[4...]: file names
**/
int main (int argc, char* argv[])
{
	char buf[BUF_SIZE];
	int dev_fd, file_fd, file_num=0;// the fd for the device and the fd for the input file
	size_t ret, file_size = -1, block_size = 0, len_sent = 0, len_package = 0, offset=0;
	size_t total_file_size=0;
	struct timeval start;
	struct timeval end;
	double trans_time; //calulate the time between the device is opened and it is closed
	double total_trans_time=0;
	void *device_addr = NULL, *file_addr = NULL;

	for (int i = 0; i < strlen(argv[3]); i++) {
		file_num = file_num*10;
		file_num = file_num + (argv[3][i] - '0');
	}

	for (int i=0; i<file_num;i++) {
		file_size = -1, block_size = 0, len_sent = 0, len_package = 0, offset=0;
		if( (dev_fd = open("/dev/slave_device", O_RDWR)) < 0) { //should be O_RDWR for PROT_WRITE when mmap()
			perror("failed to open /dev/slave_device\n");
			return 1;
		}
		gettimeofday(&start ,NULL);
		if( (file_fd = open(argv[i + 4], O_RDWR | O_CREAT | O_TRUNC)) < 0) {
			perror("failed to open input file\n");
			return 1;
		}

		if(ioctl(dev_fd, 0x12345677,  argv[2]) == -1) {//0x12345677 : connect to master in the device
			perror("ioctl create slave socket error\n");
			return 1;
		}

	    write(1, "ioctl success\n", 14);

		switch(argv[1][0]) {
			case 'f'://fcntl : read()/write()
				do
				{
					ret = read(dev_fd, buf, sizeof(buf)); // read from the the device
					write(file_fd, buf, ret); //write to the input file
					file_size += ret;
				}while(ret > 0);
				break;
			case 'm'://mmap

				while(1) {
					block_size = (1 << SHIFT_ORDER) * PAGE_SIZE;

					if((len_package = ioctl(dev_fd, 0x12345678, block_size)) < 0) {
						perror("ioctl error\n");
						return 1;
					}
					if(len_package == 0) {
						file_size = len_sent;
						break;
					}
					
					posix_fallocate(file_fd, offset, block_size);
					if((file_addr=mmap(NULL, block_size, PROT_WRITE, MAP_SHARED, file_fd, offset))==MAP_FAILED) {
						perror("slave: output file error\n");
						return 1;
					}

					if((device_addr=mmap(NULL, block_size, PROT_READ, MAP_SHARED, dev_fd, 0))==MAP_FAILED) {
						perror("slave: mmap device error\n");
						return 1;
					} else {
						ioctl(dev_fd, 0x00000000, device_addr);
					}

					memcpy(file_addr, device_addr, len_package);

					munmap(file_addr, block_size);
					munmap(device_addr, block_size);

					offset += block_size;
					len_sent += len_package;
				}
				ftruncate(file_fd, file_size);
				break;
		}

		if(ioctl(dev_fd, 0x12345679) == -1)// end receiving data, close the connection
		{
			perror("ioctl client exits error\n");
			return 1;
		}
		gettimeofday(&end, NULL);
		trans_time = (end.tv_sec - start.tv_sec)*1000 + (end.tv_usec - start.tv_usec)*0.0001;
		printf("Transmission time: %lf ms,File size: %d bytes\n", trans_time, file_size);
		total_trans_time+=trans_time;
		total_file_size+=len_sent;

		close(file_fd);
		close(dev_fd);
	}
	printf("\n");
	printf("Total transmission time: %lf ms,Total file size: %lu bytes\n", total_trans_time, total_file_size);
	return 0;
}

