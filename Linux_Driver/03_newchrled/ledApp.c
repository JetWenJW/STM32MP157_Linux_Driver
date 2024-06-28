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
 * @description	: Main function of the LED control application.
 * @param argc	: Number of command-line arguments.
 * @param argv	: Array of command-line arguments.
 * @return		: 0 on success, -1 on failure.
 */
int main(int argc, char *argv[])
{
	int fd, retvalue;
	char *filename;
	unsigned char databuf[1];
	
	/* Check for correct number of arguments */
	if (argc != 3) 
	{
		printf("Error Usage!\r\n");
		return -1;
	}

	filename = argv[1]; /* Get the filename from command-line argument */

	/* Open the LED device file */
	fd = open(filename, O_RDWR);
	if (fd < 0) 
	{
		printf("File %s open failed!\r\n", argv[1]);
		return -1;
	}

	databuf[0] = atoi(argv[2]); /* Convert the command argument to integer */

	/* Write data to the /dev/led file */
	retvalue = write(fd, databuf, sizeof(databuf));
	if (retvalue < 0) 
	{
		printf("LED Control Failed!\r\n");
		close(fd);
		return -1;
	}

	/* Close the file descriptor */
	retvalue = close(fd);
	if (retvalue < 0) 
	{
		printf("File %s close failed!\r\n", argv[1]);
		return -1;
	}

	return 0;
}
