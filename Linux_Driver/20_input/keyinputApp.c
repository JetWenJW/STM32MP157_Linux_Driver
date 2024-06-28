#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>

int main(int argc, char *argv[])
{
    int fd, ret;
    struct input_event ev;

    if (argc != 2) {
        printf("Usage:\n"
               "\t./keyinputApp /dev/input/eventX    @ Open Key\n"
        );
        return -1;
    }

    /* Open the device */
    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        printf("Error: file %s open failed!\r\n", argv[1]);
        return -1;
    }

    /* Read key events */
    for (;;) {
        ret = read(fd, &ev, sizeof(struct input_event));
        if (ret) {
            switch (ev.type) {
            case EV_KEY:
                if (KEY_0 == ev.code) {
                    if (ev.value)
                        printf("Key0 Press\n");
                    else
                        printf("Key0 Release\n");
                }
                break;
            
            /* Handle other types of events as needed */
            case EV_REL:
            case EV_ABS:
            case EV_MSC:
            case EV_SW:
                break;
            }
        } else {
            printf("Error: file %s read failed!\r\n", argv[1]);
            goto out;
        }
    }

out:
    /* Close the device */
    close(fd);
    return 0;
}
