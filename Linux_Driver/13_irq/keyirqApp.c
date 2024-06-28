#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    int fd, key_val;

    /* Check if the number of arguments is correct */
    if (argc != 2) {
        printf("Usage:\n"
               "\t./keyApp /dev/key\n");
        return -1;
    }

    /* Open the device file */
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("ERROR: %s file open failed!\n", argv[1]);
        return -1;
    }

    /* Loop to read key data */
    for (;;) {
        read(fd, &key_val, sizeof(int));
        if (key_val == 0)
            printf("Key Press\n");
        else if (key_val == 1)
            printf("Key Release\n");
    }

    /* Close the device file */
    close(fd);
    return 0;
}
