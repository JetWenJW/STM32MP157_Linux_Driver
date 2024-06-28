#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>

int main(int argc, char *argv[])
{
    fd_set readfds;
    int key_val;
    int fd;
    int ret;

    if (2 != argc) {
        printf("Usage:\n"
               "\t./keyApp /dev/key\n"
              );
        return -1;
    }

    fd = open(argv[1], O_RDONLY | O_NONBLOCK);
    if (0 > fd) {
        printf("ERROR: %s file open failed!\n", argv[1]);
        return -1;
    }

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    for (;;) {
        ret = select(fd + 1, &readfds, NULL, NULL, NULL);
        switch (ret) {
        case 0: // Timeout
            /* Custom timeout handling */
            break;
        case -1: // Error
            /* Custom error handling */
            break;
        default:
            if (FD_ISSET(fd, &readfds)) {
                read(fd, &key_val, sizeof(int));
                if (0 == key_val)
                    printf("Key Press\n");
                else if (1 == key_val)
                    printf("Key Release\n");
            }
            break;
        }
    }

    close(fd);
    return 0;
}
