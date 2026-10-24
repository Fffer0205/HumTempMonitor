#ifndef __DS1302_H
#define __DS1302_H

#include "main.h"
#include "gpio.h"
#include <stdint.h>

/* 硬件接线:
 *   SCLK -> PA8
 *   I/O  -> PB15
 *   RST  -> PB12
 *   VCC  -> 3.3V
 *   GND  -> GND
 */

#define DS1302_SCLK_Pin        GPIO_PIN_8
#define DS1302_SCLK_GPIO_Port  GPIOA
#define DS1302_IO_Pin          GPIO_PIN_15
#define DS1302_IO_GPIO_Port    GPIOB
#define DS1302_RST_Pin         GPIO_PIN_12
#define DS1302_RST_GPIO_Port   GPIOB

/* -------- 电平控制宏 -------- */
#define DS1302_SCLK_SET()   HAL_GPIO_WritePin(DS1302_SCLK_GPIO_Port, DS1302_SCLK_Pin, GPIO_PIN_SET)
#define DS1302_SCLK_RESET() HAL_GPIO_WritePin(DS1302_SCLK_GPIO_Port, DS1302_SCLK_Pin, GPIO_PIN_RESET)

#define DS1302_IO_SET()     HAL_GPIO_WritePin(DS1302_IO_GPIO_Port, DS1302_IO_Pin, GPIO_PIN_SET)
#define DS1302_IO_RESET()   HAL_GPIO_WritePin(DS1302_IO_GPIO_Port, DS1302_IO_Pin, GPIO_PIN_RESET)
#define DS1302_IO_READ()    HAL_GPIO_ReadPin(DS1302_IO_GPIO_Port, DS1302_IO_Pin)

#define DS1302_RST_SET()    HAL_GPIO_WritePin(DS1302_RST_GPIO_Port, DS1302_RST_Pin, GPIO_PIN_SET)
#define DS1302_RST_RESET()  HAL_GPIO_WritePin(DS1302_RST_GPIO_Port, DS1302_RST_Pin, GPIO_PIN_RESET)

/* I/O 方向切换：写数据时为输出，读数据时为输入 */
#define DS1302_IO_IN() \
    do { \
        GPIO_InitTypeDef GPIO_InitStruct = {0}; \
        GPIO_InitStruct.Pin = DS1302_IO_Pin; \
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT; \
        GPIO_InitStruct.Pull = GPIO_PULLUP; \
        HAL_GPIO_Init(DS1302_IO_GPIO_Port, &GPIO_InitStruct); \
    } while(0)

#define DS1302_IO_OUT() \
    do { \
        GPIO_InitTypeDef GPIO_InitStruct = {0}; \
        GPIO_InitStruct.Pin = DS1302_IO_Pin; \
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; \
        GPIO_InitStruct.Pull = GPIO_NOPULL; \
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; \
        HAL_GPIO_Init(DS1302_IO_GPIO_Port, &GPIO_InitStruct); \
    } while(0)

#define DS1302_DelayUs(us)  ds1302_delay_us(us)

/* 时间结构体（所有字段为十进制数值） */
typedef struct {
    uint8_t year;    /* 00 ~ 99 */
    uint8_t month;   /* 01 ~ 12 */
    uint8_t day;     /* 01 ~ 31 */
    uint8_t week;    /* 1 ~ 7 (1=周日, 7=周六) */
    uint8_t hour;    /* 00 ~ 23 */
    uint8_t minute;  /* 00 ~ 59 */
    uint8_t second;  /* 00 ~ 59 */
} DS1302_Time_t;

/* -------- 对外接口 -------- */
void DS1302_Init(void);                              /* 初始化 GPIO */
void DS1302_SetTime(const DS1302_Time_t *time);      /* 设置时间 */
void DS1302_GetTime(DS1302_Time_t *time);            /* 读取时间 */
void DS1302_StartClock(void);                        /* 开启时钟（清除 CH 停止位） */

#endif /* __DS1302_H */