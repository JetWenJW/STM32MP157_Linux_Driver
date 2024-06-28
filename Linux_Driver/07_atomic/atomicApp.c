#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#define LEDOFF 	0
#define LEDON 	1

int main(int argc, char *argv[])
{
	int fd, retvalue;
	char *filename;
	unsigned char cnt = 0;
	unsigned char databuf[1];
	
	if(argc != 3){
		printf("Error Usage!\r\n");
		return -1;
	}

	filename = argv[1];

	// Open the device file
	fd = open(filename, O_RDWR);
	if(fd < 0){
		printf("file %s open failed!\r\n", argv[1]);
		return -1;
	}

	// Parse the command line argument to determine LED control action
	databuf[0] = atoi(argv[2]);

	// Write to the /dev/gpioled file to control the LED
	retvalue = write(fd, databuf, sizeof(databuf));
	if(retvalue < 0){
		printf("LED Control Failed!\r\n");
		close(fd);
		return -1;
	}

	// Simulate a task that keeps the LED control active for 25 seconds
	while(1) {
		sleep(5);
		cnt++;
		printf("App running times:%d\r\n", cnt);
		if(cnt >= 5) break;
	}

	printf("App running finished!");
	
	// Close the device file
	retvalue = close(fd);
	if(retvalue < 0){
		printf("file %s close failed!\r\n", argv[1]);
		return -1;
	}
	return 0;
}
