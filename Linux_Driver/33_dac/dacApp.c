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

/* IIO framework file paths */
static char *file_path[] = {
    "/sys/bus/iio/devices/iio:device0/in_voltage_scale",
    "/sys/bus/iio/devices/iio:device0/in_voltage19_raw",
    "/sys/bus/iio/devices/iio:device1/out_voltage1_scale",
    "/sys/bus/iio/devices/iio:device1/out_voltage1_raw",
};

/* Enumeration for file path indices */
enum path_index {
    IN_VOLTAGE_SCALE = 0,
    IN_VOLTAGE_RAW,
    OUT_VOLTAGE1_SCALE,
    OUT_VOLTAGE1_RAW,
};

/*
 * DAC data structure
 */
struct dac_dev{
    int dac_raw, adc_raw;       /* Raw DAC and ADC values */
    float dac_scale, adc_scale; /* Scale factors for DAC and ADC */
    float dac_act, adc_act;     /* Actual voltages for DAC and ADC */
};

struct dac_dev stm32dac; /* Instance of DAC data structure */

 /*
 * @description         : Read data from specified file
 * @param - filename    : File path to read from
 * @param - str         : String to store read data
 * @return              : 0 on success; other values on failure
 */
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

 /*
 * @description     : Read ADC and DAC data
 * @param - dev     : Device structure pointer
 * @return          : 0 on success; other values on failure
 */
static int dac_add_dac_read(struct dac_dev *dev)
{
    int ret = 0;
    char str[50];

    /* 1. Read ADC values */
    SENSOR_FLOAT_DATA_GET(ret, IN_VOLTAGE_SCALE, str, adc_scale);
    SENSOR_INT_DATA_GET(ret, IN_VOLTAGE_RAW, str, adc_raw);

    /* Convert ADC raw value to actual voltage in mV */
    dev->adc_act = (dev->adc_scale * dev->adc_raw)/1000.f;

    /* 2. Read DAC values */
    SENSOR_FLOAT_DATA_GET(ret, OUT_VOLTAGE1_SCALE, str, dac_scale);
    SENSOR_INT_DATA_GET(ret, OUT_VOLTAGE1_RAW, str, dac_raw);

    /* Convert DAC raw value to theoretical voltage in mV */
    dev->dac_act = (dev->dac_scale * dev->dac_raw)/1000.f;
    return ret;
}

 /*
 * @description     : Enable DAC
 * @return          : None
 */
void dac_enable(void)
{
    system("echo 0 > /sys/bus/iio/devices/iio:device1/out_voltage1_powerdown");
}

/*
 * @description     : Disable DAC
 * @return          : None
 */
void dac_disable(void)
{
    system("echo 1 > /sys/bus/iio/devices/iio:device1/out_voltage1_powerdown");
}

/*
 * @description         : Set DAC value
 * @param - filename    : File path to write to
 * @param - value       : DAC raw value to set
 * @return              : 0 on success; other values on failure
 */
int dac_set(char *filename, int value)
{
    int ret = 0;
    FILE *data_stream;
    char str[10];

    /* 1. Convert integer to string */
    sprintf(str, "%d", value);

    /* 2. Open file stream for writing */
    data_stream = fopen(filename, "w");
    if(data_stream == NULL) {
        printf("can't open file %s\r\n", filename);
        return -1;
    }

    /* 3. Reset file pointer to beginning of file */
    fseek(data_stream, 0, SEEK_SET);

    /* 4. Write data to file stream */
    ret = fwrite(str, sizeof(str), 1, data_stream);
    if(!ret) {
        printf("file read error!\r\n");
    }

    /* 5. Close file */
    fclose(data_stream);
    return 0;
}

/*
 * @description         : Main program
 * @param - argc        : Number of arguments
 * @param - argv        : Array of arguments
 * @return              : 0 on success; other values on failure
 */
int main(int argc, char *argv[])
{
    int ret = 0;
    unsigned int cmd;
    unsigned char str[100];

    if (argc != 1) {
        printf("Error Usage!\r\n");
        return -1;
    }

    dac_enable(); /* Enable DAC */
    while (1) {
        printf("Enter DAC raw value (0~4095):");
        ret = scanf("%d", &cmd);
        if (ret != 1) {             /* Incorrect parameter input */
            fgets(str, sizeof(str), stdin);             /* Prevents from getting stuck */
        } else {                    /* Correct parameter input */
            if((cmd < 0) || (cmd > 4095)) {
                printf("Input error, please enter correct DAC value, range: 0~4095!\r\n");
                continue;
            }
            dac_set(file_path[OUT_VOLTAGE1_RAW], cmd);
            ret = dac_add_dac_read(&stm32dac);
            if(ret == 0) {          /* Data read successful */
                printf("DAC raw value: %d, theoretical voltage: %.3fV\r\n", stm32dac.dac_raw, stm32dac.dac_act);
                printf("ADC raw value: %d, actual voltage: %.3fV\r\n", stm32dac.adc_raw, stm32dac.adc_act);
                printf("\r\n");
            }
        }
    }
    return 0;
}
