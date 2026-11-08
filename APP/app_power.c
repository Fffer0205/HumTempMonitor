/******************************************************************************
 * 文件名 : app_power.c
 * 功能   : 低功耗管理（显示开关 / 按键与抬手唤醒 / STOP 模式）
 *
 * 功耗要点：
 *   1. OLED 是整机最大的耗电项（点亮 10~20mA）。熄屏时调用 OLED_Display_Off()，
 *      它会把电荷泵一起关掉，只剩 µA 级。
 *   2. 熄屏后 MCU 进入 STOP，电流降到 µA 级；DS1302 继续走时，时间不会丢。
 *   3. STOP 的唤醒源：KEY1/KEY2（下降沿）和 ADXL345 INT2（上升沿）。
 *
 * 注意：STOP 期间 SysTick 停止，HAL_GetTick() 不会增长。本模块只在唤醒后的
 *       运行期使用 tick 计时，不受影响。
 ******************************************************************************/
#include "app_power.h"
#include "oled.h"
#include "oled_gfx.h"
#include "soft_i2c.h"
#include "adxl345.h"
#include "app_imu.h"
#include "ds18b20.h"
#include "ds1302.h"

static uint8_t  s_display_on;         /* 1 = 屏幕点亮 */
static uint32_t s_last_activity;      /* 最近一次按键时刻（熄屏计时基准） */
static uint8_t  s_key1_last;          /* 上一次按键状态（1=按下） */
static uint8_t  s_key2_last;
static volatile uint8_t s_key_wake_flag;  /* EXTI 里置位，主循环消费 */
static volatile uint8_t s_imu_wake_flag;

/* 按键低电平有效 */
static uint8_t key_level(uint16_t pin)
{
  return (HAL_GPIO_ReadPin(GPIOA, pin) == GPIO_PIN_RESET) ? 1 : 0;
}

/* CoreDebug->DHCSR（0xE000EDF0）的 bit0 = C_DEBUGEN，调试器连接/内核被控制时为 1。
 * 本工程使用的 CMSIS 版本没有导出 CoreDebug 结构体，这里直接读寄存器地址。 */
static uint8_t debugger_attached(void)
{
  return (uint8_t)((*(volatile uint32_t *)0xE000EDF0UL & 0x1UL) != 0UL);
}

/* 按键 / 加速度计中断：只负责清标志并把 MCU 从 STOP 叫醒
 * 注意：如果以后用 CubeMX 把 PA9/PA10/PB11 配成 GPIO_EXTI，CubeMX 会在
 * stm32l0xx_it.c 里生成同名处理函数，届时删掉这里即可。 */
void EXTI4_15_IRQHandler(void)
{
  if (__HAL_GPIO_EXTI_GET_IT(KEY1_Pin) != 0U)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(KEY1_Pin);
    s_key_wake_flag = 1;
  }
  if (__HAL_GPIO_EXTI_GET_IT(KEY2_Pin) != 0U)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(KEY2_Pin);
    s_key_wake_flag = 1;
  }
  if (__HAL_GPIO_EXTI_GET_IT(ADXL345_INT_Pin) != 0U)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(ADXL345_INT_Pin);
    s_imu_wake_flag = 1;
    AppIMU_OnActivityIrq();      /* 只做计数，供标定页观察 */
  }
}

/* 按键改为下降沿中断（同时保留上拉，松开时为高电平）。
 * 加速度计 INT2 的 EXTI 引脚配置在 AppIMU_Init() 里完成。 */
static void keys_exti_init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin  = KEY1_Pin | KEY2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* STOP 模式需要 PWR 接口时钟才能配置低功耗稳压器 */
  __HAL_RCC_PWR_CLK_ENABLE();

  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

void AppPower_Init(void)
{
  keys_exti_init();

  s_display_on    = 0;
  s_last_activity = HAL_GetTick();
  s_key_wake_flag = 0;
  s_imu_wake_flag = 0;
  s_key1_last     = key_level(KEY1_Pin);
  s_key2_last     = key_level(KEY2_Pin);
}

uint8_t AppPower_IsDisplayOn(void)
{
  return s_display_on;
}

