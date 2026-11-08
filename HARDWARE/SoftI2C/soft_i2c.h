#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#include "main.h"

/* 软件模拟 I2C（标准模式，约 100kHz）
 * 接线：PB8 -> SCL，PB9 -> SDA
 *
 * 本总线由 AHT21（0x70）和 ADXL345（0x53/0x1D）共用。
 * 两者都是单主机、不同从机地址，只要不在同一时刻访问即可。
 * 时序与原 AHT21 驱动一致（半周期 5us），32MHz 主频下实测可用。
 */

#define SOFT_I2C_SCL_Pin        GPIO_PIN_8
#define SOFT_I2C_SCL_GPIO_Port  GPIOB
#define SOFT_I2C_SDA_Pin        GPIO_PIN_9
#define SOFT_I2C_SDA_GPIO_Port  GPIOB

void    SoftI2C_Init(void);                 /* 配置 PB8/PB9 并释放总线（可重复调用） */

void    SoftI2C_Start(void);
void    SoftI2C_Stop(void);
uint8_t SoftI2C_WriteByte(uint8_t dat);     /* 返回 0 = 收到 ACK */
uint8_t SoftI2C_ReadByte(uint8_t ack);      /* ack=1 回 ACK，0 回 NACK */

/* 进 STOP 前调用：把 SCL/SDA 停在静态高电平，避免通过上拉电阻漏电 */
void    SoftI2C_ReleaseBus(void);

/* 寄存器读写。dev_addr 传 8 位写地址（如 AHT21 0x70、ADXL345 0xA6）。
 * 需要自动递增的从机（ADXL345）由调用者在 reg 里自行或上 0x80。 */
uint8_t SoftI2C_WriteReg(uint8_t dev_addr, uint8_t reg, uint8_t val);
uint8_t SoftI2C_ReadRegs(uint8_t dev_addr, uint8_t reg,
                         uint8_t *buf, uint8_t len);

#endif /* __SOFT_I2C_H */
