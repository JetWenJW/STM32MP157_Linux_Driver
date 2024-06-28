#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    int fd, ret;
    int key_val;

    // Check if the correct number of arguments are provided
    if (2 != argc) {
        printf("Usage:\n"
               "\t./keyApp /dev/key\n");
        return -1;
    }

    // Open the device file
    fd = open(argv[1], O_RDONLY);
    if (0 > fd) {
        printf("ERROR: %s file open failed!\n", argv[1]);
        return -1;
    }

    // Continuously read key press/release events
    for (;;) {
        read(fd, &key_val, sizeof(int));
        if (0 == key_val)
            printf("Key Press\n");
        else if (1 == key_val)
            printf("Key Release\n");
    }

    // Close the device file
    close(fd);
    return 0;
}
