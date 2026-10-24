#ifndef __DS18B20_H
#define __DS18B20_H

#include "main.h"
#include "gpio.h"

/* 硬件接线:
 *   PA0  -> DQ
 *   VDD  -> 3.3V (也可外部供电)
 *   GND  -> GND
 *   建议在 DQ 与 VDD 之间接 4.7k 上拉电阻
 */

#define DS18B20_DQ_Pin        GPIO_PIN_0
#define DS18B20_DQ_GPIO_Port  GPIOA

/* -------- DQ 电平控制宏（开漏输出，可读取总线电平） -------- */
#define DS18B20_DQ_HIGH()  HAL_GPIO_WritePin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin, GPIO_PIN_SET)
#define DS18B20_DQ_LOW()   HAL_GPIO_WritePin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin, GPIO_PIN_RESET)
#define DS18B20_DQ_READ()  HAL_GPIO_ReadPin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin)

/* -------- 对外接口 -------- */
void DS18B20_Init(void);          /* 初始化 PA0（开漏输出） */
uint8_t DS18B20_GetTemp(float *temp);  /* 读取温度(℃)，成功返回 0，失败返回非 0 */

#endif /* __DS18B20_H */