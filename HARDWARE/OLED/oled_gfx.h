#ifndef __OLED_GFX_H
#define __OLED_GFX_H

#include "main.h"

/* 128x64 单色图形层
 * 整屏先在 RAM 帧缓冲里画好，最后再刷到 SSD1306。
 * 坐标：x = 0..127（左→右），y = 0..63（上→下），置 1 表示点亮。
 */

#define GFX_WIDTH   128
#define GFX_HEIGHT  64

/* -------- 帧缓冲绘制 -------- */
void GFX_Clear(void);
void GFX_Pixel(int x, int y);
void GFX_BoxFill(int x1, int y1, int x2, int y2);
void GFX_Rect(int x1, int y1, int x2, int y2);
void GFX_HLine(int x1, int x2, int y);
void GFX_VLine(int x, int y1, int y2);

/* -------- 字库绘制（F6x8 小字 / F8X16 可缩放） -------- */
void GFX_Char6(int x, int y, char ch);
void GFX_String6(int x, int y, const char *s);
void GFX_Char16(int x, int y, char ch, uint8_t sx, uint8_t sy);
void GFX_String16(int x, int y, const char *s, uint8_t sx, uint8_t sy);

/* -------- 图标 -------- */
void GFX_Degree(int x, int y);
void GFX_Drop(int x, int y);
void GFX_Battery(int x, int y, uint8_t pct);

/* -------- 刷屏 -------- */
/* 只把与上次相比发生变化的页/列区间写到屏上（软件 I2C 下能省下大量时间） */
void GFX_Flush(void);

/* 作废影子缓冲：下一次 GFX_Flush 会全屏重刷。
 * 熄屏一段时间后再点亮时使用，保证屏上内容与帧缓冲一致。 */
void GFX_Invalidate(void);

#endif /* __OLED_GFX_H */
