/******************************************************************************
 * 文件名 : adxl345.c
 * 功能   : ADXL345 三轴加速度计驱动（软件 I2C，与 AHT21 共用 PB8/PB9）
 *
 * 只做芯片层面的事：探测、配置、读数、活动中断开关。
 * “抬手”判断在 APP/app_imu.c 里完成。
 ******************************************************************************/
#include "adxl345.h"
#include "soft_i2c.h"

static uint8_t s_addr_w;     /* 8 位写地址 */
static uint8_t s_present;    /* 1 = 探测到了芯片 */
static uint8_t s_last_id;    /* 最近一次探测读到的 DEVID，排查用 */

static uint8_t probe(uint8_t addr_w)
{
  uint8_t id = 0;

  if (SoftI2C_ReadRegs(addr_w, (uint8_t)(ADXL345_REG_DEVID | ADXL345_SPI_READ),
                       &id, 1) != 0)
  {
    s_last_id = 0;
    return 0;
  }
  s_last_id = id;

  if (id == ADXL345_DEVID_VALUE)
    return 1;

  /* 少数兼容片对“单字节读需置 bit7”的处理不同，再试一次普通地址读法 */
  if (SoftI2C_ReadRegs(addr_w, ADXL345_REG_DEVID, &id, 1) != 0)
    return 0;
  s_last_id = id;

  return (uint8_t)(id == ADXL345_DEVID_VALUE);
}

uint8_t ADXL345_WriteReg(uint8_t reg, uint8_t val)
{
  if (!s_present)
    return 1;

  return SoftI2C_WriteReg(s_addr_w, reg, val);
}

uint8_t ADXL345_ReadReg(uint8_t reg, uint8_t *val)
{
  if (!s_present)
    return 1;

  return SoftI2C_ReadRegs(s_addr_w,
                          (uint8_t)(reg | ADXL345_SPI_READ), val, 1);
}

uint8_t ADXL345_Init(void)
{
  uint8_t tmp;

  SoftI2C_Init();          /* 幂等：AHT21 已经初始化过也没关系 */
  s_present = 0;

  if (probe(ADXL345_ADDR_W_GND))
    s_addr_w = ADXL345_ADDR_W_GND;
  else if (probe(ADXL345_ADDR_W_VCC))
    s_addr_w = ADXL345_ADDR_W_VCC;
  else
    return 1;              /* 没接 / 地址不对 / 总线有问题 */

  s_present = 1;

  /* 量程 ±16g + 全分辨率：3.9mg/LSB，快速挥动不削顶 */
  ADXL345_WriteReg(ADXL345_REG_DATA_FORMAT,
                   (uint8_t)(ADXL345_DATA_FORMAT_FULL_RES | ADXL345_RANGE_16G));

  /* 静止时用低速率低功耗；抬手扫描时由 SetRate 临时提速 */
  ADXL345_WriteReg(ADXL345_REG_BW_RATE,
                   (uint8_t)(ADXL345_RATE_25HZ | ADXL345_LOW_POWER));

  /* 活动检测：AC 耦合 + 三轴全开，阈值 0.75g */
  ADXL345_WriteReg(ADXL345_REG_THRESH_ACT, ADXL345_ACT_THRESH_DEFAULT);
  ADXL345_WriteReg(ADXL345_REG_ACT_INACT_CTL,
                   (uint8_t)(ADXL345_ACT_AC | ADXL345_ACT_X |
                             ADXL345_ACT_Y | ADXL345_ACT_Z));

  /* 活动中断路由到 INT2（1 = INT2），先不开，等熄屏时再开 */
  ADXL345_WriteReg(ADXL345_REG_INT_MAP, ADXL345_INT_ACTIVITY);
  ADXL345_WriteReg(ADXL345_REG_INT_ENABLE, 0x00);

  /* 进入测量模式后稍等，让第一个采样稳定 */
  ADXL345_WriteReg(ADXL345_REG_POWER_CTL, ADXL345_MEASURE);
  ADXL345_ReadReg(ADXL345_REG_INT_SOURCE, &tmp);   /* 顺手清一次挂起标志 */
  HAL_Delay(10);

  return 0;
}

uint8_t ADXL345_IsPresent(void)
{
  return s_present;
}

uint8_t ADXL345_LastDevId(void)
{
  return s_last_id;
}

uint8_t ADXL345_ReadXYZ(ADXL345_Accel_t *a)
{
  uint8_t b[6];

  if ((!s_present) || (a == NULL))
    return 1;

  /* 从 DATAX0 起连读 6 字节，地址自带 0x80 自动递增 */
  if (SoftI2C_ReadRegs(s_addr_w,
                       (uint8_t)(ADXL345_REG_DATAX0 | ADXL345_SPI_READ),
                       b, 6) != 0)
    return 1;

  a->x = (int16_t)(((uint16_t)b[1] << 8) | b[0]);
  a->y = (int16_t)(((uint16_t)b[3] << 8) | b[2]);
  a->z = (int16_t)(((uint16_t)b[5] << 8) | b[4]);

  return 0;
}

void ADXL345_SetRate(uint8_t rate_code, uint8_t low_power)
{
  uint8_t val = (uint8_t)(rate_code & 0x0F);

  if (low_power)
    val |= ADXL345_LOW_POWER;

  ADXL345_WriteReg(ADXL345_REG_BW_RATE, val);
}

uint8_t ADXL345_ActivityEnable(uint8_t enable)
{
  if (!s_present)
    return 1;

  /* 关中断时把 INT_SOURCE 也清掉，避免 INT2 停在拉高状态 */
  if (!enable)
  {
    ADXL345_WriteReg(ADXL345_REG_INT_ENABLE, 0x00);
    ADXL345_ClearInterrupt();
  }
  else
  {
    ADXL345_ClearInterrupt();
    ADXL345_WriteReg(ADXL345_REG_INT_ENABLE, ADXL345_INT_ACTIVITY);
  }

  return 0;
}

void ADXL345_ClearInterrupt(void)
{
  uint8_t tmp;

  ADXL345_ReadReg(ADXL345_REG_INT_SOURCE, &tmp);
}

uint8_t ADXL345_ReadIntSource(void)
{
  uint8_t src = 0;

  ADXL345_ReadReg(ADXL345_REG_INT_SOURCE, &src);
  return src;
}
