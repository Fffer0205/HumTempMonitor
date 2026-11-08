/******************************************************************************
 * 文件名 : app_ui.c
 * 功能   : 温湿度仪表盘页面
 *
 * 布局（128x64）：
 *   y0..7   状态栏：时间 | 星期 | 电池图标 + 电量
 *   y9      分隔线
 *   y11..18 分区标签：TEMP °C | HUMIDITY（中间一条点状竖线分隔两栏）
 *   y20..51 主值：平均温度（整数 16x32 + 小数 8x16） | 湿度（水滴 + 数字 16x32 + %）
 *   y54     分隔线
 *   y56..63 底部：日期 + 电池电压
 *
 * 编辑模式（AppTime_EditIndex() != 0）：中间区域换成“字段名 + 大号数值 + 按键提示”，
 * 状态栏和底部仍显示编辑副本的时间/日期，方便对照。
 ******************************************************************************/
#include "app_ui.h"
#include "oled_gfx.h"
#include "app_time.h"
#include "app_sensor.h"
#include "app_imu.h"
#include "battery.h"
#include <stdio.h>
#include <string.h>

/* 顶部星期缩写：index = week-1（1=周日） */
static const char *week_short[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

/* 编辑字段名：下标与 AppTime_EditIndex() 对应 */
static const char *edit_field[] = {"", "YEAR", "MONTH", "DAY", "HOUR", "MIN", "SEC"};

/* ---------- 状态栏：时间 + 星期 + 电量 ---------- */
static void draw_status_bar(const DS1302_Time_t *t, const AppSensor_t *s)
{
  char    buf[24];
  int     num_x, pct_x, len;

  sprintf(buf, "%02d:%02d:%02d", t->hour, t->minute, t->second);
  GFX_String6(2, 0, buf);

  if (t->week >= 1 && t->week <= 7)
    GFX_String6(58, 0, week_short[t->week - 1]);

  /* 电量 = 数字 + 2px 间隔 + '%'
   * 6x8 字库里数字左边留了一列空白，而 '%' 是从第 0 列开始画的，
   * 直接拼成 "95%" 时两者会粘在一起，所以把 '%' 拆出来单独定位。 */
  if (s->bat_ok)
    sprintf(buf, "%d", s->bat_pct);
  else
    strcpy(buf, "--");

  len   = (int)strlen(buf);
  num_x = 126 - (6 * len + 2 + 6);            /* 数字 + 间隔 + '%' 的总宽 */
  pct_x = num_x + 6 * len + 2;

  GFX_Battery(num_x - 16, 0, s->bat_ok ? s->bat_pct : 0);
  GFX_String6(num_x, 0, buf);
  GFX_String6(pct_x, 0, "%");
}

/* ---------- 编辑模式：字段名 + 大号数值 + 按键提示 ---------- */
static void draw_edit_area(const DS1302_Time_t *t, uint8_t edit_idx)
{
  char buf[24];

  GFX_String6(2, 11, edit_field[edit_idx]);
  GFX_String6(126 - 78, 11, "K1:NEXT K2:+1");

  switch (edit_idx)
  {
    case 1:  sprintf(buf, "20%02d", t->year);   break;   /* 年份显示完整 4 位 */
    case 2:  sprintf(buf, "%02d", t->month);    break;
    case 3:  sprintf(buf, "%02d", t->day);      break;
    case 4:  sprintf(buf, "%02d", t->hour);     break;
    case 5:  sprintf(buf, "%02d", t->minute);   break;
    default: sprintf(buf, "%02d", t->second);   break;
  }

  GFX_String16((128 - 16 * (int)strlen(buf)) / 2, 20, buf, 2, 2);
}

/* 小号温度单位 "°C"：3x3 小圆环 + C，共 10px 宽。
 * 圆环比字母小一号，看起来才是“度数符号”，而不是又一个字符。 */
static void draw_deg_c(int x, int y)
{
  GFX_Pixel(x + 1, y + 1);
  GFX_Pixel(x,     y + 2);
  GFX_Pixel(x + 2, y + 2);
  GFX_Pixel(x + 1, y + 3);

  GFX_Char6(x + 4, y, 'C');
}

/* ---------- 正常模式：温度（左半） + 湿度（右半） ---------- */
static void draw_monitor_area(const AppSensor_t *s)
{
  char    buf[24];
  uint8_t humi;
  int     y;

  /* 分区标签（各自在半幅内居中） */
  GFX_String6(20, 11, "TEMP");
  GFX_String6(72, 11, "HUMIDITY");

  /* 中间点状竖线：轻量分隔温度/湿度两栏 */
  for (y = 11; y <= 52; y += 2)
    GFX_Pixel(64, y);

  /* ---------- 主值：AHT21 与 DS18B20 的平均温度 ---------- */
  if (s->aht_ok || s->ds_ok)
    sprintf(buf, "%.1f", AppSensor_AverageTemp(s));
  else
    strcpy(buf, "--.-");

  /* 拆成“整数大字 + 小数小字”。
   * 单位 °C 放在数值右侧、与小数点底部对齐，和右半幅湿度末尾的“%”呼应，
   * 不再紧贴着标题 TEMP。数值太宽（3 位负数）放不下时，单位退回标题行右侧。 */
  {
    char    *dot  = strchr(buf, '.');
    int      ilen = dot ? (int)(dot - buf) : (int)strlen(buf);
    int      flen = dot ? (int)strlen(dot) : 0;
    uint8_t  big  = (uint8_t)((ilen <= 3) && (flen <= 2));
    int      w    = big ? (ilen * 16 + flen * 8) : ((int)strlen(buf) * 8);
    int      x0, unit_x;

    if (w + 13 <= 64)                            /* 3px 间隔 + 10px 单位 */
    {
      x0     = (64 - (w + 13)) / 2;              /* 数值+单位整体居中 */
      unit_x = x0 + w + 3;
    }
    else
    {
      x0     = (64 - w) / 2;
      unit_x = -1;                               /* 放不下，单位改到标题行 */
    }
    if (x0 < 0)
      x0 = 0;

    if (big)
    {
      char saved = buf[ilen];
      buf[ilen] = '\0';
      GFX_String16(x0, 20, buf, 2, 2);                    /* 整数：16x32 */
      buf[ilen] = saved;
      if (flen)
        GFX_String16(x0 + ilen * 16, 36, buf + ilen, 1, 1); /* 小数：8x16 */
    }
    else
    {
      GFX_String16(x0, 28, buf, 1, 1);                   /* 极端值：整体 8x16 */
    }

    if (unit_x >= 0)
      draw_deg_c(unit_x, big ? 44 : 36);
    else
      draw_deg_c(53, 11);
  }

  /* ---------- 副值：湿度 ---------- */
  if (s->aht_ok)
  {
    humi = (uint8_t)(s->aht_h + 0.5f);
    if (humi > 100) humi = 100;
    sprintf(buf, "%d", humi);
  }
  else
  {
    strcpy(buf, "--");
  }

  {
    int hlen = (int)strlen(buf);
    int hx   = 112 - 16 * hlen;                /* 数字右端对齐到 x=112 */

    if (hx < 64) hx = 64;
    if (hlen <= 2)
      GFX_Drop(hx - 12, 31);                   /* 水滴图标，与大号数字垂直居中 */
    GFX_String16(hx, 20, buf, 2, 2);           /* 湿度数字：16x32 */
    GFX_String16(114, 36, "%", 1, 1);          /* % 为小字，与数字底部对齐 */
  }
}

/* ---------- 加速度计标定页（APP_IMU_DEBUG_SHOW = 1 时启用） ----------
 * 第一行：IMU OK/NG + 探测到的 DEVID（正常 E5）
 * 第二行：X/Y 轴重力（mg）
 * 第三行：Z 轴重力 + score（抬腕模式下标定用，表盘朝上时应接近 +1）
 * 第四行：A = 累计活动中断次数，R = 累计判定成功次数，
 *         D = 最近一次判定窗口内的最大逐帧变化（mg）
 * 晃动时 A 应该增长；晃动时 D 通常能到 100~600。 */
#if (APP_IMU_DEBUG_SHOW != 0)
static void draw_imu_debug_area(void)
{
  char  buf[24];
  float x, y, z;

  AppIMU_GetGravity(&x, &y, &z);

  sprintf(buf, "IMU%s ID=%02X", AppIMU_IsPresent() ? " OK" : " NG",
                                (unsigned)AppIMU_DevId());
  GFX_String6(2, 11, buf);

  sprintf(buf, "X%+5.0f Y%+5.0f", x, y);
  GFX_String6(2, 20, buf);

  sprintf(buf, "Z%+5.0f S%+4.2f", z, AppIMU_LastScore());
  GFX_String6(2, 30, buf);

  sprintf(buf, "A%u R%u D%4.0f", (unsigned)AppIMU_ActivityCount(),
                                 (unsigned)AppIMU_RaiseCount(),
                                 AppIMU_LastDeltaMg());
  GFX_String6(2, 40, buf);
}
#endif

/* ---------- 底部：日期 + 电池电压 ---------- */
static void draw_footer(const DS1302_Time_t *t, const AppSensor_t *s)
{
  char buf[24];

  sprintf(buf, "20%02d-%02d-%02d", t->year, t->month, t->day);
  GFX_String6(2, 56, buf);

#if BAT_DEBUG_SHOW_RAW
  (void)s;
  sprintf(buf, "ADC%5u", (unsigned)g_bat_diag.raw_last);   /* 调试：显示 ADC 原始值 */
#else
  if (s->bat_ok)
    sprintf(buf, "BAT %.1fV", s->vbat);
  else
    strcpy(buf, "BAT N/A");
#endif
  GFX_String6(126 - 6 * (int)strlen(buf), 56, buf);
}

void AppUI_Draw(void)
{
  const DS1302_Time_t *t = AppTime_Get();
  const AppSensor_t   *s = AppSensor_Get();
  uint8_t              edit_idx = AppTime_EditIndex();

#if (APP_IMU_DEBUG_SHOW != 0)
  AppIMU_DebugSample();
#endif

  GFX_Clear();

  draw_status_bar(t, s);
  GFX_HLine(0, 127, 9);

#if (APP_IMU_DEBUG_SHOW != 0)
  draw_imu_debug_area();
#else
  if (edit_idx != 0)
    draw_edit_area(t, edit_idx);
  else
    draw_monitor_area(s);
#endif

  GFX_HLine(0, 127, 54);
  draw_footer(t, s);

  GFX_Flush();
}
