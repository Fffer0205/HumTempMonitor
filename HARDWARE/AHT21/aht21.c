/******************************************************************************
 * 文件名 : aht21.c
 * 功能   : AHT21 温湿度传感器驱动
 * 接线   : PB8 -> SCL, PB9 -> SDA（软件 I2C，与 ADXL345 共用）
 * 说明   : 底层时序在 HARDWARE/SoftI2C/soft_i2c.c 中统一实现
 ******************************************************************************/
#include "aht21.h"
#include "soft_i2c.h"

/* AHT21 命令字 */
#define AHT21_CMD_SOFTRESET  0xBA   /* 软复位            */
#define AHT21_CMD_INIT       0xE1   /* 初始化（校准）    */
#define AHT21_CMD_MEASURE    0xAC   /* 触发测量          */
#define AHT21_MEAS_CTRL      0x33   /* 测量控制字0（关闭NTC）*/
#define AHT21_MEAS_CTRL2     0x00   /* 测量控制字1       */

#define AHT21_STATUS_BUSY    0x80   /* 状态字节 bit7: 忙标志 */

static uint8_t i2c_status = 0;      /* 结果状态缓存（调试用） */

/**
 * @brief 初始化 AHT21
 *        配置 SCL/SDA 引脚，进行软复位并发送初始化命令
 */
void AHT21_Init(void)
{
    SoftI2C_Init();

    /* 软复位 */
    SoftI2C_Start();
    SoftI2C_WriteByte(AHT21_ADDR_W);
    SoftI2C_WriteByte(AHT21_CMD_SOFTRESET);
    SoftI2C_Stop();
    HAL_Delay(20);

    /* 初始化（校准） */
    SoftI2C_Start();
    SoftI2C_WriteByte(AHT21_ADDR_W);
    SoftI2C_WriteByte(AHT21_CMD_INIT);
    SoftI2C_WriteByte(0x08);
    SoftI2C_WriteByte(0x00);
    SoftI2C_Stop();
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
    SoftI2C_Start();
    if (SoftI2C_WriteByte(AHT21_ADDR_W))
    {
        SoftI2C_Stop();
        return 1;
    }
    SoftI2C_WriteByte(AHT21_CMD_MEASURE);
    SoftI2C_WriteByte(AHT21_MEAS_CTRL);
    SoftI2C_WriteByte(AHT21_MEAS_CTRL2);
    SoftI2C_Stop();

    /* 2. 等待测量完成：读状态字节，bit7 = 0 表示空闲 */
    timeout = 0;
    do
    {
        HAL_Delay(1);
        SoftI2C_Start();
        if (SoftI2C_WriteByte(AHT21_ADDR_R))
        {
            SoftI2C_Stop();
            return 1;
        }
        i2c_status = SoftI2C_ReadByte(0);   /* NACK 结束本次读取 */
        SoftI2C_Stop();
        if (++timeout > 100)                /* 超过 100ms */
        {
            ret = 2;
            break;
        }
    } while (i2c_status & AHT21_STATUS_BUSY);

    if (ret)
        return ret;

    /* 3. 读取 6 字节数据：状态 + 湿度20bit + 温度20bit */
    SoftI2C_Start();
    if (SoftI2C_WriteByte(AHT21_ADDR_R))
    {
        SoftI2C_Stop();
        return 1;
    }
    for (i = 0; i < 5; i++)
        buf[i] = SoftI2C_ReadByte(1);       /* ACK */
    buf[5] = SoftI2C_ReadByte(0);           /* NACK */
    SoftI2C_Stop();

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
