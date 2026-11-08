/******************************************************************************
 * 文件名 : app_imu.c
 * 功能   : 动作亮屏 —— 用 ADXL345 判断用户是否想查看屏幕
 *
 * 两种模式（APP_IMU_WAKE_MODE）：
 *
 * 1) MODE_SHAKE（默认，适合放在桌面）
 *    “拿起来晃一下”会产生两次以上的强冲击：合加速度 |a| 冲高、
 *    回落、再冲高。只要在判定窗口内数到足够多的冲击就亮屏。
 *    单纯碰一下桌面通常只有一个尖峰，会被挡掉。
 *
 * 2) MODE_WRIST（适合戴在手上）
 *    抬腕看表时前臂绕肘转约 90°，表盘法线由接近水平转到朝上。
 *    盯住“重力在表盘法线上的投影”：
 *        表盘朝上     -> score 接近 +1
 *        手腕自然下垂 -> score 接近 0
 *    要求几百毫秒内 score 相对起始值上升 0.45 以上，并稳定在
 *    0.70 的锥角内保持 60ms。
 *
 * 注意：STOP 期间 SysTick 停止，HAL_GetTick() 不走，本模块的计时
 *       全部发生在“醒着”的时间段内，因此不受影响。
 ******************************************************************************/
#include "app_imu.h"
#include <math.h>

static uint8_t s_present;            /* 1 = ADXL345 就绪 */
static float   s_gx, s_gy, s_gz;     /* 最近一次采样（mg），调试用 */
static float   s_last_score;         /* 最近一帧的“表盘朝上”投影 */
static float   s_last_peak;          /* 最近一次判定窗口内的 |a| 峰值（mg） */
static float   s_last_delta;         /* 最近一次判定窗口内的最大逐帧变化（mg） */
static volatile uint16_t s_activity_cnt;  /* INT2 中断计数 */
static volatile uint16_t s_raise_cnt;     /* 判定成功次数 */

/* 由 EXTI 中断调用：只记个数，方便在标定页上看“到底有没有被叫醒” */
void AppIMU_OnActivityIrq(void)
{
  s_activity_cnt++;
}

/* INT2 配置成上升沿外部中断，用于把 MCU 从 STOP 叫醒。
 * 优先级沿用按键里已经设好的 EXTI4_15 设置，这里只打开时钟和引脚。 */
static void imu_exti_init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* ADXL345 的 INT2 是推挽输出，不需要上下拉（避免高电平时白耗电流） */
  GPIO_InitStruct.Pin  = ADXL345_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ADXL345_INT_GPIO_Port, &GPIO_InitStruct);

  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

/* 由三轴重力计算“表盘朝上”投影，范围 -1..+1（mag = 合加速度，单位 mg）。
 * +1 = 表盘正朝上；0 = 表盘竖直；-1 = 表盘朝下。 */
static float face_score_raw(float x, float y, float z, float mag)
{
  float v;

  (void)x;                   /* 由 APP_IMU_GRAV_AXIS 选择用哪一个，其余未用 */
  (void)y;
  (void)z;

  if (mag < 100.0f)          /* 小于 0.39g：失重/异常，本次数据不可信 */
  {
    s_last_score = 0.0f;
    return 0.0f;
  }

#if   (APP_IMU_GRAV_AXIS == 0)
  v = x;
#elif (APP_IMU_GRAV_AXIS == 1)
  v = y;
#else
  v = z;
#endif

  s_last_score = (APP_IMU_GRAV_SIGN * v) / mag;
  return s_last_score;
}

void AppIMU_Init(void)
{
  s_gx = 0.0f;
  s_gy = 0.0f;
  s_gz = 0.0f;
  s_last_score = 0.0f;
  s_last_peak  = 0.0f;
  s_last_delta = 0.0f;

  s_present = (uint8_t)(ADXL345_Init() == 0);
  if (s_present)
    imu_exti_init();
}

uint8_t AppIMU_IsPresent(void)
{
  return s_present;
}

uint8_t AppIMU_DevId(void)
{
  return ADXL345_LastDevId();
}

