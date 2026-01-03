
/***************************************************************************//**
*  \file       i2c_driver.c
*
*  \details    Simple I2C driver for SSD1306 OLED Display and AHT20 Sensor
*              Compatible with Linux 6.12.25+rpt-rpi-v8
*
*******************************************************************************/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/kernel.h>

#define I2C_BUS_AVAILABLE   (1)            // I2C bus number (usually 1 for Raspberry Pi)
#define SLAVE_DEVICE_NAME   "ETX_OLED"     // OLED driver name
#define SSD1306_SLAVE_ADDR  (0x3C)         // OLED I2C address

#define AHT20_DEVICE_NAME   "AHT20_SENSOR" // AHT20 sensor name
#define AHT20_SLAVE_ADDR    (0x38)         // AHT20 I2C address

static struct i2c_adapter *etx_i2c_adapter     = NULL;
static struct i2c_client  *etx_i2c_client_oled = NULL;
static struct i2c_client  *etx_i2c_client_aht  = NULL;

/* ==================== OLED FUNCTIONS (UNMODIFIED) ==================== */

static int I2C_Write(unsigned char *buf, unsigned int len)
{
    int ret = i2c_master_send(etx_i2c_client_oled, buf, len);
    return ret;
}

static void SSD1306_Write(bool is_cmd, unsigned char data)
{
    unsigned char buf[2];
    buf[0] = is_cmd ? 0x00 : 0x40;
    buf[1] = data;
    I2C_Write(buf, 2);
}

static void SSD1306_DisplayInit(void)
{
    msleep(100);
    SSD1306_Write(true, 0xAE);
    SSD1306_Write(true, 0xD5);
    SSD1306_Write(true, 0x80);
    SSD1306_Write(true, 0xA8);
    SSD1306_Write(true, 0x3F);
    SSD1306_Write(true, 0xD3);
    SSD1306_Write(true, 0x00);
    SSD1306_Write(true, 0x40);
    SSD1306_Write(true, 0x8D);
    SSD1306_Write(true, 0x14);
    SSD1306_Write(true, 0x20);
    SSD1306_Write(true, 0x00);
    SSD1306_Write(true, 0xA1);
    SSD1306_Write(true, 0xC8);
    SSD1306_Write(true, 0xDA);
    SSD1306_Write(true, 0x12);
    SSD1306_Write(true, 0x81);
    SSD1306_Write(true, 0x80);
    SSD1306_Write(true, 0xD9);
    SSD1306_Write(true, 0xF1);
    SSD1306_Write(true, 0xDB);
    SSD1306_Write(true, 0x20);
    SSD1306_Write(true, 0xA4);
    SSD1306_Write(true, 0xA6);
    SSD1306_Write(true, 0x2E);
    SSD1306_Write(true, 0xAF);
}

static void SSD1306_Fill(unsigned char data)
{
    unsigned int total = 128 * 8;
    unsigned int i;

    for (i = 0; i < total; i++)
        SSD1306_Write(false, data);
}

static int etx_oled_probe(struct i2c_client *client)
{
    etx_i2c_client_oled = client;
    pr_info("ETX_OLED: Device probed successfully\n");
    SSD1306_DisplayInit();
    SSD1306_Fill(0xFF);
    return 0;
}

static void etx_oled_remove(struct i2c_client *client)
{
    SSD1306_Fill(0x00);
    pr_info("ETX_OLED: Device removed\n");
}

