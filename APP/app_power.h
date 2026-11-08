#ifndef __APP_POWER_H
#define __APP_POWER_H

#include "main.h"

/* 低功耗管理：显示开关、按键/抬手唤醒、STOP 模式
 *
 * 策略：亮屏时正常跑；15 秒无按键自动熄屏，然后进入 STOP。
 *       熄屏期间 DS1302 照常走时，不采样、不刷屏。
 *       STOP 的唤醒源有两个：
 *         - KEY1/KEY2 下降沿
 *         - ADXL345 活动中断 INT2 上升沿（抬手检测的第一道门）
 *       被动作叫醒后，不一定真的亮屏：还要经过 AppIMU_DetectWake()
 *       确认是“拿起晃动/抬腕”才点亮，否则马上回去继续睡。 */

#define APP_KEY1_EVENT      0x01u     /* KEY1 按下沿 */
#define APP_KEY2_EVENT      0x02u     /* KEY2 按下沿 */

/* AppPower_EnterStop() 的返回值：本次是被谁叫醒的 */
#define APP_WAKE_NONE       0x00u
#define APP_WAKE_KEY        0x01u
#define APP_WAKE_IMU        0x02u

/* 自动熄屏开关：1 = 无按键 15 秒后熄屏并进入 STOP（电池供电推荐）；
 *               0 = 屏幕常亮、永不睡眠（只适合插电使用，约 20mA）。 */
#define APP_DISPLAY_AUTO_OFF    1
#define APP_DISPLAY_IDLE_MS     15000UL   /* 无按键多久自动熄屏 */

/* 调试器连接时是否仍然进入 STOP。
 *   0（默认）：连着调试器时不进 STOP，避免 SWD 掉线、连不上、无法再下载；
 *             此时主循环走“台架模式”，不睡觉、轮询抬手，方便验证算法。
 *   1：无论是否在调试都照常睡眠。
 * 实测电池电流时把调试器拔掉即可，不必改这个宏。 */
#define APP_STOP_WHILE_DEBUGGED 0

void    AppPower_Init(void);                  /* 配置按键 EXTI 与初始状态 */
uint8_t AppPower_PollKeys(uint32_t now_ms);   /* 读按键，返回事件位；同时处理唤醒 */

/* 熄屏期间检查是否有按键唤醒请求：有则吃掉这次按下并返回 1。
 * 台架模式（不睡、轮询）下用它，保证按键依然能点亮屏幕。 */
uint8_t AppPower_PollWakeKey(void);
uint8_t AppPower_IsDisplayOn(void);
void    AppPower_DisplayOn(void);
void    AppPower_DisplayOff(void);
uint8_t AppPower_IdleExpired(uint32_t now_ms);/* 亮屏超时（该熄屏了）返回 1 */

/* 1 = 调试器在线（此时进 STOP 会让 SWD 掉线）。主循环据此决定是否
 * 用“不睡觉、轮询抬手”的台架模式，方便带调试器验证算法。 */
uint8_t AppPower_DebuggerAttached(void);

/* 进入 STOP，直到有唤醒源。返回 APP_WAKE_xxx 位掩码。
 * 返回时系统时钟已经恢复，但屏幕仍保持熄灭——是否亮屏由调用者决定。 */
uint8_t AppPower_EnterStop(void);

#endif /* __APP_POWER_H */
