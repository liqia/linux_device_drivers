#include <sys/ioctl.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h> 
#include <stdio.h>
#include "scull_ioctl.h"

int main(){
	//int ioctl(int handle, int cmd,[int *argdx, int argcx]);
	int quantum = 66;
	int fd = open("/dev/scull",O_RDWR | O_NOCTTY | O_NDELAY);
	ioctl(fd, SCULL_IOCSQUANTUM, &quantum);
	ioctl(fd, SCULL_IOCGQUANTUM, &quantum);
	printf("quantum:%d\n", quantum);
	close(fd);
	return 0;
}