float AppIMU_LastScore(void)
{
  return s_last_score;
}

float AppIMU_LastPeakMg(void)
{
  return s_last_peak;
}

float AppIMU_LastDeltaMg(void)
{
  return s_last_delta;
}

uint16_t AppIMU_ActivityCount(void)
{
  return s_activity_cnt;
}

uint16_t AppIMU_RaiseCount(void)
{
  return s_raise_cnt;
}

void AppIMU_PrepareSleep(void)
{
  uint8_t i;

  if (!s_present)
    return;

  /* 熄屏等待期间用最低采样率省电；活动检测本身不受影响 */
  ADXL345_SetRate(APP_IMU_IDLE_RATE_CODE, 1);
  ADXL345_ActivityEnable(1);          /* 清标志后打开活动中断 */
  __HAL_GPIO_EXTI_CLEAR_IT(ADXL345_INT_Pin);

  /* 如果 INT2 还停在高电平（上一段动作还没结束），等它回低。
   * 否则进 STOP 时没有“上升沿”，会错过下一次唤醒。 */
  for (i = 0; i < 20; i++)
  {
    if (HAL_GPIO_ReadPin(ADXL345_INT_GPIO_Port, ADXL345_INT_Pin) == GPIO_PIN_RESET)
      break;
    HAL_Delay(1);
  }
  __HAL_GPIO_EXTI_CLEAR_IT(ADXL345_INT_Pin);
}

void AppIMU_ScreenOn(void)
{
  if (!s_present)
    return;

  ADXL345_SetRate(APP_IMU_IDLE_RATE_CODE, 1);
  ADXL345_ActivityEnable(0);          /* 亮屏时不需要外部中断 */
  __HAL_GPIO_EXTI_CLEAR_IT(ADXL345_INT_Pin);
}

