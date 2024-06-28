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

/* Macros for converting string to float and integer */
#define SENSOR_FLOAT_DATA_GET(ret, index, str, member)\
    ret = file_data_read(file_path[index], str);\
    dev->member = atof(str);\

#define SENSOR_INT_DATA_GET(ret, index, str, member)\
    ret = file_data_read(file_path[index], str);\
    dev->member = atoi(str);\

/* ADC IIO framework file paths */
static char *file_path[] = {
    "/sys/bus/iio/devices/iio:device0/in_voltage_scale",
    "/sys/bus/iio/devices/iio:device0/in_voltage19_raw",
};

/* Enumeration for file path indices */
enum path_index {
    IN_VOLTAGE_SCALE = 0,
    IN_VOLTAGE_RAW,
};

/* ADC data structure */
struct adc_dev{
    int raw;        /* Raw ADC value */
    float scale;    /* Scale factor for ADC */
    float act;      /* Actual voltage */
};

struct adc_dev stm32adc; /* Instance of ADC data structure */

/* Function to read data from a specified file */
static int file_data_read(char *filename, char *str)
{
    int ret = 0;
    FILE *data_stream;

    data_stream = fopen(filename, "r");
    if(data_stream == NULL) {
        printf("can't open file %s\r\n", filename);
        return -1;
    }

    ret = fscanf(data_stream, "%s", str);
    if(!ret) {
        printf("file read error!\r\n");
    } else if(ret == EOF) {
        fseek(data_stream, 0, SEEK_SET);
    }
    fclose(data_stream);
    return 0;
}

/* Function to read ADC data */
static int adc_read(struct adc_dev *dev)
{
    int ret = 0;
    char str[50];

    /* Read scale factor and convert to float */
    SENSOR_FLOAT_DATA_GET(ret, IN_VOLTAGE_SCALE, str, scale);

    /* Read raw ADC value and convert to integer */
    SENSOR_INT_DATA_GET(ret, IN_VOLTAGE_RAW, str, raw);

    /* Calculate actual voltage in volts */
    dev->act = (dev->scale * dev->raw)/1000.f;
    return ret;
}

/* Main function */
int main(int argc, char *argv[])
{
    int ret = 0;

    /* Check command line arguments */
    if (argc != 1) {
        printf("Error Usage!\r\n");
        return -1;
    }

    /* Main loop to continuously read and print ADC data */
    while (1) {
        ret = adc_read(&stm32adc);
        if(ret == 0) {
            printf("ADC raw value: %d, voltage: %.3fV\r\n", stm32adc.raw, stm32adc.act);
        }
        usleep(100000); /* Sleep for 100ms */
    }
    return 0;
}
