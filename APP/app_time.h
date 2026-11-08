#ifndef __APP_TIME_H
#define __APP_TIME_H

#include "main.h"
#include "ds1302.h"

/* 时间显示与编辑
 * - 缓存 DS1302 的时间，按需刷新（避免每次重绘都走一遍三线串行）
 * - 编辑时在副本上改，退出时才回写，避免边改边写时钟
 * - 10 秒无按键自动保存并退出编辑
 */

#define APP_TIME_EDIT_TIMEOUT_MS   10000UL   /* 编辑超时（无按键自动保存） */
#define APP_TIME_REFRESH_MS        250UL     /* 时钟缓存刷新周期 */

void AppTime_Init(void);                          /* 初始化 DS1302 并读取缓存 */
void AppTime_Update(uint32_t now_ms);             /* 刷新缓存 / 处理编辑超时 */
void AppTime_Invalidate(void);                    /* 强制下次 Update 重读时钟 */
void AppTime_HandleKey(uint8_t events, uint32_t now_ms);  /* 处理 KEY1/KEY2 事件 */

const DS1302_Time_t *AppTime_Get(void);           /* 显示用时间（编辑中返回副本） */
uint8_t AppTime_IsEditing(void);                  /* 1 = 正在编辑 */
uint8_t AppTime_EditIndex(void);                  /* 0=不编辑；1..6=年/月/日/时/分/秒 */

#endif /* __APP_TIME_H */
