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
		printf("Error Usage!\r\n");  // Print error message if arguments are incorrect
		return -1;
	}

	filename = argv[1];  // Get the device file name from command line arguments

	fd = open(filename, O_RDWR);  // Open the device file in read-write mode
	if(fd < 0){
		printf("file %s open failed!\r\n", argv[1]);  // Print error if file open fails
		return -1;
	}

	databuf[0] = atoi(argv[2]);  // Convert the second argument to integer and store in databuf

	retvalue = write(fd, databuf, sizeof(databuf));  // Write data to the device file
	if(retvalue < 0){
		printf("LED Control Failed!\r\n");  // Print error if write operation fails
		close(fd);  // Close the file descriptor
		return -1;
	}

	while(1) {
		sleep(5);  // Sleep for 5 seconds
		cnt++;
		printf("App running times:%d\r\n", cnt);  // Print the running times of the application
		if(cnt >= 5) break;  // Exit the loop after running 5 times
	}

	printf("App running finished!");  // Print message indicating application has finished
	retvalue = close(fd);  // Close the file descriptor
	if(retvalue < 0){
		printf("file %s close failed!\r\n", argv[1]);  // Print error if file close fails
		return -1;
	}
	return 0;  // Return success
}
