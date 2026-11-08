/******************************************************************************
 * 文件名 : soft_i2c.c
 * 功能   : 软件模拟 I2C 主机（PB8 = SCL，PB9 = SDA）
 * 说明   : 由 AHT21 与 ADXL345 共用。时序与原 AHT21 驱动保持一致。
 ******************************************************************************/
#include "soft_i2c.h"

/* 微秒级延时：空循环实现，按 32MHz 主频标定 */
static void i2c_delay_us(uint32_t us)
{
  volatile uint32_t i;

  for (; us > 0; us--)
  {
    for (i = 0; i < 10; i++)
    {
      __NOP();
    }
  }
}

#define SCL_SET()    HAL_GPIO_WritePin(SOFT_I2C_SCL_GPIO_Port, SOFT_I2C_SCL_Pin, GPIO_PIN_SET)
#define SCL_RESET()  HAL_GPIO_WritePin(SOFT_I2C_SCL_GPIO_Port, SOFT_I2C_SCL_Pin, GPIO_PIN_RESET)
#define SDA_SET()    HAL_GPIO_WritePin(SOFT_I2C_SDA_GPIO_Port, SOFT_I2C_SDA_Pin, GPIO_PIN_SET)
#define SDA_RESET()  HAL_GPIO_WritePin(SOFT_I2C_SDA_GPIO_Port, SOFT_I2C_SDA_Pin, GPIO_PIN_RESET)
#define SDA_READ()   HAL_GPIO_ReadPin(SOFT_I2C_SDA_GPIO_Port, SOFT_I2C_SDA_Pin)

/* SDA 方向切换：写的时候推挽输出，读的时候输入 + 上拉 */
static void sda_out(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin   = SOFT_I2C_SDA_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SOFT_I2C_SDA_GPIO_Port, &GPIO_InitStruct);
}

static void sda_in(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin  = SOFT_I2C_SDA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SOFT_I2C_SDA_GPIO_Port, &GPIO_InitStruct);
}

void SoftI2C_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* SCL 推挽输出 */
  GPIO_InitStruct.Pin   = SOFT_I2C_SCL_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SOFT_I2C_SCL_GPIO_Port, &GPIO_InitStruct);

  /* SDA 先当输出 */
  GPIO_InitStruct.Pin = SOFT_I2C_SDA_Pin;
  HAL_GPIO_Init(SOFT_I2C_SDA_GPIO_Port, &GPIO_InitStruct);

  SCL_SET();
  SDA_SET();
}

/* 起始条件：SCL 高电平期间 SDA 由高变低
 * 该实现同时兼容“重复起始”：无论当前 SCL 高低都能产生正确的起始沿 */
void SoftI2C_Start(void)
{
  sda_out();
  SDA_SET();
  SCL_SET();
  i2c_delay_us(5);
  SDA_RESET();
  i2c_delay_us(5);
  SCL_RESET();
  i2c_delay_us(5);
}

/* 停止条件：SCL 高电平期间 SDA 由低变高 */
void SoftI2C_Stop(void)
{
  sda_out();
  SDA_RESET();
  i2c_delay_us(2);
  SCL_SET();
  i2c_delay_us(5);
  SDA_SET();
  i2c_delay_us(5);
}

uint8_t SoftI2C_WriteByte(uint8_t dat)
{
  uint8_t i, ack;

  sda_out();
  for (i = 0; i < 8; i++)
  {
    if (dat & 0x80)
      SDA_SET();
    else
      SDA_RESET();
    dat <<= 1;
    i2c_delay_us(2);
    SCL_SET();
    i2c_delay_us(5);
    SCL_RESET();
    i2c_delay_us(2);
  }

  /* 第 9 个时钟：释放 SDA，读从机应答 */
  sda_in();
  i2c_delay_us(2);
  SCL_SET();
  i2c_delay_us(5);
  ack = (uint8_t)(SDA_READ() == GPIO_PIN_SET);
  SCL_RESET();
  i2c_delay_us(2);
  sda_out();

  return ack;   /* 0 = ACK，1 = NACK */
}

uint8_t SoftI2C_ReadByte(uint8_t ack)
{
  uint8_t i, dat = 0;

  sda_in();
  for (i = 0; i < 8; i++)
  {
    dat <<= 1;
    SCL_SET();
    i2c_delay_us(5);
    if (SDA_READ() == GPIO_PIN_SET)
      dat |= 0x01;
    SCL_RESET();
    i2c_delay_us(2);
  }

  /* 第 9 个时钟：主机回 ACK / NACK */
  sda_out();
  if (ack)
    SDA_RESET();     /* ACK  = 拉低 */
  else
    SDA_SET();       /* NACK = 保持高 */
  i2c_delay_us(2);
  SCL_SET();
  i2c_delay_us(5);
  SCL_RESET();
  i2c_delay_us(2);

  return dat;
}

void SoftI2C_ReleaseBus(void)
{
  sda_out();
  SDA_SET();
  SCL_SET();
}

uint8_t SoftI2C_WriteReg(uint8_t dev_addr, uint8_t reg, uint8_t val)
{
  uint8_t nack;

  SoftI2C_Start();
  nack  = SoftI2C_WriteByte(dev_addr);
  nack |= SoftI2C_WriteByte(reg);
  nack |= SoftI2C_WriteByte(val);
  SoftI2C_Stop();

  return (uint8_t)(nack != 0);
}

uint8_t SoftI2C_ReadRegs(uint8_t dev_addr, uint8_t reg,
                         uint8_t *buf, uint8_t len)
{
  uint8_t i;

  if ((buf == NULL) || (len == 0))
    return 1;

  SoftI2C_Start();
  if (SoftI2C_WriteByte(dev_addr) != 0)          /* 从机没应答：器件不在 */
  {
    SoftI2C_Stop();
    return 1;
  }
  SoftI2C_WriteByte(reg);

  SoftI2C_Start();                               /* 重复起始，转读 */
  if (SoftI2C_WriteByte((uint8_t)(dev_addr | 0x01)) != 0)
  {
    SoftI2C_Stop();
    return 1;
  }

  for (i = 0; i < len; i++)
    buf[i] = SoftI2C_ReadByte((uint8_t)((i + 1) < len));

  SoftI2C_Stop();
  return 0;
}