void AppPower_DisplayOn(void)
{
  if (s_display_on)
    return;

  OLED_Display_On();     /* 重新打开电荷泵 + 显示 */
  GFX_Invalidate();      /* 屏上内容已不可信，下次刷屏全量重写 */
  s_display_on    = 1;
  s_last_activity = HAL_GetTick();
}

void AppPower_DisplayOff(void)
{
  if (!s_display_on)
    return;

  OLED_Display_Off();    /* 关显示 + 关电荷泵，掉到 µA 级 */
  s_display_on = 0;
}

uint8_t AppPower_IdleExpired(uint32_t now_ms)
{
#if (APP_DISPLAY_AUTO_OFF == 0)
  (void)now_ms;
  (void)s_last_activity;
  return 0;
#else
  if (!s_display_on)
    return 0;

  return (uint8_t)((uint32_t)(now_ms - s_last_activity) >= APP_DISPLAY_IDLE_MS);
#endif
}

uint8_t AppPower_DebuggerAttached(void)
{
  return debugger_attached();
}

uint8_t AppPower_PollWakeKey(void)
{
  uint8_t k1 = key_level(KEY1_Pin);
  uint8_t k2 = key_level(KEY2_Pin);

  if (s_key_wake_flag || k1 || k2)
  {
    s_key_wake_flag = 0;
    s_key1_last = k1;
    s_key2_last = k2;
    return 1;
  }

  return 0;
}

uint8_t AppPower_PollKeys(uint32_t now_ms)
{
  uint8_t k1 = key_level(KEY1_Pin);
  uint8_t k2 = key_level(KEY2_Pin);
  uint8_t events = 0;

  /* 熄屏状态下的任意按键：只负责点亮屏幕，这一下不算“操作” */
  if (!s_display_on && (s_key_wake_flag || k1 || k2))
  {
    s_key_wake_flag = 0;
    s_key1_last = k1;
    s_key2_last = k2;
    AppPower_DisplayOn();
    return 0;
  }

  if (k1 && !s_key1_last)
  {
    events |= APP_KEY1_EVENT;
    s_last_activity = now_ms;
  }
  if (k2 && !s_key2_last)
  {
    events |= APP_KEY2_EVENT;
    s_last_activity = now_ms;
  }

  s_key1_last = k1;
  s_key2_last = k2;

  return events;
}

uint8_t AppPower_EnterStop(void)
{
  uint8_t src = APP_WAKE_NONE;

  /* 1. 让各条共用总线回到静态高电平，避免通过上拉电阻持续漏电 */
  SoftI2C_ReleaseBus();               /* AHT21 / ADXL345 共用的 PB8/PB9 */

  DS18B20_DQ_HIGH();                  /* 开漏，等于释放总线 */

  DS1302_RST_RESET();
  DS1302_SCLK_RESET();
  DS1302_IO_SET();                    /* 先置位再切输出，避免毛刺 */
  DS1302_IO_OUT();

  /* 2. 清掉可能残留的 EXTI 挂起标志，否则会立刻退出 STOP */
  __HAL_GPIO_EXTI_CLEAR_IT(KEY1_Pin | KEY2_Pin | ADXL345_INT_Pin);
  s_key_wake_flag = 0;
  s_imu_wake_flag = 0;

  /* 3. 进入 STOP，按键与加速度计活动中断都能唤醒 */
  HAL_SuspendTick();
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

  /* 4. 唤醒后恢复系统时钟：STOP 期间 PLL 被关闭，不重配就会留在 MSI 2MHz。
   *    SystemClock_Config() 内部的 HAL_InitTick() 会重新配置 SysTick。 */
  SystemClock_Config();
  HAL_ResumeTick();

  /* 5. 记录唤醒源。按键按着不放也算按键唤醒（优先于抬手） */
  s_key1_last = key_level(KEY1_Pin);
  s_key2_last = key_level(KEY2_Pin);

  if (s_key_wake_flag || s_key1_last || s_key2_last)
    src |= APP_WAKE_KEY;
  if (s_imu_wake_flag)
    src |= APP_WAKE_IMU;

  s_key_wake_flag = 0;
  s_imu_wake_flag = 0;

  return src;
}
