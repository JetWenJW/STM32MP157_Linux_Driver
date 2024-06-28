#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static int fd;

/*
 * Signal handler function for SIGIO.
 * Reads the key value from the device file descriptor.
 * Prints "Key Press" if key_val is 0, "Key Release" if key_val is 1.
 */
static void sigio_signal_func(int signum)
{
    unsigned int key_val = 0;

    read(fd, &key_val, sizeof(unsigned int));
    if (0 == key_val)
        printf("Key Press\n");
    else if (1 == key_val)
        printf("Key Release\n");
}

/*
 * Main function.
 * Opens a device file specified by command-line argument.
 * Sets up SIGIO signal handler for asynchronous notification.
 * Polls for key events and prints corresponding messages.
 */
int main(int argc, char *argv[])
{
    int flags = 0;

    // Check if command-line arguments are correct
    if (2 != argc) {
        printf("Usage:\n"
               "\t./asyncKeyApp /dev/key\n"
              );
        return -1;
    }

    // Open device file in non-blocking mode
    fd = open(argv[1], O_RDONLY | O_NONBLOCK);
    if (0 > fd) {
        printf("ERROR: %s file open failed!\n", argv[1]);
        return -1;
    }

    // Set up signal handler for SIGIO
    signal(SIGIO, sigio_signal_func);
    fcntl(fd, F_SETOWN, getpid());      // Set owner of the file descriptor
    flags = fcntl(fd, F_GETFD);         // Get file descriptor flags
    fcntl(fd, F_SETFL, flags | FASYNC); // Enable asynchronous notification

    // Main loop to continuously poll for key events
    for (;;) {
        sleep(2);  // Sleep for 2 seconds
    }

    // Close the device file descriptor
    close(fd);
    return 0;
}
