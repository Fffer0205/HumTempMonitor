#ifndef __APP_UI_H
#define __APP_UI_H

#include "main.h"

/* 温湿度仪表盘页面
 * 页面内容全部从 AppTime / AppSensor 取，绘制一次后由 GFX_Flush 增量刷屏。 */

void AppUI_Draw(void);

#endif /* __APP_UI_H */