uint8_t AppIMU_DetectWake(void)
{
  ADXL345_Accel_t a;
  uint32_t t0;
  uint8_t  detected = 0;
  float    peak = 0.0f;

#if (APP_IMU_WAKE_MODE == APP_IMU_MODE_SHAKE)
  uint8_t  above = 0;     /* 当前是否处在“高于高门限”的状态 */
  uint8_t  peaks = 0;     /* 冲击次数 */
  uint16_t motion_cnt = 0;/* “在动”的采样点个数 */
  float    px = 0.0f, py = 0.0f, pz = 0.0f;  /* 上一帧数据 */
  uint8_t  have_prev = 0;
  float    max_delta = 0.0f;                 /* 窗口内最大逐帧变化 */
  uint32_t t_last_motion; /* 最近一次检测到明显动作的时刻 */
#else
  uint32_t t_reach = 0;
  float    c, c0 = 0.0f;
  uint8_t  have_c0 = 0;
  uint8_t  confirm = 0;
#endif

  if (!s_present)
    return 0;

  /* 判定期间临时提速，获得足够密度的姿态/冲击点 */
  ADXL345_SetRate(APP_IMU_SCAN_RATE_CODE, 0);
  HAL_Delay(APP_IMU_SETTLE_MS);

  t0 = HAL_GetTick();

#if (APP_IMU_WAKE_MODE == APP_IMU_MODE_SHAKE)
  t_last_motion = t0;
#endif

  while (1)
  {
    uint32_t now = HAL_GetTick();

    if ((uint32_t)(now - t0) >= APP_IMU_SCAN_MS)
      break;                        /* 到达窗口上限 */

#if (APP_IMU_WAKE_MODE == APP_IMU_MODE_SHAKE)
    /* 已经动过了，又安静下来：这次动作结束，提前收工。
     * 反过来，只要还在动就一直扫，避免“一次唤醒只抓半段动作”。 */
    if (((uint32_t)(now - t0) >= APP_IMU_MIN_SCAN_MS) &&
        ((uint32_t)(now - t_last_motion) >= APP_IMU_SHAKE_IDLE_MS))
      break;
#endif

    if (ADXL345_ReadXYZ(&a) == 0)
    {
      float x = (float)a.x * 3.9f;
      float y = (float)a.y * 3.9f;
      float z = (float)a.z * 3.9f;
      float mag;

      s_gx = x;
      s_gy = y;
      s_gz = z;

      mag = sqrtf(x * x + y * y + z * z);   /* 合加速度，静止时约 1000mg */
      if (mag > peak)
        peak = mag;

#if (APP_IMU_WAKE_MODE == APP_IMU_MODE_SHAKE)
      /* 逐帧变化：平移会让三轴读数整体跳变，转动会让重力方向在轴上
       * 重新分配，两种都会在这里体现出来。静止时只有几 mg 的噪声。 */
      if (have_prev)
      {
        float dx = x - px;
        float dy = y - py;
        float dz = z - pz;
        float delta = sqrtf(dx * dx + dy * dy + dz * dz);

        if (delta > max_delta)
          max_delta = delta;

        if ((delta > APP_IMU_MOTION_DELTA_MG) ||
            (mag > APP_IMU_SHAKE_HI_MG) ||
            (mag < 700.0f))
        {
          motion_cnt++;           /* 明显在动 */
          t_last_motion = now;
        }
        else if (mag > APP_IMU_SHAKE_LO_MG)
        {
          t_last_motion = now;    /* 动作还在持续，窗口别急着关 */
        }
      }
      px = x;
      py = y;
      pz = z;
      have_prev = 1;

      /* 带迟滞地数冲击峰：冲高一次算一个峰，回落后才能再计 */
      if ((!above) && (mag > APP_IMU_SHAKE_HI_MG))
      {
        above = 1;
        peaks++;
      }
      else if (above && (mag < APP_IMU_SHAKE_LO_MG))
      {
        above = 0;
      }

      if ((motion_cnt >= APP_IMU_MOTION_SAMPLES) ||
          (peaks >= APP_IMU_SHAKE_PEAKS) ||
          (peak >= APP_IMU_SHAKE_STRONG_MG))
      {
        detected = 1;
        break;
      }
#else
      c = face_score_raw(x, y, z, mag);

      if (!have_c0)
      {
        c0 = c;
        have_c0 = 1;
      }
      else if ((c - c0) >= APP_IMU_PROGRESS_TH)
      {
        if (c >= APP_IMU_FACE_TH)
        {
          if (confirm == 0)
            t_reach = HAL_GetTick();
          confirm++;

          if ((uint32_t)(HAL_GetTick() - t_reach) >= APP_IMU_CONFIRM_MS)
          {
            detected = 1;
            break;
          }
        }
        else
        {
          confirm = 0;      /* 还没到位，重新计时 */
        }
      }
      else
      {
        confirm = 0;        /* 转动幅度不够，重新计时 */
      }
#endif
    }

    HAL_Delay(APP_IMU_SAMPLE_MS);
  }

  s_last_peak = peak;
#if (APP_IMU_WAKE_MODE == APP_IMU_MODE_SHAKE)
  s_last_delta = max_delta;
#endif

  /* 判定结束：无论成败都回到熄屏待机配置 */
  ADXL345_SetRate(APP_IMU_IDLE_RATE_CODE, 1);
  ADXL345_ActivityEnable(0);

  if (detected)
    s_raise_cnt++;

  return detected;
}

void AppIMU_GetGravity(float *gx, float *gy, float *gz)
{
  if (gx) *gx = s_gx;
  if (gy) *gy = s_gy;
  if (gz) *gz = s_gz;
}

#if (APP_IMU_DEBUG_SHOW != 0)
void AppIMU_DebugSample(void)
{
  ADXL345_Accel_t a;

  if (!s_present)
    return;

  if (ADXL345_ReadXYZ(&a) == 0)
  {
    float x = (float)a.x * 3.9f;
    float y = (float)a.y * 3.9f;
    float z = (float)a.z * 3.9f;

    s_gx = x;
    s_gy = y;
    s_gz = z;
    (void)face_score_raw(x, y, z, sqrtf(x * x + y * y + z * z));
  }
}
#endif
