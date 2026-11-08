/******************************************************************************
 * 文件名 : ds18b20.c
 * 功能   : DS18B20 温度传感器一线总线（1-Wire）驱动程序
 * 接线   : DQ -> PA0（开漏输出，建议 DQ 对 VDD 接 4.7k 上拉）
 * 说明   : 延时至微秒基于 SysTick 计数值实现，不受编译优化影响
 ******************************************************************************/
#include "ds18b20.h"

#define DS18B20_SKIP_ROM      0xCC
#define DS18B20_CONVERT_T     0x44
#define DS18B20_READ_SCRATCH  0xBE
#define DS18B20_WRITE_SCRATCH 0x4E

/* 微秒级延时：基于 SysTick 向下计数值（VAL），兼容 HAL_Delay 同用的 SysTick。
 * SysTick 每 1 个时钟周期减 1，因此 us 微秒所需 tick 数 = us * (主频/1000000)。*/
static void ds18b20_delay_us(uint32_t us)
{
    uint32_t start, cur, delta, need;
    uint32_t reload = SysTick->LOAD;               /* SysTick 重装载值 */
    uint32_t cps    = HAL_RCC_GetHCLKFreq();       /* 总线时钟频率 Hz */

    if (us == 0)
        return;

    start = SysTick->VAL;                           /* 读取当前倒计数值 */
    need  = us * (cps / 1000000UL);                 /* 需要的 tick 数 */

    for (;;)
    {
        cur = SysTick->VAL;
        /* 计算经过的 tick 数，并处理 0 -> LOAD 的回绕 */
        delta = (start >= cur) ? (start - cur)
                               : (start + (reload + 1) - cur);
        if (delta >= need)
            break;
    }
}

/**
 * @brief 一线总线复位并检测器件存在
 * @retval 0：存在；1：超时(无器件)
 */
static uint8_t ds18b20_reset(void)
{
    uint8_t presence = 1;

    DS18B20_DQ_LOW();                 /* 拉低总线 >480us */
    ds18b20_delay_us(500);
    DS18B20_DQ_HIGH();                /* 释放总线，等待应答 */
    ds18b20_delay_us(70);

    presence = DS18B20_DQ_READ();     /* DS18B20 拉低总线作为存在应答 */
    ds18b20_delay_us(500);            /* 保持复位时序总时长 >=480us */

    return presence;
}

/* 发送一个字节（LSB 优先） */
static void ds18b20_write_byte(uint8_t dat)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        if (dat & 0x01)
        {
            /* 写 '1'：拉低 6us 后释放，总时隙约 70us */
            DS18B20_DQ_LOW();
            ds18b20_delay_us(6);
            DS18B20_DQ_HIGH();
            ds18b20_delay_us(64);
        }
        else
        {
            /* 写 '0'：持续拉低 >60us */
            DS18B20_DQ_LOW();
            ds18b20_delay_us(70);
            DS18B20_DQ_HIGH();
            ds18b20_delay_us(5);
        }
        dat >>= 1;
    }
}

/* 读取一个字节（LSB 优先） */
static uint8_t ds18b20_read_byte(void)
{
    uint8_t i, dat = 0;

    for (i = 0; i < 8; i++)
    {
        dat >>= 1;
        DS18B20_DQ_LOW();
        ds18b20_delay_us(2);          /* 拉低约 2us */
        DS18B20_DQ_HIGH();
        ds18b20_delay_us(12);         /* 约 14us 处采样 */
        if (DS18B20_DQ_READ())
            dat |= 0x80;
        ds18b20_delay_us(45);         /* 完成 60us 读时隙 */
    }
    return dat;
}

/**
 * @brief 初始化 DS18B20：配置 PA0 为开漏输出 + 上拉
 */
void DS18B20_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = DS18B20_DQ_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;   /* 开漏，便于读回总线电平 */
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DS18B20_DQ_GPIO_Port, &GPIO_InitStruct);

    DS18B20_DQ_HIGH();
}

/**
 * @brief 设置温度转换分辨率
 * @param bits 分辨率 9~12（对应 0.5 / 0.25 / 0.125 / 0.0625 ℃）
 * @retval 0：成功；1：器件不存在
 * @note  分辨率只写进暂存器（掉电失效），不写 EEPROM，避免反复擦写。
 *        分辨率越低转换越快：9bit = 93.75ms，12bit = 750ms。
 */
uint8_t DS18B20_SetResolution(uint8_t bits)
{
    uint8_t cfg;

    if (bits < 9)  bits = 9;
    if (bits > 12) bits = 12;

    cfg = (uint8_t)(0x1Fu | (uint8_t)((bits - 9) << 5));  /* bit6:5 = R1:R0 */

    if (ds18b20_reset())
        return 1;

    ds18b20_write_byte(DS18B20_SKIP_ROM);
    ds18b20_write_byte(DS18B20_WRITE_SCRATCH);
    ds18b20_write_byte(0x4B);   /* TH：报警上限，保持默认 */
    ds18b20_write_byte(0x46);   /* TL：报警下限，保持默认 */
    ds18b20_write_byte(cfg);    /* 配置寄存器 */

    return 0;
}

/**
 * @brief 启动一次温度转换（不复位等待）
 * @retval 0：成功；1：器件不存在
 */
uint8_t DS18B20_StartConvert(void)
{
    if (ds18b20_reset())
        return 1;

    ds18b20_write_byte(DS18B20_SKIP_ROM);
    ds18b20_write_byte(DS18B20_CONVERT_T);

    return 0;
}

/**
 * @brief 读取上一次转换的温度结果
 * @param temp 温度输出（单位 ℃）
 * @retval 0：成功；1：器件不存在
 */
uint8_t DS18B20_ReadTemp(float *temp)
{
    uint8_t low, high;
    uint8_t scratch[9];
    uint8_t i;
    int16_t raw = 0;

    if (temp == NULL)
        return 1;

    /* 复位 + 跳过 ROM + 读取暂存器(9字节) */
    if (ds18b20_reset())
        return 1;

    ds18b20_write_byte(DS18B20_SKIP_ROM);
    ds18b20_write_byte(DS18B20_READ_SCRATCH);

    for (i = 0; i < 9; i++)
    {
        scratch[i] = ds18b20_read_byte();
    }

    low  = scratch[0];
    high = scratch[1];
    raw  = (int16_t)(((uint16_t)high << 8) | low);

    *temp = (float)raw * 0.0625f;   /* 12bit 分辨率：LSB = 0.0625℃ */

    return 0;
}

/**
 * @brief 读取温度（阻塞式：启动转换后等待 760ms 再读结果）
 * @param temp 温度输出（单位 ℃）
 * @retval 0：成功；1：器件不存在
 */
uint8_t DS18B20_GetTemp(float *temp)
{
    if (temp == NULL)
        return 1;

    /* 1. 复位 + 跳过 ROM + 启动温度转换 */
    if (DS18B20_StartConvert())
        return 1;

    /* 2. 等待转换完成：12bit 分辨率最大需 750ms */
    HAL_Delay(760);

    /* 3. 读取结果 */
    return DS18B20_ReadTemp(temp);
}