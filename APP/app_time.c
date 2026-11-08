/******************************************************************************
 * 文件名 : app_time.c
 * 功能   : 时间显示与编辑（DS1302 时钟）
 * 说明   : 按键语义沿用原实现
 *          KEY1：进入编辑 → 依次切换 年/月/日/时/分/秒 → 再按一次保存退出
 *          KEY2：未编辑时直接进入编辑（选中小时）；编辑中给当前字段 +1
 ******************************************************************************/
#include "app_time.h"
#include "app_power.h"

/* 上电时若时钟停走（后备电池也掉过电）使用的初始时间 */
#define APP_TIME_DEFAULT_YEAR    26
#define APP_TIME_DEFAULT_MONTH   11
#define APP_TIME_DEFAULT_DAY     8
#define APP_TIME_DEFAULT_WEEK    1     /* 1 = 周日 */
#define APP_TIME_DEFAULT_HOUR    19
#define APP_TIME_DEFAULT_MINUTE  10
#define APP_TIME_DEFAULT_SECOND  0

static DS1302_Time_t s_now;          /* 最近一次从 DS1302 读到的时间 */
static DS1302_Time_t s_edit;         /* 编辑副本，避免边改边写时钟 */
static uint32_t      s_last_read;    /* 上次读取 DS1302 的时刻 */
static uint32_t      s_edit_tick;    /* 最近一次按键时刻，用于超时保存退出 */
static uint8_t       s_edit_idx;     /* 0=不编辑；1..6=年/月/日/时/分/秒 */
static uint8_t       s_edit_dirty;   /* 本次编辑是否真的改过值 */
static uint8_t       s_need_read;    /* 下一次 Update 必须重读时钟 */

/* 某年某月有多少天（year2 = 0..99，表示 2000..2099；
 * 2000 能被 400 整除是闰年，因此 2000..2099 内“能被 4 整除”即为闰年） */
static uint8_t days_in_month(uint8_t year2, uint8_t month)
{
  static const uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

  if (month < 1 || month > 12)
    return 31;
  if (month == 2 && (year2 % 4) == 0)
    return 29;
  return days[month - 1];
}

/* 由年月日计算星期：1=周日 ... 7=周六（与 DS1302 星期寄存器、week_short 一致） */
static uint8_t weekday_from_ymd(uint16_t year, uint8_t month, uint8_t day)
{
  static const uint8_t t[12] = {0,3,2,5,0,3,5,1,4,6,2,4};
  uint16_t y = year;

  if (month < 3)
    y -= 1;
  return (uint8_t)(((y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7) + 1);
}

/* 读取时钟并刷新缓存 */
static void time_read(uint32_t now_ms)
{
  DS1302_GetTime(&s_now);
  s_last_read = now_ms;
  s_need_read = 0;
}

/* 把编辑副本写回 DS1302（没改过值就跳过，避免时钟被悄悄拨回） */
static void edit_commit(void)
{
  if (s_edit_dirty)
    DS1302_SetTime(&s_edit);

  s_edit_dirty = 0;
  s_edit_idx   = 0;
  s_need_read  = 1;
}

/* 进入编辑模式：以当前时间作为编辑起点 */
static void edit_begin(uint8_t first_field, uint32_t now_ms)
{
  time_read(now_ms);
  s_edit       = s_now;
  s_edit_dirty = 0;
  s_edit_idx   = first_field;
  s_edit_tick  = now_ms;
}

void AppTime_Init(void)
{
  DS1302_Time_t init_time;

  DS1302_Init();

  /* 仅在时钟停走（CH=1，通常意味着后备电池也掉过电）时写入初始时间；
   * 否则保留 DS1302 里正在走的时间，避免每次复位都覆盖用户按键设置的值。 */
  if (DS1302_IsClockHalted())
  {
    init_time.year   = APP_TIME_DEFAULT_YEAR;
    init_time.month  = APP_TIME_DEFAULT_MONTH;
    init_time.day    = APP_TIME_DEFAULT_DAY;
    init_time.week   = APP_TIME_DEFAULT_WEEK;
    init_time.hour   = APP_TIME_DEFAULT_HOUR;
    init_time.minute = APP_TIME_DEFAULT_MINUTE;
    init_time.second = APP_TIME_DEFAULT_SECOND;
    DS1302_SetTime(&init_time);
  }

  DS1302_StartClock();               /* 确保时钟在走 */

  s_last_read  = HAL_GetTick();
  s_edit_idx   = 0;
  s_edit_dirty = 0;
  s_need_read  = 1;
  time_read(s_last_read);
}

void AppTime_Invalidate(void)
{
  s_need_read = 1;
}

void AppTime_Update(uint32_t now_ms)
{
  if (s_edit_idx != 0)
  {
    /* 编辑中：只关心超时保存，不刷新显示用的时钟 */
    if ((uint32_t)(now_ms - s_edit_tick) >= APP_TIME_EDIT_TIMEOUT_MS)
      edit_commit();
    return;
  }

  if (s_need_read || (uint32_t)(now_ms - s_last_read) >= APP_TIME_REFRESH_MS)
    time_read(now_ms);
}

void AppTime_HandleKey(uint8_t events, uint32_t now_ms)
{
  if (events == 0)
    return;

  s_edit_tick = now_ms;

  if (events & APP_KEY1_EVENT)
  {
    if (s_edit_idx == 0)
      edit_begin(1, now_ms);              /* 从“年”开始 */
    else if (++s_edit_idx > 6)
      edit_commit();                      /* 秒之后再按：保存并退出 */
  }

  if (events & APP_KEY2_EVENT)
  {
    if (s_edit_idx == 0)
    {
      edit_begin(4, now_ms);              /* 未编辑时进入编辑，先选中“时” */
    }
    else
    {
      switch (s_edit_idx)
      {
        case 1: s_edit.year   = (s_edit.year + 1) % 100;    break;
        case 2: s_edit.month  = (s_edit.month % 12) + 1;    break;
        case 3: s_edit.day    = (s_edit.day >= days_in_month(s_edit.year, s_edit.month))
                                  ? 1 : (s_edit.day + 1);   break;
        case 4: s_edit.hour   = (s_edit.hour + 1) % 24;     break;
        case 5: s_edit.minute = (s_edit.minute + 1) % 60;   break;
        case 6: s_edit.second = (s_edit.second + 1) % 60;   break;
        default: break;
      }
      s_edit_dirty = 1;

      /* 月份/年份变化可能让“日”越界（如 1/31 → 2 月），这里统一收拢 */
      {
        uint8_t dim = days_in_month(s_edit.year, s_edit.month);
        if (s_edit.day > dim)
          s_edit.day = dim;
      }
      if (s_edit_idx <= 3)                /* 日期变了，同步重算星期 */
        s_edit.week = weekday_from_ymd(2000 + s_edit.year, s_edit.month, s_edit.day);
    }
  }
}

const DS1302_Time_t *AppTime_Get(void)
{
  return (s_edit_idx != 0) ? &s_edit : &s_now;
}

uint8_t AppTime_IsEditing(void)
{
  return (uint8_t)(s_edit_idx != 0);
}

uint8_t AppTime_EditIndex(void)
{
  return s_edit_idx;
}
