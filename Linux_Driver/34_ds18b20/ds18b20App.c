#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

int main()
{
    int fd, ret;
    unsigned char result[2];
    int TH, TL;
    short tmp = 0;
    float temperature;
    int flag = 0;

    fd = open("/dev/ds18b20", 0);

    if(fd < 0)
    {
        perror("open device failed\n");
        exit(1);
    }
    else
        printf("Open success!\n");

    while(1)
    {
        ret = read(fd, &result, sizeof(result)); 
		if(ret == 0) {	/* Data read successful */
			TL = result[0];
			TH = result[1];
    
			if((TH == 0XFF) && (TL == 0XFF))/* Skip if data is 0XFFFF */
				continue;
			if(TH > 7) {	/* Negative number handling */
				TH = ~TH;
				TL = ~TL;
				flag = 1;	/* Mark as negative */
			}

			tmp = TH;
			tmp <<= 8;
			tmp += TL;
        
			if(flag == 1) {
				temperature = (float)(tmp+1)*0.0625; /* Calculate temperature for negative numbers */
				temperature = -temperature;
			}else {
				temperature = (float)tmp *0.0625;	/* Calculate temperature for positive numbers */
			}            

			if(temperature < 125 && temperature > -55) {	/* Temperature range */
				printf("Current Temperature: %f\n", temperature);
			}
		}
    flag = 0;
    sleep(1);
    }
	close(fd);	/* Close file */
}
