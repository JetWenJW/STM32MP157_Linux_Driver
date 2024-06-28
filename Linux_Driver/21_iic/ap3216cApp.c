#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    int fd;                     // File descriptor for the device file
    char *filename;             // Name of the device file
    unsigned short databuf[3];  // Buffer to hold sensor data
    unsigned short ir, als, ps; // Variables to hold IR, ALS, and PS sensor data
    int ret = 0;                // Return value for error checking

    // Check if the user provided exactly one argument (the filename)
    if (argc != 2) {
        printf("Error Usage!\n");
        return -1;
    }

    // Get the filename from the command line arguments
    filename = argv[1];
    
    // Open the device file in read-write mode
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        printf("can't open file %s\n", filename);
        return -1;
    }

    // Infinite loop to read and print sensor data
    while (1) {
        // Read sensor data from the device file into the buffer
        ret = read(fd, databuf, sizeof(databuf));
        if (ret == 0) {         /* Data read successfully */
            ir =  databuf[0];   /* IR sensor data */
            als = databuf[1];   /* ALS sensor data */
            ps =  databuf[2];   /* PS sensor data */
            // Print the sensor data
            printf("ir = %d, als = %d, ps = %d\n", ir, als, ps);
        }
        // Sleep for 200 milliseconds
        usleep(200000);
    }
    
    // Close the device file
    close(fd);
    return 0;
}
