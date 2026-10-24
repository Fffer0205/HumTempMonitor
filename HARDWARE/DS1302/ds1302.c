#include "ds1302.h"

/* DS1302 寄存器地址（命令字节的地址部分，低 5 位） */
#define DS1302_SEC        0x80    /* 秒       */
#define DS1302_MIN        0x82    /* 分       */
#define DS1302_HOUR       0x84    /* 时       */
#define DS1302_DATE       0x86    /* 日       */
#define DS1302_MONTH      0x88    /* 月       */
#define DS1302_DAY        0x8A    /* 星期     */
#define DS1302_YEAR       0x8C    /* 年       */
#define DS1302_WP         0x8E    /* 写保护   */
#define DS1302_CHARGE     0x90    /* 充电配置 */

/* 微秒级延时：利用空循环实现，用于时序控制（主频约 4MHz） */
void ds1302_delay_us(uint32_t us)
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

/*
 * DS1302 是一个 3 线制（SCLK/I/O/RST）器件，
 * 数据按 LSB 优先（低位在前）逐位收发。
 */

/* 写一个字节（LSB 优先） */
static void ds1302_write_byte(uint8_t dat)
{
    uint8_t i;

    DS1302_IO_OUT();
    for (i = 0; i < 8; i++)
    {
        if (dat & 0x01)
            DS1302_IO_SET();
        else
            DS1302_IO_RESET();
        dat >>= 1;

        DS1302_SCLK_SET();          /* SCLK 上升沿锁存数据 */
        DS1302_DelayUs(1);
        DS1302_SCLK_RESET();
        DS1302_DelayUs(1);
    }
}

/* 读一个字节（LSB 优先）
 * 注意：命令字节的最后一个下降沿已把数据 bit0 放到 I/O 上，
 * 因此每轮循环必须先采样当前位，再产生时钟沿让芯片输出下一位。 */
static uint8_t ds1302_read_byte(void)
{
    uint8_t i, dat = 0;

    DS1302_IO_IN();
    DS1302_DelayUs(2);
    for (i = 0; i < 8; i++)
    {
        dat >>= 1;                  /* LSB 优先累积 */
        if (DS1302_IO_READ())       /* 先读当前有效位 */
            dat |= 0x80;

        DS1302_SCLK_SET();          /* 拉高 */
        DS1302_DelayUs(2);
        DS1302_SCLK_RESET();        /* 下降沿：DS1302 送下一位 */
        DS1302_DelayUs(2);
    }

    return dat;
}

/* 向寄存器写入一个字节：先发命令，再发数据 */
static void ds1302_write_reg(uint8_t addr, uint8_t dat)
{
    DS1302_RST_SET();               /* 置高 RST 激活通信 */
    DS1302_DelayUs(2);
    ds1302_write_byte(addr | 0x80); /* 命令字节：bit7=1，bit6=0 表示写 */
    ds1302_write_byte(dat);
    DS1302_RST_RESET();             /* 拉低 RST 结束通信 */
    DS1302_DelayUs(2);
}

/* 从寄存器读取一个字节：先发命令，再收数据 */
static uint8_t ds1302_read_reg(uint8_t addr)
{
    uint8_t dat;

    DS1302_RST_SET();
    DS1302_DelayUs(2);
    ds1302_write_byte(addr | 0x80 | 0x01); /* 命令字节：bit7=1，bit6=1 表示读 */
    dat = ds1302_read_byte();
    DS1302_RST_RESET();
    DS1302_DelayUs(2);

    return dat;
}

/* 十进制转 BCD 码 */
static uint8_t dec_to_bcd(uint8_t dec)
{
    return ((dec / 10) << 4) | (dec % 10);
}

/* BCD 码转十进制 */
static uint8_t bcd_to_dec(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

/* 初始化 GPIO：将 SCLK、I/O、RST 三线配置为输出 */
void DS1302_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* SCLK 与 RST 始终为主机输出 */
    GPIO_InitStruct.Pin = DS1302_SCLK_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DS1302_SCLK_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = DS1302_RST_Pin;
    HAL_GPIO_Init(DS1302_RST_GPIO_Port, &GPIO_InitStruct);

    /* 上电时 SCLK 必须保持低电平，以防误写 */
    DS1302_SCLK_RESET();
    DS1302_RST_RESET();
    DS1302_IO_SET();

    DS1302_IO_OUT();
}

/* 开启时钟：清除秒寄存器的 CH 时钟停止位，
 * 但保留 WP 写保护位为 1（锁定），防止误写寄存器的时钟值。 */
void DS1302_StartClock(void)
{
    ds1302_write_reg(DS1302_WP, 0x00);   /* 关闭写保护 */
    /* 读取当前秒值并清除 CH 位（最高位） */
    uint8_t sec = ds1302_read_reg(DS1302_SEC);
    sec &= 0x7F;
    ds1302_write_reg(DS1302_SEC, sec);
    ds1302_write_reg(DS1302_WP, 0x80);   /* 恢复写保护 */
}

/* 设置时间（输入十进制数值，内部转为 BCD） */
void DS1302_SetTime(const DS1302_Time_t *time)
{
    ds1302_write_reg(DS1302_WP, 0x00);          /* 关闭写保护 */

    ds1302_write_reg(DS1302_SEC,   dec_to_bcd(time->second));
    ds1302_write_reg(DS1302_MIN,   dec_to_bcd(time->minute));
    ds1302_write_reg(DS1302_HOUR,  dec_to_bcd(time->hour));
    ds1302_write_reg(DS1302_DATE,  dec_to_bcd(time->day));
    ds1302_write_reg(DS1302_MONTH, dec_to_bcd(time->month));
    ds1302_write_reg(DS1302_DAY,   dec_to_bcd(time->week));
    ds1302_write_reg(DS1302_YEAR,  dec_to_bcd(time->year));

    ds1302_write_reg(DS1302_WP, 0x80);          /* 恢复写保护 */
}

/* 读取时间（内部 BCD 转为十进制） */
void DS1302_GetTime(DS1302_Time_t *time)
{
    time->second = bcd_to_dec(ds1302_read_reg(DS1302_SEC));
    time->minute = bcd_to_dec(ds1302_read_reg(DS1302_MIN));
    time->hour   = bcd_to_dec(ds1302_read_reg(DS1302_HOUR));
    time->day    = bcd_to_dec(ds1302_read_reg(DS1302_DATE));
    time->month  = bcd_to_dec(ds1302_read_reg(DS1302_MONTH));
    time->week   = bcd_to_dec(ds1302_read_reg(DS1302_DAY));
    time->year   = bcd_to_dec(ds1302_read_reg(DS1302_YEAR));
}