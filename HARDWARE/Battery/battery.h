#ifndef __BATTERY_H
#define __BATTERY_H

#include "main.h"
#include "gpio.h"

/* 电池检测引脚：PA1 = ADC_IN1 */
#define BAT_ADC_GPIO_Port   GPIOA
#define BAT_ADC_Pin         GPIO_PIN_1
#define BAT_ADC_CHSEL       ADC_CHSELR_CHSEL1   /* ADC_IN1 通道选择位 */

/* 分压电路：R1=R2=100k，取中点，ADC 读数 = Vbat / 2，故实际电压乘 2 */
#define BAT_DIVIDER_RATIO   2.0f

/* ADC 参考电压（本板 VDDA = 3.3V），单位 mV；12 位分辨率满量程 */
#define BAT_ADC_REF_MV      3300UL
#define BAT_ADC_FULL_SCALE  4095UL

/* 每次测量取的采样数，以及单个等待步骤的超时时间（毫秒）。
 * 校准比单次转换慢得多，这里留足余量。 */
#define BAT_ADC_SAMPLES         4
#define BAT_ADC_TIMEOUT_MS      20UL

/* ---------------- 调试 / 兼容开关 ---------------- */

/* 1 = 测量前给 ADC 上电、测完掉电（省电，推荐）
 * 0 = ADC 常开（和改造前完全一致，用来判断"是不是上电时序的问题"） */
#define BAT_ADC_POWER_CYCLE     1

/* 1 = 界面底部改为显示 ADC 原始采样值（0~4095），用来判断是
 *     "PA1 上没有电压" 还是 "ADC 根本没工作"。定位完记得改回 0 */
#define BAT_DEBUG_SHOW_RAW      0

/* 诊断计数：可以在 Keil 的 Watch 窗口直接观察
 *   g_bat_diag.raw_last  —— 最近一次原始采样平均值（有电池时应约 1500~2600）
 *   g_bat_diag.up_fail   —— 上电/校准失败次数（持续增长说明 ADC 没起来）
 *   g_bat_diag.conv_fail —— 转换超时次数
 *   g_bat_diag.attempts  —— 总测量次数 */
typedef struct
{
    uint32_t attempts;
    uint32_t up_fail;
    uint32_t conv_fail;
    uint16_t raw_last;
} BAT_Diag_t;

extern volatile BAT_Diag_t g_bat_diag;

/* 初始化：配置 PA1 并做一次 ADC 上电/校准自检。
 * 之后每次 BAT_ReadVoltage() 内部按 BAT_ADC_POWER_CYCLE 决定是否上电/掉电。 */
void     BAT_Init(void);         /* 初始化 ADC1 + PA1 模拟输入并校准 */
uint8_t  BAT_IsReady(void);      /* 最近一次测量是否有效：1=有效（读函数之后查询） */
uint16_t BAT_ReadRawADC(void);   /* 读取单次 ADC 原始值（0~4095，失败返回 0） */
float    BAT_ReadVoltage(void);  /* 返回电池实际电压，单位 V；读取失败时沿用上次值 */
uint8_t  BAT_PercentFromVoltage(float voltage); /* 按锂电池曲线由电压换算电量 */
uint8_t  BAT_ReadPercent(void);  /* 返回剩余电量百分比 0~100 */

#endif /* __BATTERY_H */
