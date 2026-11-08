#ifndef __AHT21_H
#define __AHT21_H

#include "main.h"
#include "gpio.h"

/* 硬件接线:
 *   PB8  -> SCL   （与 ADXL345 共用，底层见 soft_i2c.h）
 *   PB9  -> SDA
 *   VDD  -> 3.3V
 *   GND  -> GND
 */

/* I2C 从机地址（8bit 写地址），写 0x70 / 读 0x71 */
#define AHT21_ADDR_W         0x70
#define AHT21_ADDR_R         0x71

/* -------- 对外接口 -------- */
void AHT21_Init(void);                       /* 初始化 GPIO 并复位/初始化传感器     */
uint8_t AHT21_ReadData(float *humidity,
                       float *temperature);  /* 读取温湿度，成功返回 0，失败返回非 0 */

#endif /* __AHT21_H */
