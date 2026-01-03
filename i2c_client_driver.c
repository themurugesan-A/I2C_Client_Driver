/***************************************************************************//**
*  \file       i2c_driver.c
*
*  \details    Professional I2C Driver with Character Devices
*              SSD1306 OLED + AHT20 Sensor
*              Raspberry Pi 4B | Linux 6.12.x
*
*******************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

/* ===================== CONFIG ===================== */
#define I2C_BUS_AVAILABLE   1

#define SSD1306_ADDR        0x3C
#define AHT20_ADDR          0x38

#define OLED_DEV_NAME       "etx_oled"
#define AHT20_DEV_NAME      "etx_aht20"

/* ===================== GLOBALS ===================== */
static struct i2c_adapter *i2c_adap;
static struct i2c_client  *oled_client;
static struct i2c_client  *aht20_client;

/* Char devices */
static dev_t oled_dev, aht20_dev;
static struct cdev oled_cdev, aht20_cdev;

/* ===================== IOCTL ===================== */
#define OLED_CLEAR      _IO('o',1)
#define OLED_FILL       _IO('o',2)

struct aht20_data {
    int temperature;   /* x10 °C */
    int humidity;      /* x10 %  */
};

#define AHT20_READ_DATA _IOR('a',1, struct aht20_data)

/* ===================== SSD1306 ===================== */

static void oled_write(u8 mode, u8 data)
{
    u8 buf[2] = {mode, data};
    i2c_master_send(oled_client, buf, 2);
}

static void oled_init(void)
{
    msleep(100);
    oled_write(0x00, 0xAE);
    oled_write(0x00, 0xA8);
    oled_write(0x00, 0x3F);
    oled_write(0x00, 0xAF);
}

static void oled_clear(void)
{
    int i;
    for (i = 0; i < 1024; i++)
        oled_write(0x40, 0x00);
}

static void oled_fill(void)
{
    int i;
    for (i = 0; i < 1024; i++)
        oled_write(0x40, 0xFF);
}

/* OLED CHAR OPS */
static long oled_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case OLED_CLEAR:
        oled_clear();
        break;
    case OLED_FILL:
        oled_fill();
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

static struct file_operations oled_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = oled_ioctl,
};

/* ===================== AHT20 ===================== */

static int aht20_trigger(void)
{
    u8 cmd[3] = {0xAC, 0x33, 0x00};
    return i2c_master_send(aht20_client, cmd, 3);
}

static int aht20_read_raw(u32 *t, u32 *h)
{
    u8 d[6];

    msleep(80);
    i2c_master_recv(aht20_client, d, 6);

    *h = ((d[1] << 12) | (d[2] << 4) | (d[3] >> 4));
    *t = (((d[3] & 0x0F) << 16) | (d[4] << 8) | d[5]);
    return 0;
}

/* AHT20 CHAR OPS */
static long aht20_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    struct aht20_data data;
    u32 rt, rh;

    if (cmd != AHT20_READ_DATA)
        return -EINVAL;

    aht20_trigger();
    aht20_read_raw(&rt, &rh);

    data.humidity    = (rh * 1000) / 1048576;
    data.temperature = ((rt * 2000) / 1048576) - 500;

    if (copy_to_user((void *)arg, &data, sizeof(data)))
        return -EFAULT;

    return 0;
}

static struct file_operations aht20_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = aht20_ioctl,
};

/* ===================== I2C PROBE ===================== */

static int oled_probe(struct i2c_client *client)
{
    oled_client = client;
    oled_init();
    pr_info("SSD1306 OLED probed\n");
    return 0;
}

static int aht20_probe(struct i2c_client *client)
{
    aht20_client = client;
    pr_info("AHT20 sensor probed\n");
    return 0;
}

static const struct i2c_device_id oled_id[] = {
    { "ssd1306", 0 }, {}
};

static const struct i2c_device_id aht20_id[] = {
    { "aht20", 0 }, {}
};

static struct i2c_driver oled_driver = {
    .driver = { .name = "ssd1306" },
    .probe  = oled_probe,
    .id_table = oled_id,
};

static struct i2c_driver aht20_driver = {
    .driver = { .name = "aht20" },
    .probe  = aht20_probe,
    .id_table = aht20_id,
};

/* ===================== INIT / EXIT ===================== */
static struct i2c_board_info oled_info = {
    I2C_BOARD_INFO("ssd1306", SSD1306_ADDR),
};

static struct i2c_board_info aht20_info = {
    I2C_BOARD_INFO("aht20", AHT20_ADDR),
};

static int __init etx_init(void)
{
    i2c_adap = i2c_get_adapter(I2C_BUS_AVAILABLE);
    if (!i2c_adap)
        return -ENODEV;

    oled_client = i2c_new_client_device(i2c_adap, &oled_info);
    if (IS_ERR(oled_client))
        return PTR_ERR(oled_client);

    aht20_client = i2c_new_client_device(i2c_adap, &aht20_info);
    if (IS_ERR(aht20_client))
        return PTR_ERR(aht20_client);

    i2c_add_driver(&oled_driver);
    i2c_add_driver(&aht20_driver);

    /* Char devices */
    alloc_chrdev_region(&oled_dev, 0, 1, OLED_DEV_NAME);
    cdev_init(&oled_cdev, &oled_fops);
    cdev_add(&oled_cdev, oled_dev, 1);

    alloc_chrdev_region(&aht20_dev, 0, 1, AHT20_DEV_NAME);
    cdev_init(&aht20_cdev, &aht20_fops);
    cdev_add(&aht20_cdev, aht20_dev, 1);

    pr_info("ETX I2C Driver Loaded\n");
    return 0;
}

static void __exit etx_exit(void)
{
    cdev_del(&oled_cdev);
    cdev_del(&aht20_cdev);

    unregister_chrdev_region(oled_dev, 1);
    unregister_chrdev_region(aht20_dev, 1);

    i2c_unregister_device(oled_client);
    i2c_unregister_device(aht20_client);

    i2c_del_driver(&oled_driver);
    i2c_del_driver(&aht20_driver);

    i2c_put_adapter(i2c_adap);
    pr_info("ETX I2C Driver Removed\n");
}

module_init(etx_init);
module_exit(etx_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Murugesan");
MODULE_DESCRIPTION("Professional SSD1306 + AHT20 I2C Driver with Char Devices");
MODULE_VERSION("2.0");
