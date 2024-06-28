#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

int main()
{
    int fd, ret;
    unsigned char data[5];

    // Open the DHT11 device file
    fd = open("/dev/dht11", 0);

    if(fd < 0)
    {
        perror("open device failed\n");
        exit(1);
    }
    else
        printf("Open success!\n");

    while(1)
    {
        ret = read(fd, &data, sizeof(data));
        if(ret == 0) 
        {	
            /* Read Data */
            // Verify checksum
			if(data[4] == data[0] + data[1] + data[2] + data[3]) {
				// Print temperature and humidity
				printf("Temperature: %d.%d°C, Humidity: %d.%d%%\n", data[2], data[3], data[0], data[1]);
			}
		}
        sleep(1);
    }
}