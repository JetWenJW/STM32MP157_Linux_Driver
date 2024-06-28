#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "sys/ioctl.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include <poll.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
    int fd;                     // File descriptor for the device file
    char *filename;             // Name of the device file
    signed int databuf[7];      // Buffer to hold raw sensor data
    unsigned char data[14];     // Unused, can be removed
    signed int gyro_x_adc, gyro_y_adc, gyro_z_adc; // Raw gyroscope data
    signed int accel_x_adc, accel_y_adc, accel_z_adc; // Raw accelerometer data
    signed int temp_adc;        // Raw temperature data

    float gyro_x_act, gyro_y_act, gyro_z_act; // Processed gyroscope data
    float accel_x_act, accel_y_act, accel_z_act; // Processed accelerometer data
    float temp_act;             // Processed temperature data

    int ret = 0;                // Return value for error checking

    // Check if the user provided exactly one argument (the filename)
    if (argc != 2) {
        printf("Error Usage!\r\n"); // Print error message for incorrect usage
        return -1;
    }

    // Get the filename from the command line arguments
    filename = argv[1];
    
    // Open the device file in read-write mode
    fd = open(filename, O_RDWR);
    if(fd < 0) {
        printf("can't open file %s\r\n", filename); // Print error message if file cannot be opened
        return -1;
    }

    // Infinite loop to read and print sensor data
    while (1) {
        // Read sensor data from the device file into the buffer
        ret = read(fd, databuf, sizeof(databuf));
        if(ret == 0) { // Data read successfully
            // Assign raw sensor data to respective variables
            gyro_x_adc = databuf[0];
            gyro_y_adc = databuf[1];
            gyro_z_adc = databuf[2];
            accel_x_adc = databuf[3];
            accel_y_adc = databuf[4];
            accel_z_adc = databuf[5];
            temp_adc = databuf[6];

            // Convert raw data to actual values
            gyro_x_act = (float)(gyro_x_adc) / 16.4;
            gyro_y_act = (float)(gyro_y_adc) / 16.4;
            gyro_z_act = (float)(gyro_z_adc) / 16.4;
            accel_x_act = (float)(accel_x_adc) / 2048;
            accel_y_act = (float)(accel_y_adc) / 2048;
            accel_z_act = (float)(accel_z_adc) / 2048;
            temp_act = ((float)(temp_adc) - 25 ) / 326.8 + 25;

            // Print raw and processed sensor data
            printf("\r\n");
            printf("gx = %d, gy = %d, gz = %d\r\n", gyro_x_adc, gyro_y_adc, gyro_z_adc);
            printf("ax = %d, ay = %d, az = %d\r\n", accel_x_adc, accel_y_adc, accel_z_adc);
            printf("temp = %d\r\n", temp_adc);
            printf("act gx = %.2f°/S, act gy = %.2f°/S, act gz = %.2f°/S\r\n", gyro_x_act, gyro_y_act, gyro_z_act);
            printf("act ax = %.2fg, act ay = %.2fg, act az = %.2fg\r\n", accel_x_act, accel_y_act, accel_z_act);
            printf("act temp = %.2f°C\r\n", temp_act);
        }
        // Sleep for 100 milliseconds
        usleep(100000);
    }

    // Close the device file
    close(fd);
    return 0;
}
