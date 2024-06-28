#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#define LEDOFF  0
#define LEDON   1

int main(int argc, char *argv[])
{
    int fd, retvalue;
    char *filename;
    unsigned char databuf[1];
    
    // Check if the correct number of arguments is provided
    if(argc != 3){
        printf("Error Usage!\r\n");
        return -1;
    }

    filename = argv[1];

    // Open the LED driver file
    fd = open(filename, O_RDWR);
    if(fd < 0){
        printf("file %s open failed!\r\n", argv[1]);
        return -1;
    }

    // Determine the LED operation (turn on or turn off)
    databuf[0] = atoi(argv[2]);

    // Write data to /dev/led file to control the LED
    retvalue = write(fd, databuf, sizeof(databuf));
    if(retvalue < 0){
        printf("LED Control Failed!\r\n");
        close(fd);
        return -1;
    }

    // Close the file descriptor
    retvalue = close(fd);
    if(retvalue < 0){
        printf("file %s close failed!\r\n", argv[1]);
        return -1;
    }
    return 0;
}
