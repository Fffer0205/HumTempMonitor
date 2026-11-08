#ifndef __ADXL345_H
#define __ADXL345_H

#include "main.h"

/* ADXL345BCCZ-RL7 三轴加速度计（软件 I2C）
 *
 * 接线：
 *   VDD / VDD_IO -> 3.3V
 *   GND          -> GND
 *   CS           -> 3.3V        （拉高选择 I2C 模式）
 *   SCL          -> PB8         （与 AHT21 共用总线）
 *   SDA          -> PB9         （与 AHT21 共用总线）
 *   SDO / ALT    -> GND         （地址 0x53；若接 VCC 则为 0x1D，驱动会自动探测）
 *   INT1         -> PB10        （本工程未使用：PB10 与 KEY1(PA10) 共用 EXTI10）
 *   INT2         -> PB11        （活动中断，用于把 MCU 从 STOP 唤醒）
 *
 * 量程 ±16g + FULL_RES：分辨率固定 3.9mg/LSB，快速挥动也不会削顶。
 */

/* INT2 引脚（外部中断，上升沿有效） */
#define ADXL345_INT_Pin        GPIO_PIN_11
#define ADXL345_INT_GPIO_Port  GPIOB

/* -------- 地址 -------- */
#define ADXL345_ADDR_W_GND     0xA6   /* SDO 接地：7bit 0x53 */
#define ADXL345_ADDR_W_VCC     0x3A   /* SDO 接高：7bit 0x1D */

#define ADXL345_DEVID_VALUE    0xE5

/* -------- 寄存器 -------- */
#define ADXL345_REG_DEVID        0x00
#define ADXL345_REG_THRESH_TAP   0x1D
#define ADXL345_REG_OFSX         0x1E
#define ADXL345_REG_OFSY         0x1F
#define ADXL345_REG_OFSZ         0x20
#define ADXL345_REG_DUR          0x21
#define ADXL345_REG_LATENT       0x22
#define ADXL345_REG_WINDOW       0x23
#define ADXL345_REG_THRESH_ACT   0x24
#define ADXL345_REG_THRESH_INACT 0x25
#define ADXL345_REG_TIME_INACT   0x26
#define ADXL345_REG_ACT_INACT_CTL 0x27
#define ADXL345_REG_THRESH_FF    0x28
#define ADXL345_REG_TIME_FF      0x29
#define ADXL345_REG_TAP_AXES     0x2A
#define ADXL345_REG_ACT_TAP_STATUS 0x2B
#define ADXL345_REG_BW_RATE      0x2C
#define ADXL345_REG_POWER_CTL    0x2D
#define ADXL345_REG_INT_ENABLE   0x2E
#define ADXL345_REG_INT_MAP      0x2F
#define ADXL345_REG_INT_SOURCE   0x30
#define ADXL345_REG_DATA_FORMAT  0x31
#define ADXL345_REG_DATAX0       0x32
#define ADXL345_REG_FIFO_CTL     0x38
#define ADXL345_REG_FIFO_STATUS  0x39

/* 多字节读时寄存器地址要或上 0x80 打开自动递增 */
#define ADXL345_SPI_READ         0x80

/* -------- DATA_FORMAT (0x31) -------- */
#define ADXL345_DATA_FORMAT_FULL_RES  0x08
#define ADXL345_RANGE_2G              0x00
#define ADXL345_RANGE_4G              0x01
#define ADXL345_RANGE_8G              0x02
#define ADXL345_RANGE_16G             0x03

/* -------- BW_RATE (0x2C)：低 4 位 ODR 编码，bit4 = LOW_POWER -------- */
#define ADXL345_LOW_POWER         0x10
#define ADXL345_RATE_3200HZ       0x0F
#define ADXL345_RATE_1600HZ       0x0E
#define ADXL345_RATE_800HZ        0x0D
#define ADXL345_RATE_400HZ        0x0C
#define ADXL345_RATE_200HZ        0x0B
#define ADXL345_RATE_100HZ        0x0A
#define ADXL345_RATE_50HZ         0x09
#define ADXL345_RATE_25HZ         0x08
#define ADXL345_RATE_12_5HZ       0x07

/* -------- POWER_CTL (0x2D) -------- */
#define ADXL345_MEASURE           0x08
#define ADXL345_AUTO_SLEEP        0x10
#define ADXL345_SLEEP             0x20

/* -------- INT_ENABLE / INT_MAP / INT_SOURCE 位 -------- */
#define ADXL345_INT_DATA_READY    0x80
#define ADXL345_INT_SINGLE_TAP    0x40
#define ADXL345_INT_DOUBLE_TAP    0x20
#define ADXL345_INT_ACTIVITY      0x10
#define ADXL345_INT_INACTIVITY    0x08
#define ADXL345_INT_FREE_FALL     0x04
#define ADXL345_INT_WATERMARK     0x02
#define ADXL345_INT_OVERRUN       0x01

/* -------- ACT_INACT_CTL (0x27)：活动检测用 AC 耦合，三轴全开 -------- */
#define ADXL345_ACT_AC            0x80
#define ADXL345_ACT_X             0x40
#define ADXL345_ACT_Y             0x20
#define ADXL345_ACT_Z             0x10

/* 活动阈值：1 LSB = 62.5mg。0x08 = 0.5g
 *
 * 这里用 AC 耦合（只对加速度的“变化量”比较，不受重力方向影响），
 * 所以阈值可以取得比较低。太小会频繁误唤醒（走路摆臂也会触发），
 * 太大则轻缓的抬手可能唤不醒。0.5g 是比较折中的值，可按实测调整。 */
#define ADXL345_ACT_THRESH_DEFAULT 0x08

typedef struct
{
  int16_t x;   /* 原始值，FULL_RES 下 1 LSB = 3.9mg */
  int16_t y;
  int16_t z;
} ADXL345_Accel_t;

/* -------- 对外接口 -------- */
uint8_t ADXL345_Init(void);          /* 探测 + 基本配置，返回 0 表示成功 */
uint8_t ADXL345_IsPresent(void);
uint8_t ADXL345_LastDevId(void);     /* 探测时读到的 DEVID（正常为 0xE5），排查用 */
uint8_t ADXL345_ReadXYZ(ADXL345_Accel_t *a);   /* 读三轴原始值，0 = 成功 */

void    ADXL345_SetRate(uint8_t rate_code, uint8_t low_power);
uint8_t ADXL345_ActivityEnable(uint8_t enable); /* 开/关活动中断（映射到 INT2） */
void    ADXL345_ClearInterrupt(void);          /* 读 INT_SOURCE，清挂起标志 */
uint8_t ADXL345_ReadIntSource(void);

uint8_t ADXL345_WriteReg(uint8_t reg, uint8_t val);
uint8_t ADXL345_ReadReg(uint8_t reg, uint8_t *val);

#endif /* __ADXL345_H */
