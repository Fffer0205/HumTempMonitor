/******************************************************************************
 * 文件名 : aht21.c
 * 功能   : AHT21 温湿度传感器软件 I2C 驱动程序
 * 接线   : PB8 -> SCL, PB9 -> SDA
 * 说明   : 使用软件模拟 I2C，兼容 STM32L0 系列 HAL 库
 ******************************************************************************/
#include "aht21.h"

/* AHT21 命令字 */
#define AHT21_CMD_SOFTRESET  0xBA   /* 软复位            */
#define AHT21_CMD_INIT       0xE1   /* 初始化（校准）    */
#define AHT21_CMD_MEASURE    0xAC   /* 触发测量          */
#define AHT21_MEAS_CTRL      0x33   /* 测量控制字0（关闭NTC）*/
#define AHT21_MEAS_CTRL2     0x00   /* 测量控制字1       */

#define AHT21_STATUS_BUSY    0x80   /* 状态字节 bit7: 忙标志 */

static uint8_t i2c_status = 0;      /* 结果状态缓存（调试用） */

/* 微秒级延时：利用空循环实现，用于 I2C 时序，可根据主频调整 */
static void aht21_delay_us(uint32_t us)
{
    volatile uint32_t i;
    for (; us > 0; us--)
    {
        for (i = 0; i < 10; i++)
        {
            __NOP();
        }
    }
}

/* I2C 起始条件 */
static void i2c_start(void)
{
    AHT21_SDA_OUT();
    AHT21_SDA_SET();
    AHT21_SCL_SET();
    AHT21_DelayUs(5);
    AHT21_SDA_RESET();
    AHT21_DelayUs(5);
    AHT21_SCL_RESET();
    AHT21_DelayUs(5);
}

/* I2C 停止条件 */
static void i2c_stop(void)
{
    AHT21_SDA_OUT();
    AHT21_SDA_RESET();
    AHT21_SCL_SET();
    AHT21_DelayUs(5);
    AHT21_SDA_SET();
    AHT21_DelayUs(5);
}

/* 主机发送一个字节，返回从机 ACK（0 表示收到 ACK） */
static uint8_t i2c_write_byte(uint8_t dat)
{
    uint8_t i, ack;

    AHT21_SDA_OUT();
    for (i = 0; i < 8; i++)
    {
        if (dat & 0x80)
            AHT21_SDA_SET();
        else
            AHT21_SDA_RESET();
        dat <<= 1;
        AHT21_DelayUs(2);
        AHT21_SCL_SET();
        AHT21_DelayUs(5);
        AHT21_SCL_RESET();
        AHT21_DelayUs(2);
    }

    /* 释放 SDA，从机回 ACK */
    AHT21_SDA_IN();
    AHT21_DelayUs(2);
    AHT21_SCL_SET();
    AHT21_DelayUs(5);
    ack = AHT21_SDA_READ();
    AHT21_SCL_RESET();
    AHT21_DelayUs(2);
    AHT21_SDA_OUT();

    return ack;   /* 返回 0 表示 ACK，1 表示 NACK */
}

/* 主机读取一个字节，ack=1 则发送 ACK，否则发送 NACK */
static uint8_t i2c_read_byte(uint8_t ack)
{
    uint8_t i, dat = 0;

    AHT21_SDA_IN();
    for (i = 0; i < 8; i++)
    {
        dat <<= 1;
        AHT21_SCL_SET();
        AHT21_DelayUs(5);
        if (AHT21_SDA_READ())
            dat |= 0x01;
        AHT21_SCL_RESET();
        AHT21_DelayUs(2);
    }

    /* 应答位 */
    AHT21_SDA_OUT();
    if (ack)
        AHT21_SDA_RESET();   /* ACK */
    else
        AHT21_SDA_SET();     /* NACK */
    AHT21_DelayUs(2);
    AHT21_SCL_SET();
    AHT21_DelayUs(5);
    AHT21_SCL_RESET();
    AHT21_SDA_OUT();

    return dat;
}

