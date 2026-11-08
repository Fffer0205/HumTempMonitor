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
uint8_t DS18B20_SetResolution(uint8_t bits); /* 设置分辨率 9~12bit，0=成功 */

/* 12bit 分辨率转换需 750ms；9bit 只需 93.75ms。读取前请等待对应时间。 */
#define DS18B20_CONV_MS_9BIT   100
#define DS18B20_CONV_MS_12BIT  800

uint8_t DS18B20_GetTemp(float *temp);  /* 阻塞式读温度(℃)，内部等待 760ms，成功返回 0 */

/* 非阻塞分步接口：先启动转换，等待足够时间后再读取结果，期间 CPU 可以干别的。
 * 等待时间取决于分辨率：9bit 约 100ms，12bit 约 800ms。
 * 两者成功均返回 0，失败返回非 0。 */
uint8_t DS18B20_StartConvert(void);    /* 复位 + 跳过 ROM + 启动温度转换 */
uint8_t DS18B20_ReadTemp(float *temp); /* 读取上一次转换结果(℃) */

#endif /* __DS18B20_H */