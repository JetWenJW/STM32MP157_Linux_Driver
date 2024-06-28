#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/unaligned/be_byteshift.h>
#include <linux/iio/trigger.h>
#include "ap3216creg.h"

#define AP3216C_NAME			"ap3216c"


enum inv_icm20608_scan {
	AP3216C_ALS,
	AP3216C_PS,
	AP3216C_IR,
};


static const int als_scale_ap3216c[] = {315000, 78800, 19700, 4900};

struct ap3216c_dev {
	struct i2c_client *client;	/* i2c 设备 */
	struct regmap *regmap;				/* regmap */
	struct regmap_config regmap_config;	
	struct mutex lock;
	struct iio_trigger  *trig;
};


static const struct iio_chan_spec ap3216c_channels[] = {
	/* ALS通道 */
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_BOTH,
		.address = AP3216C_ALSDATALOW,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = AP3216C_ALS,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},

	/* PS通道 */
	{
		.type = IIO_PROXIMITY,
		.address = AP3216C_PSDATALOW,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = AP3216C_PS,
		.scan_type = {
			.sign = 'u',
			.realbits = 10,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},

	/* IR通道 */
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_IR,
		.address = AP3216C_IRDATALOW,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = AP3216C_IR,
		.scan_type = {
			.sign = 'u',
			.realbits = 10,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
};


static const unsigned long ap3216c_scan_masks[] = {
	BIT(AP3216C_ALS)
	| BIT(AP3216C_PS)
	| BIT(AP3216C_IR),
	0,
};


static unsigned char ap3216c_read_reg(struct ap3216c_dev *dev, u8 reg)
{
	u8 ret;
	unsigned int data;

	ret = regmap_read(dev->regmap, reg, &data);
	return (u8)data;
}


static void ap3216c_write_reg(struct ap3216c_dev *dev, u8 reg, u8 data)
{
	regmap_write(dev->regmap, reg, data);
}


static int ap3216c_reginit(struct ap3216c_dev *dev)
{
	/* 初始化AP3216C */
	ap3216c_write_reg(dev, AP3216C_SYSTEMCONG, 0x04);		/* 复位AP3216C 			*/
	mdelay(50);												/* AP3216C复位最少10ms 	*/
	ap3216c_write_reg(dev, AP3216C_SYSTEMCONG, 0X03);		/* 开启ALS、PS+IR 		*/
	ap3216c_write_reg(dev, AP3216C_ALSCONFIG, 0X00);		/* ALS单次转换触发，量程为0～20661 lux */
	ap3216c_write_reg(dev, AP3216C_PSLEDCONFIG, 0X13);		/* IR LED 1脉冲，驱动电流100%*/

	return 0;
}


static int ap3216c_read_alsir_data(struct ap3216c_dev *dev, int reg,
				   int chann2, int *val)
{
	int ret = 0;
	unsigned char data[2];

	switch (chann2) {
	case IIO_MOD_LIGHT_BOTH:	/* 读取ALS数据 */
		ret = regmap_bulk_read(dev->regmap, reg, data, 2);
		*val = ((int)data[1] << 8) | data[0];   
		break;
	case IIO_MOD_LIGHT_IR:		/* 读取IR数据 */
		ret = regmap_bulk_read(dev->regmap, reg, data, 2);
		*val = ((int)data[1] << 2) | (data[0] & 0X03); 
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret) {
		return -EINVAL;
	}
		
	return IIO_VAL_INT;
}


static int ap3216c_write_als_scale(struct ap3216c_dev *dev, int chann2, int val)
{
	int ret = 0, i;	
	u8 d;

	switch (chann2) {
	case IIO_MOD_LIGHT_BOTH:	/* 设置ALS分辨率 */
		for (i = 0; i < ARRAY_SIZE(als_scale_ap3216c); ++i) {
			if (als_scale_ap3216c[i] == val) {
				d = (i << 4);
				ret = regmap_write(dev->regmap, AP3216C_ALSCONFIG, d);
			}
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}
		
	return ret;
}


static int ap3216c_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int ret = 0;
	unsigned char data[2];
	unsigned char regdata = 0;
	struct ap3216c_dev *dev = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:								/* 读取ICM20608加速度计、陀螺仪、温度传感器原始值 */
		mutex_lock(&dev->lock);								/* 上锁 			*/
		switch (chan->type) {
		case IIO_INTENSITY:
			ret = ap3216c_read_alsir_data(dev, chan->address, chan->channel2, val); /* 读取ALS */
			break;				/* 值为val */
		case IIO_PROXIMITY:
			ret = regmap_bulk_read(dev->regmap, chan->address, data, 2);
			*val = ((int)(data[1] & 0X3F) << 4) | (data[0] & 0X0F);  
			ret = IIO_VAL_INT; 	/* 值为val */
			break;
		default:
			ret = -EINVAL;
			break;
		}
		mutex_unlock(&dev->lock);							/* 释放锁 			*/
		return ret;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_INTENSITY:			/* ALS量程 */
			mutex_lock(&dev->lock);
			regdata = (ap3216c_read_reg(dev, AP3216C_ALSCONFIG) & 0X30) >> 4;
			*val  = 0;
			*val2 = als_scale_ap3216c[regdata];
			mutex_unlock(&dev->lock);
			return IIO_VAL_INT_PLUS_MICRO;	/* 值为val+val2/1000000 */
		default:
			return -EINVAL;
		}
		return ret;
		
	default:
		return -EINVAL;
	}
	return ret;
}

