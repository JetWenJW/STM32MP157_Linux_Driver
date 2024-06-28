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
#include <errno.h>

/* Macro to read float data from file and convert it to float */
#define SENSOR_FLOAT_DATA_GET(ret, index, str, member)\
	ret = file_data_read(file_path[index], str);\
	dev->member = atof(str);\

/* Macro to read integer data from file and convert it to integer */
#define SENSOR_INT_DATA_GET(ret, index, str, member)\
	ret = file_data_read(file_path[index], str);\
	dev->member = atoi(str);\

/* File paths corresponding to ap3216c iio framework */
static char *file_path[] = {
	"/sys/bus/iio/devices/iio:device0/in_intensity_both_scale",
	"/sys/bus/iio/devices/iio:device0/in_intensity_both_raw",
	"/sys/bus/iio/devices/iio:device0/in_intensity_ir_raw",
	"/sys/bus/iio/devices/iio:device0/in_proximity_raw",
};

/* Enum for file path indices */
enum path_index {
	IN_INTENSITY_BOTH_SCALE = 0,
	IN_INTENSITY_BOTH_RAW,
	IN_INTENSITY_IR_RAW,
	IN_PROXIMITY_RAW,
};

/* Structure for ap3216c data device */
struct ap3216c_dev {
	int als_raw, ir_raw, ps_raw;  // Raw data variables
	float als_scale;               // Scale for ALS
	float als_act;                 // Actual ALS value
};

struct ap3216c_dev ap3216c;  // Instance of ap3216c data device

/* Function to read data from a file */
static int file_data_read(char *filename, char *str)
{
	int ret = 0;
	FILE *data_stream;

    data_stream = fopen(filename, "r");  // Open file for reading
    if(data_stream == NULL) {
		printf("can't open file %s\r\n", filename);  // Print error if file cannot be opened
		return -1;
	}

	ret = fscanf(data_stream, "%s", str);  // Read data from file into string
    if(!ret) {
        printf("file read error!\r\n");  // Print error if reading from file fails
    } else if(ret == EOF) {
        fseek(data_stream, 0, SEEK_SET);  // Reset file pointer to beginning if end of file is reached
    }
	fclose(data_stream);  // Close file
	return 0;
}

/* Function to read sensor data from files */
static int sensor_read(struct ap3216c_dev *dev)
{
	int ret = 0;
	char str[50];  // Buffer for storing read data

	SENSOR_FLOAT_DATA_GET(ret, IN_INTENSITY_BOTH_SCALE, str, als_scale);  // Read and convert ALS scale data
	SENSOR_INT_DATA_GET(ret, IN_INTENSITY_BOTH_RAW, str, als_raw);         // Read and convert ALS raw data
	SENSOR_INT_DATA_GET(ret, IN_INTENSITY_IR_RAW, str, ir_raw);            // Read and convert IR raw data
	SENSOR_INT_DATA_GET(ret, IN_PROXIMITY_RAW, str, ps_raw);               // Read and convert proximity raw data

	dev->als_act = dev->als_scale * dev->als_raw;  // Calculate actual ALS value
	return ret;
}

/* Main function */
int main(int argc, char *argv[])
{
	int ret = 0;

	if (argc != 1) {
		printf("Error Usage!\r\n");  // Print error if command line arguments are incorrect
		return -1;
	}

	while (1) {
		ret = sensor_read(&ap3216c);  // Read sensor data
		if(ret == 0) {  // If data read successfully
			printf("\r\nRaw values:\r\n");
			printf("als = %d, ps = %d, ir = %d\r\n", ap3216c.als_raw, ap3216c.ps_raw, ap3216c.ir_raw);  // Print raw sensor values
			printf("Actual values:");
			printf("act als = %.2f lx\r\n", ap3216c.als_act);  // Print actual ALS value
		}
		usleep(100000); /*100ms */
	}
	return 0;
}
