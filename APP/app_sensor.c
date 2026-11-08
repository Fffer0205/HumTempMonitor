/******************************************************************************
 * 文件名 : app_sensor.c
 * 功能   : 传感器调度（AHT21 温湿度 / DS18B20 温度 / 电池电压）
 * 说明   : 全部按“到点才采”的方式调度，只在亮屏期间被调用；
 *          DS18B20 转换期间不阻塞，靠时间戳回来取结果。
 ******************************************************************************/
#include "app_sensor.h"
#include "aht21.h"
#include "ds18b20.h"
#include "battery.h"

static AppSensor_t s_data;

static uint32_t s_last_aht;     /* 上次 AHT21 采样时刻 */
static uint32_t s_last_ds;      /* 上次发起 DS18B20 转换的时刻 */
static uint32_t s_ds_start;     /* 本次 DS18B20 转换开始时刻 */
static uint32_t s_last_bat;     /* 上次电池采样时刻 */
static uint8_t  s_ds_busy;      /* DS18B20 正在转换中 */

void AppSensor_Init(void)
{
  s_data.aht_t = 0.0f;
  s_data.aht_h = 0.0f;
  s_data.ds_t  = 0.0f;
  s_data.vbat  = 0.0f;
  s_data.aht_ok = 0;
  s_data.ds_ok  = 0;
  s_data.bat_ok = 0;
  s_data.bat_pct = 0;

  AHT21_Init();
  DS18B20_Init();
  DS18B20_SetResolution(9);      /* 0.5℃ 足够，转换时间从 750ms 降到 94ms */
  BAT_Init();                    /* 只做一次自检，之后测量时才给 ADC 上电 */

  s_last_aht = HAL_GetTick();
  s_last_ds  = s_last_aht;
  s_last_bat = s_last_aht;
  s_ds_busy  = 0;
}

/* 把各路的“上次采样时刻”回拨一个周期，让下一轮 Update 立即采样。
 * 用无符号减法天然处理回绕，上电时 now_ms 很小也不会漏。 */
void AppSensor_ForceSample(uint32_t now_ms)
{
  s_last_aht = now_ms - APP_AHT_PERIOD_MS;
  s_last_ds  = now_ms - APP_DS_PERIOD_MS;
  s_last_bat = now_ms - APP_BAT_PERIOD_MS;
}

void AppSensor_Update(uint32_t now_ms)
{
  /* ---------- AHT21 温湿度 ---------- */
  if ((uint32_t)(now_ms - s_last_aht) >= APP_AHT_PERIOD_MS)
  {
    s_last_aht = now_ms;
    if (AHT21_ReadData(&s_data.aht_h, &s_data.aht_t) == 0)
      s_data.aht_ok = 1;
  }

  /* ---------- DS18B20 温度（非阻塞两段式） ---------- */
  if (!s_ds_busy && (uint32_t)(now_ms - s_last_ds) >= APP_DS_PERIOD_MS)
  {
    s_last_ds = now_ms;
    if (DS18B20_StartConvert() == 0)
    {
      s_ds_busy  = 1;
      s_ds_start = now_ms;
    }
  }
  if (s_ds_busy && (uint32_t)(now_ms - s_ds_start) >= APP_DS_CONV_MS)
  {
    s_ds_busy = 0;
    if (DS18B20_ReadTemp(&s_data.ds_t) == 0)
      s_data.ds_ok = 1;
  }

  /* ---------- 电池电压（ADC 只在测量期间上电） ---------- */
  if ((uint32_t)(now_ms - s_last_bat) >= APP_BAT_PERIOD_MS)
  {
    s_last_bat = now_ms;
    s_data.vbat = BAT_ReadVoltage();
    s_data.bat_ok = (uint8_t)((BAT_IsReady() != 0) && (s_data.vbat > 0.1f));
    s_data.bat_pct = s_data.bat_ok
                   ? BAT_PercentFromVoltage(s_data.vbat) : 0;
  }
}

const AppSensor_t *AppSensor_Get(void)
{
  return &s_data;
}

float AppSensor_AverageTemp(const AppSensor_t *s)
{
  if (s->aht_ok && s->ds_ok)
    return (s->aht_t + s->ds_t) * 0.5f;
  if (s->aht_ok)
    return s->aht_t;
  return s->ds_t;
}