static int ap3216c_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	int ret = 0;
	struct ap3216c_dev *dev = iio_priv(indio_dev);

	iio_device_claim_direct_mode(indio_dev);		/* 保持direct模式 	*/
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:	/* 设置ALS量程 */
		switch (chan->type) {
		case IIO_INTENSITY:		/* 设置ALS量程 */
			mutex_lock(&dev->lock);
			ret = ap3216c_write_als_scale(dev, chan->channel2, val2);
			mutex_unlock(&dev->lock);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	
	default:
		ret = -EINVAL;
		break;
	}

	iio_device_release_direct_mode(indio_dev);			/* 释放direct模式 	*/
	return ret;
}


static int ap3216c_write_raw_get_fmt(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_INTENSITY:		/* 用户空间写的陀螺仪分辨率数据要乘以1000000 */
			return IIO_VAL_INT_PLUS_MICRO;
		default:				
			return IIO_VAL_INT_PLUS_MICRO;
		}
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}


static const struct iio_info ap3216c_info = {
	.read_raw		= ap3216c_read_raw,
	.write_raw		= ap3216c_write_raw,
	.write_raw_get_fmt = &ap3216c_write_raw_get_fmt,	/* 用户空间写数据格式 */
};

static int ap3216c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	struct ap3216c_dev *dev;
	struct iio_dev *indio_dev;

	/*  1、申请iio_dev内存 */
	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*dev));
	if (!indio_dev)
		return -ENOMEM;

	/* 2、获取ap3216c_dev结构体地址 */
	dev = iio_priv(indio_dev); 
	dev->client = client;
	
	i2c_set_clientdata(client, indio_dev); /* 保存ap3216cdev结构体 */
		
	/* 初始化regmap_config设置 */
	dev->regmap_config.reg_bits = 8;		/* 寄存器长度8bit */
	dev->regmap_config.val_bits = 8;		/* 值长度8bit */

	/* 初始化IIC接口的regmap */
	dev->regmap = regmap_init_i2c(client, &dev->regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err_regmap_init;
	}	

	mutex_init(&dev->lock);	

	/* 4、iio_dev的其他成员变量 */
	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &ap3216c_info;
	indio_dev->name = AP3216C_NAME;	
	indio_dev->modes = INDIO_DIRECT_MODE;	/* 直接模式，提供sysfs接口 */
	indio_dev->channels = ap3216c_channels;
	indio_dev->num_channels = ARRAY_SIZE(ap3216c_channels);
	indio_dev->available_scan_masks = ap3216c_scan_masks;

	/* 5、注册iio_dev */
	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "iio_device_register failed\n");
		goto err_iio_register;
	}

	ap3216c_reginit(dev); /* 初始化ap3216c */
	return 0;

err_iio_register:
err_regmap_init:
	iio_device_unregister(indio_dev);
	return ret;
}

static int ap3216c_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ap3216c_dev *dev;
	
	dev = iio_priv(indio_dev);

	/* 1、释放regmap */
	regmap_exit(dev->regmap);
	/* 2、注销IIO */
	iio_device_unregister(indio_dev);
	return 0;
}

/* 传统匹配方式ID列表 */
static const struct i2c_device_id ap3216c_id[] = {
	{"alientek,ap3216c", 0},  
	{}
};

/* 设备树匹配列表 */
static const struct of_device_id ap3216c_of_match[] = {
	{ .compatible = "alientek,ap3216c" },
	{ /* Sentinel */ }
};

/* i2c驱动结构体 */	
static struct i2c_driver ap3216c_driver = {
	.probe = ap3216c_probe,
	.remove = ap3216c_remove,
	.driver = {
			.owner = THIS_MODULE,
		   	.name = "ap3216c",
		   	.of_match_table = ap3216c_of_match, 
		   },
	.id_table = ap3216c_id,
};

static int __init ap3216c_init(void)
{
	int ret = 0;

	ret = i2c_add_driver(&ap3216c_driver);
	return ret;
}

static void __exit ap3216c_exit(void)
{
	i2c_del_driver(&ap3216c_driver);
}

/* module_i2c_driver(ap3216c_driver) */

module_init(ap3216c_init);
module_exit(ap3216c_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
