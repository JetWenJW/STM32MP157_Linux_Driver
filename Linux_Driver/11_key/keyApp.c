#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#define KEY0VALUE 0XF0
#define INVAKEY   0X00

int main(int argc, char *argv[])
{
    int fd, ret;
    char *filename;
    int keyvalue;
    
    if (argc != 2) {
        printf("Error Usage!\r\n");  // Print error message if incorrect usage
        return -1;
    }

    filename = argv[1];

    /* Open the key driver */
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        printf("file %s open failed!\r\n", argv[1]);  // Print error message if file open fails
        return -1;
    }

    /* Loop to read key value data */
    while (1) {
        read(fd, &keyvalue, sizeof(keyvalue));  // Read key value from device file
        if (keyvalue == KEY0VALUE) {
            printf("KEY0 Press, value = %#X\r\n", keyvalue);  // Print message when KEY0 is pressed
        }
    }

    ret = close(fd); /* Close the file */
    if (ret < 0) {
        printf("file %s close failed!\r\n", argv[1]);  // Print error message if file close fails
        return -1;
    }
    return 0;
}