static const struct i2c_device_id etx_oled_id[] = {
    { SLAVE_DEVICE_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, etx_oled_id);

static struct i2c_driver etx_oled_driver = {
    .driver = {
        .name = SLAVE_DEVICE_NAME,
        .owner = THIS_MODULE,
    },
    .probe = etx_oled_probe,
    .remove = etx_oled_remove,
    .id_table = etx_oled_id,
};

static struct i2c_board_info oled_i2c_board_info = {
    I2C_BOARD_INFO(SLAVE_DEVICE_NAME, SSD1306_SLAVE_ADDR)
};

/* ==================== AHT20 SENSOR SECTION (ADDED) ==================== */

static int AHT20_Init(void)
{
    int ret;
    unsigned char cmd[3] = {0xBE, 0x08, 0x00}; // initialization command
    ret = i2c_master_send(etx_i2c_client_aht, cmd, 3);
    if (ret < 0)
        pr_err("AHT20: Initialization failed\n");
    else
        pr_info("AHT20: Initialized successfully\n");
    msleep(40);
    return ret;
}

static int AHT20_ReadData(int *temperature, int *humidity)
{
    unsigned char cmd = 0xAC;
    unsigned char data[6];
    int ret;

    cmd = 0xAC;
    unsigned char trigger_cmd[3] = {0xAC, 0x33, 0x00};
    ret = i2c_master_send(etx_i2c_client_aht, trigger_cmd, 3);
    if (ret < 0) {
        pr_err("AHT20: Trigger measurement failed\n");
        return ret;
    }

    msleep(80);
    ret = i2c_master_recv(etx_i2c_client_aht, data, 6);
    if (ret < 0) {
        pr_err("AHT20: Data read failed\n");
        return ret;
    }

    u32 raw_humidity = ((data[1] << 12) | (data[2] << 4) | (data[3] >> 4));
    u32 raw_temperature = (((data[3] & 0x0F) << 16) | (data[4] << 8) | data[5]);

    *humidity = (raw_humidity * 1000) / 1048576;
    *temperature = ((raw_temperature * 2000) / 1048576) - 500;

    pr_info("AHT20: Temp = %d.%d°C, RH = %d.%d%%\n",
            *temperature / 10, *temperature % 10,
            *humidity / 10, *humidity % 10);

    return 0;
}

static int aht20_probe(struct i2c_client *client)
{
    int temp, hum;
    etx_i2c_client_aht = client;
    pr_info("AHT20: Device probed successfully\n");
    AHT20_Init();
    AHT20_ReadData(&temp, &hum);
    return 0;
}

static void aht20_remove(struct i2c_client *client)
{
    pr_info("AHT20: Device removed\n");
}

static const struct i2c_device_id aht20_id[] = {
    { AHT20_DEVICE_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, aht20_id);

static struct i2c_driver aht20_driver = {
    .driver = {
        .name = AHT20_DEVICE_NAME,
        .owner = THIS_MODULE,
    },
    .probe = aht20_probe,
    .remove = aht20_remove,
    .id_table = aht20_id,
};

static struct i2c_board_info aht20_i2c_board_info = {
    I2C_BOARD_INFO(AHT20_DEVICE_NAME, AHT20_SLAVE_ADDR)
};

/* ==================== MODULE INIT / EXIT ==================== */

static int select_device = 0;
module_param(select_device, int, 0444);
MODULE_PARM_DESC(select_device, "Select I2C device: 0=Both, 1=OLED, 2=AHT20");

static int __init etx_driver_init(void)
{
    struct i2c_client *client_oled;
    struct i2c_client *client_aht;

    pr_info("ETX_I2C: Module init started (select_device = %d)\n", select_device);
    
    /* Print kernel message for user-selected device */
    printk(KERN_INFO "ETX_I2C: Selected device = %d\n", select_device);

    etx_i2c_adapter = i2c_get_adapter(I2C_BUS_AVAILABLE);
    if (!etx_i2c_adapter) {
        pr_err("ETX_I2C: Cannot get I2C adapter %d\n", I2C_BUS_AVAILABLE);
        return -ENODEV;
    }

    /* Initialize OLED if selected or both */
    if (select_device == 0 || select_device == 1) {
        client_oled = i2c_new_client_device(etx_i2c_adapter, &oled_i2c_board_info);
        if (IS_ERR(client_oled)) {
            pr_err("ETX_OLED: Failed to register OLED device\n");
        } else {
            etx_i2c_client_oled = client_oled;
            i2c_add_driver(&etx_oled_driver);
            pr_info("ETX_OLED: Driver added\n");
        }
    }

    /* Initialize AHT20 if selected or both */
    if (select_device == 0 || select_device == 2) {
        client_aht = i2c_new_client_device(etx_i2c_adapter, &aht20_i2c_board_info);
        if (IS_ERR(client_aht)) {
            pr_err("AHT20: Failed to register AHT20 device\n");
        } else {
            etx_i2c_client_aht = client_aht;
            i2c_add_driver(&aht20_driver);
            pr_info("AHT20: Driver added\n");
        }
    }
    
    i2c_put_adapter(etx_i2c_adapter);
    printk(KERN_ALERT "ETX_I2C: Module loaded and visible on console!\n");
    return 0;
}

static void __exit etx_driver_exit(void)
{
    if (etx_i2c_client_aht) {
        i2c_unregister_device(etx_i2c_client_aht);
        i2c_del_driver(&aht20_driver);
        pr_info("AHT20: Driver removed\n");
    }

    i2c_unregister_device(etx_i2c_client_oled);
    i2c_del_driver(&etx_oled_driver);
    pr_info("ETX_OLED: Driver removed\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX + AHT20 integration by Murugesan");
MODULE_DESCRIPTION("SSD1306 OLED + AHT20 I2C Driver for Raspberry Pi Kernel 6.12.25+rpt-rpi-v8");
MODULE_VERSION("1.50");
