#ifndef __APP_IMU_H
#define __APP_IMU_H

#include "main.h"
#include "adxl345.h"

/* 动作亮屏（ADXL345）
 *
 * 两种判定方式，用 APP_IMU_WAKE_MODE 选择：
 *   APP_IMU_MODE_SHAKE : 放在桌上，拿起来晃一下 -> 亮屏（默认）
 *   APP_IMU_MODE_WRIST : 戴在手上，抬腕看表     -> 亮屏
 *
 * 共同流程：
 *   熄屏 -> ADXL345 降到 25Hz 低功耗，开启“活动”中断（INT2 -> PB11）
 *   -> 有动作时 INT2 上升沿把 MCU 从 STOP 唤醒
 *   -> 切到 200Hz 连续采样，按选定方式判定
 *   -> 是：点亮屏幕；否：回去继续睡
 */

/* ---------------- 唤醒方式 ---------------- */
#define APP_IMU_MODE_SHAKE   1     /* 拿起晃动 */
#define APP_IMU_MODE_WRIST   2     /* 抬腕看表 */

#ifndef APP_IMU_WAKE_MODE
#define APP_IMU_WAKE_MODE    APP_IMU_MODE_SHAKE
#endif

/* ------- 晃动亮屏参数（MODE_SHAKE） -------
 * 说明：只数“合加速度幅值峰值”会漏掉纯转动的晃动（转动时 |a| 始终≈1g），
 * 所以同时统计三轴读数的逐帧变化量。平移和转动都能触发。
 *
 * 判定（满足任一即可）：
 *   1. 逐帧变化 > MOTION_DELTA 的采样点累计到 MOTION_SAMPLES 个
 *      （相当于持续约 60~80ms 的明显动作）；
 *   2. 冲高 > HI 的次数达到 PEAKS（中间要回落到 LO 以下）；
 *   3. 单次幅值 > STRONG。
 *
 * 灵敏度：拿起来晃不亮 -> 调小 MOTION_DELTA / MOTION_SAMPLES；
 *         误亮多 -> 调大。 */
#define APP_IMU_MOTION_DELTA_MG  35.0f    /* 相邻两帧三轴差值的合模长（mg） */
#define APP_IMU_MOTION_SAMPLES   10       /* 累计多少个“在动”的采样点算一次晃动 */
#define APP_IMU_SHAKE_HI_MG      1300.0f  /* 超过它算一次冲击 */
#define APP_IMU_SHAKE_LO_MG      1080.0f  /* 回落到它以下算峰结束，也用来判断“还在动” */
#define APP_IMU_SHAKE_PEAKS      2        /* 需要几次冲击 */
#define APP_IMU_SHAKE_STRONG_MG  2200.0f  /* 单次超过这么大也直接认 */

/* 判定窗口：只要还在动就一直扫，安静下来才提前结束 */
#define APP_IMU_MIN_SCAN_MS      300UL    /* 至少扫这么久（给动作留出时间） */
#define APP_IMU_SHAKE_IDLE_MS    500UL    /* 连续这么久没有明显动作就认为动作结束 */

/* ------- 抬腕亮屏参数（MODE_WRIST） ------- */
#define APP_IMU_FACE_TH          0.70f    /* 与表盘法线夹角 < 45° 才算“朝上” */
#define APP_IMU_PROGRESS_TH      0.45f    /* 相比起始姿态至少要转动这么多 */
#define APP_IMU_CONFIRM_MS       60UL     /* 进入目标姿态后需要保持的时间 */

/* ---------------- 姿态标定（只对 MODE_WRIST 有意义） ----------------
 * 把表戴在手上，摆成平时“看时间”的姿势（表盘朝上、朝向眼睛），
 * 把 APP_IMU_DEBUG_SHOW 改成 1 重新编译，OLED 上会显示三轴重力值（mg）。
 *
 * 找到数值最大的那个轴：
 *   APP_IMU_GRAV_AXIS = 0 / 1 / 2  对应 X / Y / Z
 *   APP_IMU_GRAV_SIGN = 读数为负填 -1，读数为正填 +1
 * 即：满足 GRAV_SIGN × 该轴读数 > 0 的那个轴，就是“表盘朝上”时重力所在方向。
 *
 * 下面按本板实测标定：表盘朝上时 Z 轴约 +1g，故 AXIS=2、SIGN=+1。
 */
#define APP_IMU_GRAV_AXIS     2
#define APP_IMU_GRAV_SIGN     (+1)

/* ---------------- 采样与判定参数 ---------------- */
#define APP_IMU_IDLE_RATE_CODE   ADXL345_RATE_25HZ    /* 熄屏等待时的采样率 */
#define APP_IMU_SCAN_RATE_CODE   ADXL345_RATE_200HZ   /* 判定时的采样率 */
#define APP_IMU_SETTLE_MS        12UL     /* 切到高速率后等第一帧新数据 */
#define APP_IMU_SAMPLE_MS        5UL      /* 判定时的采样周期 */
#define APP_IMU_SCAN_MS          1500UL   /* 最长判定窗口（还在动时可一直延长到上限） */

/* 1 = 在屏幕上显示加速度调试信息；正常使用时保持 0 */
#define APP_IMU_DEBUG_SHOW       0

/* ---------------- 对外接口 ---------------- */
void    AppIMU_Init(void);                     /* 探测 ADXL345 并配置 INT2 */
uint8_t AppIMU_IsPresent(void);                /* 1 = 加速度计正常 */

void    AppIMU_PrepareSleep(void);             /* 进 STOP 前：降速 + 开活动中断 */
void    AppIMU_ScreenOn(void);                 /* 亮屏期间：关活动中断省电 */

/* 从 STOP 被 INT2 唤醒后调用：返回 1 = 判定为“要亮屏”的动作 */
uint8_t AppIMU_DetectWake(void);

/* 调试：最近一次采样的三轴重力（mg） */
void    AppIMU_GetGravity(float *gx, float *gy, float *gz);

/* -------- 排查用 -------- */
void     AppIMU_OnActivityIrq(void);   /* 由 INT2 的 EXTI 中断调用，只做计数 */
uint8_t  AppIMU_DevId(void);           /* 探测读到的 DEVID（正常 0xE5），排查用 */
float    AppIMU_LastScore(void);       /* 最近一帧的“表盘朝上”投影（+1 = 正朝上） */
float    AppIMU_LastPeakMg(void);      /* 最近一次判定窗口内的合加速度峰值（mg） */
float    AppIMU_LastDeltaMg(void);     /* 最近一次判定窗口内的最大逐帧变化（mg） */
uint16_t AppIMU_ActivityCount(void);   /* 累计收到的活动中断次数 */
uint16_t AppIMU_RaiseCount(void);      /* 累计判定成功的次数 */

#if (APP_IMU_DEBUG_SHOW != 0)
/* 标定用：读一帧当前加速度并更新缓存（由界面在重绘前调用） */
void    AppIMU_DebugSample(void);
#endif

#endif /* __APP_IMU_H */
