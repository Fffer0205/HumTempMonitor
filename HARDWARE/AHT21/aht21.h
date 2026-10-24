#ifndef __AHT21_H
#define __AHT21_H

#include "main.h"
#include "gpio.h"

/* 硬件接线:
 *   PB8  -> SCL
 *   PB9  -> SDA
 *   VDD  -> 3.3V
 *   GND  -> GND
 */

#define AHT21_SCL_Pin        GPIO_PIN_8
#define AHT21_SCL_GPIO_Port  GPIOB
#define AHT21_SDA_Pin        GPIO_PIN_9
#define AHT21_SDA_GPIO_Port  GPIOB

/* I2C 从机地址（7bit），写地址 0x70 / 读地址 0x71 */
#define AHT21_ADDR_W         0x70
#define AHT21_ADDR_R         0x71

/* -------- 软件 I2C 电平控制宏 -------- */
#define AHT21_SCL_SET()  HAL_GPIO_WritePin(AHT21_SCL_GPIO_Port, AHT21_SCL_Pin, GPIO_PIN_SET)
#define AHT21_SCL_RESET() HAL_GPIO_WritePin(AHT21_SCL_GPIO_Port, AHT21_SCL_Pin, GPIO_PIN_RESET)

#define AHT21_SDA_SET()  HAL_GPIO_WritePin(AHT21_SDA_GPIO_Port, AHT21_SDA_Pin, GPIO_PIN_SET)
#define AHT21_SDA_RESET() HAL_GPIO_WritePin(AHT21_SDA_GPIO_Port, AHT21_SDA_Pin, GPIO_PIN_RESET)

/* SDA 读取：主机读取数据时需要读引脚电平 */
#define AHT21_SDA_READ() HAL_GPIO_ReadPin(AHT21_SDA_GPIO_Port, AHT21_SDA_Pin)

/* SDA 方向切换：AHT21 需要主机读数据，读时切换为输入，写时切换为输出 */
#define AHT21_SDA_IN() \
    do { \
        GPIO_InitTypeDef GPIO_InitStruct = {0}; \
        GPIO_InitStruct.Pin = AHT21_SDA_Pin; \
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT; \
        GPIO_InitStruct.Pull = GPIO_PULLUP; \
        HAL_GPIO_Init(AHT21_SDA_GPIO_Port, &GPIO_InitStruct); \
    } while(0)

#define AHT21_SDA_OUT() \
    do { \
        GPIO_InitTypeDef GPIO_InitStruct = {0}; \
        GPIO_InitStruct.Pin = AHT21_SDA_Pin; \
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; \
        GPIO_InitStruct.Pull = GPIO_NOPULL; \
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; \
        HAL_GPIO_Init(AHT21_SDA_GPIO_Port, &GPIO_InitStruct); \
    } while(0)

/* 延时（微秒级，用于 I2C 时序） */
#define AHT21_DelayUs(us)  aht21_delay_us(us)

/* -------- 对外接口 -------- */
void AHT21_Init(void);                       /* 初始化 GPIO 并复位/初始化传感器     */
uint8_t AHT21_ReadData(float *humidity,
                       float *temperature);  /* 读取温湿度，成功返回 0，失败返回非 0 */

#endif /* __AHT21_H */