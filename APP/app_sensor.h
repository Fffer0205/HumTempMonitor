#ifndef __APP_SENSOR_H
#define __APP_SENSOR_H

#include "main.h"

/* 传感器调度
 * - AHT21 每 2s 采一次（单次测量约 80ms）
 * - DS18B20 用非阻塞方式：先启动转换，转换完成后再读结果
 * - 电池电压每 30s 采一次，ADC 只在测量期间上电
 * 只在亮屏期间调用 AppSensor_Update()，熄屏后完全停止采样。
 */

/* 采样周期 */
#define APP_AHT_PERIOD_MS    2000UL
#define APP_DS_PERIOD_MS     5000UL
#define APP_BAT_PERIOD_MS    30000UL

/* DS18B20 配置为 9bit 分辨率（0.5℃），转换时间 93.75ms，取 100ms 余量 */
#define APP_DS_CONV_MS       100UL

typedef struct
{
  float   aht_t;      /* AHT21 温度（℃）   */
  float   aht_h;      /* AHT21 湿度（%RH）  */
  float   ds_t;       /* DS18B20 温度（℃）  */
  float   vbat;       /* 电池电压（V）      */
  uint8_t aht_ok;     /* 是否已成功读到过 AHT21 数据 */
  uint8_t ds_ok;      /* 是否已成功读到过 DS18B20 数据 */
  uint8_t bat_ok;     /* 电池读数是否有效 */
  uint8_t bat_pct;    /* 剩余电量 0~100 */
} AppSensor_t;

void AppSensor_Init(void);                    /* 初始化三路传感器 */
void AppSensor_Update(uint32_t now_ms);       /* 到点采样（非阻塞） */
void AppSensor_ForceSample(uint32_t now_ms);  /* 让下一次 Update 立即全部采样 */
const AppSensor_t *AppSensor_Get(void);       /* 取最近一次采样结果 */

/* 显示用平均温度：两者都有取平均，只有一路时用另一路 */
float AppSensor_AverageTemp(const AppSensor_t *s);

#endif /* __APP_SENSOR_H */
