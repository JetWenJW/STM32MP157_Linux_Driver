#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#define LEDOFF 	0
#define LEDON 	1

/*
 * @description		: Main program
 * @param - argc 	: Number of elements in argv array
 * @param - argv 	: Command line arguments
 * @return 			: 0 on success; -1 on failure
 */
int main(int argc, char *argv[])
{
	int fd, retvalue;
	char *filename;
	unsigned char databuf[1];
	
	if(argc != 3){
		printf("Error Usage!\r\n");
		return -1;
	}

	filename = argv[1];

	/* Open the LED driver */
	fd = open(filename, O_RDWR);
	if(fd < 0){
		printf("File %s open failed!\r\n", argv[1]);
		return -1;
	}
	
	databuf[0] = atoi(argv[2]);	/* Operation to perform: turn LED on or off */
	retvalue = write(fd, databuf, sizeof(databuf));
	if(retvalue < 0){
		printf("LED Control Failed!\r\n");
		close(fd);
		return -1;
	}

	retvalue = close(fd); /* Close the file */
	if(retvalue < 0){
		printf("File %s close failed!\r\n", argv[1]);
		return -1;
	}
	return 0;
}
