#ifndef AP3216C_H
#define AP3216C_H

#define AP3216C_ADDR    	0X1E	/* AP3216C device address */

/* AP3316C registers */
#define AP3216C_SYSTEMCONG	0x00	/* Configuration register */
#define AP3216C_INTSTATUS	0X01	/* Interrupt status register */
#define AP3216C_INTCLEAR	0X02	/* Interrupt clear register */
#define AP3216C_IRDATALOW	0x0A	/* IR data low byte */
#define AP3216C_IRDATAHIGH	0x0B	/* IR data high byte */
#define AP3216C_ALSDATALOW	0x0C	/* ALS data low byte */
#define AP3216C_ALSDATAHIGH	0X0D	/* ALS data high byte */
#define AP3216C_PSDATALOW	0X0E	/* PS data low byte */
#define AP3216C_PSDATAHIGH	0X0F	/* PS data high byte */

#define AP3216C_ALSCONFIG	0X10	/* ALS configuration register */
#define AP3216C_PSCONFIG	0X20	/* PS configuration register */
#define AP3216C_PSLEDCONFIG 0X21	/* LED configuration register */

#endif
