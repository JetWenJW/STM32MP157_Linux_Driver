#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#define LEDOFF 0   /* Turn off the LED */
#define LEDON 1    /* Turn on the LED */

/*
 * @description  : main function
 * @param - argc : number of elements in argv array
 * @param - argv : command line arguments
 * @return       : 0 on success; other on failure
 */
int main(int argc, char *argv[]) {
    int fd, retvalue;
    char *filename;
    unsigned char cnt = 0;
    unsigned char databuf[1];

    if (argc != 3) {
        printf("Error Usage!\r\n");
        return -1;
    }

    filename = argv[1];

    /* Open the LED device file */
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        printf("file %s open failed!\r\n", argv[1]);
        return -1;
    }

    databuf[0] = atoi(argv[2]); /* Operation to perform: turn on or off */

    /* Write data to /dev/gpioled file */
    retvalue = write(fd, databuf, sizeof(databuf));
    if (retvalue < 0) {
        printf("LED Control Failed!\r\n");
        close(fd);
        return -1;
    }

    /* Simulate occupying the LED for 25 seconds */
    while (1) {
        sleep(5);
        cnt++;
        printf("App running times: %d\r\n", cnt);
        if (cnt >= 5) break;
    }

    printf("App running finished!\r\n");
    retvalue = close(fd); /* Close the file */
    if (retvalue < 0) {
        printf("file %s close failed!\r\n", argv[1]);
        return -1;
    }
    return 0;
}
