/***************************************************************
Copyright © ALIENTEK Co., Ltd. 1998-2029. All rights reserved.
文件名		: icm20608_triggerApp.c
作者	  	: 左忠凯
版本	   	: V1.0
描述	   	: icm20608设备iio框架触发缓冲测试程序。
其他	   	: 无
使用方法	 ：./icm20608_triggerApp 
论坛 	   	: www.openedv.com
日志	   	: 初版V1.0 2021/8/21 左忠凯创建
***************************************************************/
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

/*
 * icm20608数据设备结构体
 */
struct icm20608_dev {
	unsigned char data[14];
	int accel_x_calibbias, accel_y_calibbias, accel_z_calibbias;
	int accel_x_raw, accel_y_raw, accel_z_raw;

	int gyro_x_calibbias, gyro_y_calibbias, gyro_z_calibbias;
	int gyro_x_raw, gyro_y_raw, gyro_z_raw;

	int temp_offset, temp_raw;

	float accel_scale, gyro_scale, temp_scale;

	float gyro_x_act, gyro_y_act, gyro_z_act;
	float accel_x_act, accel_y_act, accel_z_act;
	float temp_act;
};

struct icm20608_dev icm20608;

 /* 
 * @description		: 对icm20608相关触发进行设置 
 * @param   		: 无
 * @return 			: 无
 */
void icm20608_trigger_set(void)
{
	system("echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_accel_x_en");
	system("echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_accel_y_en");	
	system("echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_accel_z_en");
	system("echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_anglvel_x_en");
	system("echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_anglvel_y_en");
	system("echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_anglvel_z_en");
	system("echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_temp_en");

	system("echo icm20608-dev0 > /sys/bus/iio/devices/iio:device0/trigger/current_trigger");
	system("echo 14 > /sys/bus/iio/devices/iio:device0/buffer/length");
	system("echo 1 > /sys/bus/iio/devices/iio:device0/buffer/enable");
}

 /*
 * @description			: 读取指定文件内容
 * @param - filename 	: 要读取的文件路径
 * @param - str 		: 读取到的文件字符串
 * @return 				: 0 成功;其他 失败
 */
static int file_data_read(char *filename, char *str)
{
	int ret = 0;
	FILE *data_stream;

    data_stream = fopen(filename, "r"); /* 只读打开 */
    if(data_stream == NULL) {
		printf("can't open file %s\r\n", filename);
		return -1;
	}

	ret = fscanf(data_stream, "%s", str);
    if(!ret) {
        printf("file read error!\r\n");
    } else if(ret == EOF) {
        /* 读到文件末尾的话将文件指针重新调整到文件头 */
        fseek(data_stream, 0, SEEK_SET);  
    }
	fclose(data_stream);	/* 关闭文件 */	
	return 0;
}

/* 
 * @description		: 读取数据
 * @param - fd 		: 文件描述符
 * @return 			: 0 成功;其他 失败
 */
int icm20608_read(int fd, struct icm20608_dev *dev)
{
	int ret = 0;
	char str[50];
	int i = 0;

	file_data_read("/sys/bus/iio/devices/iio:device0/in_accel_scale", str);
	dev->accel_scale = atof(str);

	file_data_read("/sys/bus/iio/devices/iio:device0/in_anglvel_scale", str);
	dev->gyro_scale = atof(str);

	file_data_read("/sys/bus/iio/devices/iio:device0/in_temp_scale", str);
	dev->temp_scale = atof(str);

	ret = read(fd, dev->data, 14);

	dev->accel_z_raw 	= (signed short)((dev->data[4] << 8) | dev->data[5]);
	dev->accel_x_raw 	= (signed short)((dev->data[0] << 8) | dev->data[1]); 
	dev->accel_y_raw 	= (signed short)((dev->data[2] << 8) | dev->data[3]); 
	dev->accel_z_raw 	= (signed short)((dev->data[4] << 8) | dev->data[5]); 
	dev->temp_raw   	= (signed short)((dev->data[6] << 8) | dev->data[7]); 
	dev->gyro_x_raw  	= (signed short)((dev->data[8] << 8) | dev->data[9]); 
	dev->gyro_y_raw  	= (signed short)((dev->data[10] << 8) | dev->data[11]);
	dev->gyro_z_raw  	= (signed short)((dev->data[12] << 8) | dev->data[13]);

	/* 转换为实际数值 */
	dev->accel_x_act = dev->accel_x_raw * dev->accel_scale;
	dev->accel_y_act = dev->accel_y_raw * dev->accel_scale;
	dev->accel_z_act = dev->accel_z_raw * dev->accel_scale;

	dev->gyro_x_act = dev->gyro_x_raw * dev->gyro_scale;
	dev->gyro_y_act = dev->gyro_y_raw * dev->gyro_scale;
	dev->gyro_z_act = dev->gyro_z_raw * dev->gyro_scale;

	dev->temp_act = ((dev->temp_raw - dev->temp_offset) / dev->temp_scale) + 25;
	return ret;
}

/*
 * @description		: main主程序
 * @param - argc 	: argv数组元素个数
 * @param - argv 	: 具体参数
 * @return 			: 0 成功;其他 失败
 */
int main(int argc, char *argv[])
{
	int fd;
	int ret = 0;

    /* 判断传参个数是否正确 */
    if(2 != argc) {
        printf("Usage:\n"
               "\t./icm20608_triggerApp /dev/iio:device0\n"
              );
        return -1;
    }

	icm20608_trigger_set();	/* 配置好触发缓冲区相关设置 */

    /* 打开设备 */
    fd = open(argv[1], O_RDONLY);
    if(0 > fd) {
        printf("ERROR: %s file open failed!\n", argv[1]);
        return -1;
    }

	/* 循环轮训读取按键数据 */
    while(1) {
    	icm20608_read(fd, &icm20608);
		if(ret == 0) { 			/* 数据读取成功 */
			printf("\r\n原始值:\r\n");
			printf("gx = %d, gy = %d, gz = %d\r\n", icm20608.gyro_x_raw, icm20608.gyro_y_raw, icm20608.gyro_z_raw);
			printf("ax = %d, ay = %d, az = %d\r\n", icm20608.accel_x_raw, icm20608.accel_y_raw, icm20608.accel_z_raw);
			printf("temp = %d\r\n", icm20608.temp_raw);
			printf("实际值:");
			printf("act gx = %.2f°/S, act gy = %.2f°/S, act gz = %.2f°/S\r\n", icm20608.gyro_x_act, icm20608.gyro_y_act, icm20608.gyro_z_act);
			printf("act ax = %.2fg, act ay = %.2fg, act az = %.2fg\r\n", icm20608.accel_x_act, icm20608.accel_y_act, icm20608.accel_z_act);
			printf("act temp = %.2f°C\r\n", icm20608.temp_act);
		}
		usleep(100000); /*100ms */
    }
}