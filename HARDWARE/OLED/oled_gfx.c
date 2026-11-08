/******************************************************************************
 * 文件名 : oled_gfx.c
 * 功能   : 128x64 单色图形层（帧缓冲 + 增量刷屏）
 * 说明   : 整屏先在 RAM 帧缓冲里画好，最后只把与上次相比变化的页/列区间刷到
 *          SSD1306。低功耗改造前每次重绘都会把 1KB 显存全量写一遍，在软件
 *          I2C 下要几十毫秒；改成增量后，只有时间变化时每帧只写第一页。
 ******************************************************************************/
#include "oled_gfx.h"
#include "oled.h"
#include <string.h>

/* F8X16 大字模（8x16）与 F6x8 小字模，均定义于 oledfont.h */
extern const unsigned char F8X16[];
extern const unsigned char F6x8[][6];

static uint8_t s_fb[8][GFX_WIDTH];       /* 当前帧缓冲（页 × 列），约 1KB RAM */
static uint8_t s_shadow[8][GFX_WIDTH];   /* 上一次真正刷到屏上的内容 */
static uint8_t s_shadow_valid = 0;       /* s_shadow 是否有效 */

/* 清空帧缓冲（只清 RAM，不动屏） */
void GFX_Clear(void)
{
  memset(s_fb, 0, sizeof(s_fb));
}

/* 点亮单个像素（越界自动忽略）
 * 注意：SSD1306 一个页内 bit0 是最上面一行，字库 F6x8/F8X16 也是 bit0 在上，
 * 因此这里必须用 bit(y&7)，写反的话真机上每 8 行会上下镜像。 */
void GFX_Pixel(int x, int y)
{
  if (x < 0 || x > (GFX_WIDTH - 1) || y < 0 || y > (GFX_HEIGHT - 1))
    return;
  s_fb[y >> 3][x] |= (uint8_t)(1u << (y & 7));
}

/* 填充矩形（含端点，自动裁剪到屏幕内） */
void GFX_BoxFill(int x1, int y1, int x2, int y2)
{
  int x, y, t;

  if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
  if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 > (GFX_WIDTH - 1))  x2 = GFX_WIDTH - 1;
  if (y2 > (GFX_HEIGHT - 1)) y2 = GFX_HEIGHT - 1;

  for (y = y1; y <= y2; y++)
    for (x = x1; x <= x2; x++)
      GFX_Pixel(x, y);
}

/* 矩形边框 */
void GFX_Rect(int x1, int y1, int x2, int y2)
{
  GFX_BoxFill(x1, y1, x2, y1);
  GFX_BoxFill(x1, y2, x2, y2);
  GFX_BoxFill(x1, y1, x1, y2);
  GFX_BoxFill(x2, y1, x2, y2);
}

/* 水平/垂直直线 */
void GFX_HLine(int x1, int x2, int y) { GFX_BoxFill(x1, y, x2, y); }
void GFX_VLine(int x, int y1, int y2) { GFX_BoxFill(x, y1, x, y2); }

/* F6x8 小字（6 像素宽），用于标签、状态栏、底部信息 */
void GFX_Char6(int x, int y, char ch)
{
  uint8_t c, col, row, bits;

  if (ch < ' ' || ch > '~')
    ch = '?';
  c = (uint8_t)(ch - ' ');

  for (col = 0; col < 6; col++)
  {
    bits = F6x8[c][col];
    for (row = 0; row < 8; row++)
      if (bits & (1u << row))
        GFX_Pixel(x + (int)col, y + (int)row);
  }
}

void GFX_String6(int x, int y, const char *s)
{
  while (*s)
  {
    GFX_Char6(x, y, *s++);
    x += 6;
  }
}

/* F8X16 字体按 sx/sy 倍放大绘制（sx=sy=2 即 16x32 大号数字） */
void GFX_Char16(int x, int y, char ch, uint8_t sx, uint8_t sy)
{
  uint8_t c, col, row, hi, lo;

  if (ch < ' ' || ch > '~')
    ch = '?';
  c = (uint8_t)(ch - ' ');

  for (col = 0; col < 8; col++)
  {
    hi = F8X16[c * 16 + col];
    lo = F8X16[c * 16 + col + 8];
    for (row = 0; row < 8; row++)
    {
      /* F8X16 与 SSD1306 页一致：bit0 为顶行 */
      if (hi & (1u << row))
        GFX_BoxFill(x + col * sx, y + row * sy,
                    x + col * sx + sx - 1, y + row * sy + sy - 1);
      if (lo & (1u << row))
        GFX_BoxFill(x + col * sx, y + (row + 8) * sy,
                    x + col * sx + sx - 1, y + (row + 8) * sy + sy - 1);
    }
  }
}

void GFX_String16(int x, int y, const char *s, uint8_t sx, uint8_t sy)
{
  while (*s)
  {
    GFX_Char16(x, y, *s++, sx, sy);
    x += 8 * sx;
  }
}

/* 5x5 空心圆（度符号） */
void GFX_Degree(int x, int y)
{
  static const uint8_t ring[5] = {0x0E, 0x11, 0x11, 0x11, 0x0E};
  uint8_t r, c;

  for (r = 0; r < 5; r++)
    for (c = 0; c < 5; c++)
      if ((ring[r] >> (4 - c)) & 1)
        GFX_Pixel(x + c, y + r);
}

/* 7x10 水滴图标（湿度） */
void GFX_Drop(int x, int y)
{
  static const uint8_t drop[10] = {0x08, 0x1C, 0x1C, 0x3E, 0x3E,
                                   0x7F, 0x7F, 0x7F, 0x3E, 0x1C};
  uint8_t r, c;

  for (r = 0; r < 10; r++)
    for (c = 0; c < 7; c++)
      if ((drop[r] >> (6 - c)) & 1)
        GFX_Pixel(x + c, y + r);
}

/* 14x8 电池图标：外壳 + 正极 + 按百分比填充 */
void GFX_Battery(int x, int y, uint8_t pct)
{
  int fill;

  if (pct > 100) pct = 100;
  GFX_Rect(x, y, x + 11, y + 7);              /* 12x8 外壳    */
  GFX_BoxFill(x + 12, y + 2, x + 13, y + 5);  /* 正极凸起     */

  fill = (pct * 8 + 50) / 100;                /* 内部可填 8px */
  if (fill > 0)
    GFX_BoxFill(x + 2, y + 2, x + 1 + fill, y + 5);
}

/* 把帧缓冲刷到 OLED：逐页比较，只写有变化的列区间 */
void GFX_Flush(void)
{
  uint8_t page, col, c0, c1;

  for (page = 0; page < 8; page++)
  {
    if (s_shadow_valid && (memcmp(s_fb[page], s_shadow[page], GFX_WIDTH) == 0))
      continue;                                   /* 整页没变，跳过 */

    c0 = 0;
    c1 = GFX_WIDTH - 1;
    if (s_shadow_valid)
    {
      while (c0 < GFX_WIDTH && s_fb[page][c0] == s_shadow[page][c0])
        c0++;
      while (c1 > c0 && s_fb[page][c1] == s_shadow[page][c1])
        c1--;
    }

    OLED_Set_Pos(c0, page);
    for (col = c0; col <= c1; col++)
      OLED_WR_Byte(s_fb[page][col], OLED_DATA);

    memcpy(s_shadow[page], s_fb[page], GFX_WIDTH);
  }

  s_shadow_valid = 1;
}

void GFX_Invalidate(void)
{
  s_shadow_valid = 0;
}
