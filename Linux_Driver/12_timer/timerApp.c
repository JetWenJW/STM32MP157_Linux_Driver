#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include <sys/ioctl.h>

#define CLOSE_CMD 		(_IO(0XEF, 0x1))	/* Close the timer */
#define OPEN_CMD		(_IO(0XEF, 0x2))	/* Open the timer */
#define SETPERIOD_CMD	(_IO(0XEF, 0x3))	/* Set timer period command */

int main(int argc, char *argv[])
{
    int fd, ret;
    char *filename;
    unsigned int cmd;
    unsigned int arg;
    unsigned char str[100];

    if (argc != 2) {
        printf("Error Usage!\r\n");
        return -1;
    }

    filename = argv[1];

    fd = open(filename, O_RDWR);
    if (fd < 0) {
        printf("Can't open file %s\r\n", filename);
        return -1;
    }

    while (1) {
        printf("Input CMD:");
        ret = scanf("%d", &cmd);
        if (ret != 1) {
            fgets(str, sizeof(str), stdin); /* Prevents from getting stuck */
        }
        if (cmd == 4)
            goto out;
        if (cmd == 1)
            cmd = CLOSE_CMD;
        else if (cmd == 2)
            cmd = OPEN_CMD;
        else if (cmd == 3) {
            cmd = SETPERIOD_CMD;
            printf("Input Timer Period:");
            ret = scanf("%d", &arg);
            if (ret != 1) {
                fgets(str, sizeof(str), stdin); /* Prevents from getting stuck */
            }
        }
        ioctl(fd, cmd, arg); /* Control the timer open/close and set period */
    }

out:
    close(fd);
}