/**
 * @brief 初始化 AHT21
 *        配置 SCL/SDA 引脚，进行软复位并发送初始化命令
 */
void AHT21_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* SCL 配置为推挽输出 */
    GPIO_InitStruct.Pin = AHT21_SCL_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(AHT21_SCL_GPIO_Port, &GPIO_InitStruct);

    /* SDA 配置为推挽输出 */
    GPIO_InitStruct.Pin = AHT21_SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(AHT21_SDA_GPIO_Port, &GPIO_InitStruct);

    HAL_GPIO_WritePin(AHT21_SCL_GPIO_Port, AHT21_SCL_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(AHT21_SDA_GPIO_Port, AHT21_SDA_Pin, GPIO_PIN_SET);

    /* 软复位 */
    i2c_start();
    i2c_write_byte(AHT21_ADDR_W);
    i2c_write_byte(AHT21_CMD_SOFTRESET);
    i2c_stop();
    HAL_Delay(20);

    /* 初始化（校准） */
    i2c_start();
    i2c_write_byte(AHT21_ADDR_W);
    i2c_write_byte(AHT21_CMD_INIT);
    i2c_write_byte(0x08);
    i2c_write_byte(0x00);
    i2c_stop();
    HAL_Delay(10);
}

/**
 * @brief 触发一次温湿度测量并读取结果
 * @param humidity      相对湿度输出（单位 %RH）
 * @param temperature   温度输出（单位 ℃）
 * @retval 0：成功；1：写命令失败；2：读取超时
 */
uint8_t AHT21_ReadData(float *humidity, float *temperature)
{
    uint32_t raw_humi = 0, raw_temp = 0;
    uint8_t buf[6];
    uint8_t i, ret = 0;
    uint32_t timeout = 0;

    if ((humidity == NULL) || (temperature == NULL))
        return 1;

    /* 1. 触发测量 */
    i2c_start();
    if (i2c_write_byte(AHT21_ADDR_W))
    {
        i2c_stop();
        return 1;
    }
    i2c_write_byte(AHT21_CMD_MEASURE);
    i2c_write_byte(AHT21_MEAS_CTRL);
    i2c_write_byte(AHT21_MEAS_CTRL2);
    i2c_stop();

    /* 2. 等待测量完成：读状态字节，bit7 = 0 表示空闲 */
    timeout = 0;
    do
    {
        HAL_Delay(1);
        i2c_start();
        if (i2c_write_byte(AHT21_ADDR_R))
        {
            i2c_stop();
            return 1;
        }
        i2c_status = i2c_read_byte(0);   /* NACK 结束本次读取 */
        i2c_stop();
        if (++timeout > 100)             /* 超过 100ms */
        {
            ret = 2;
            break;
        }
    } while (i2c_status & AHT21_STATUS_BUSY);

    if (ret)
        return ret;

    /* 3. 读取 6 字节数据：状态 + 湿度20bit + 温度20bit */
    i2c_start();
    if (i2c_write_byte(AHT21_ADDR_R))
    {
        i2c_stop();
        return 1;
    }
    for (i = 0; i < 5; i++)
        buf[i] = i2c_read_byte(1);       /* ACK */
    buf[5] = i2c_read_byte(0);           /* NACK */
    i2c_stop();

    /* 4. 数据解析 */
    raw_humi = ((uint32_t)buf[1] << 12)
             | ((uint32_t)buf[2] << 4)
             | ((uint32_t)buf[3] >> 4);
    raw_temp = (((uint32_t)(buf[3] & 0x0F)) << 16)
             | ((uint32_t)buf[4] << 8)
             | (uint32_t)buf[5];

    *humidity    = ((float)raw_humi / 1048576.0f) * 100.0f;        /* 0~100 %RH  */
    *temperature = ((float)raw_temp / 1048576.0f) * 200.0f - 50.0f;/* -50~150 ℃ */

    return 0;
}