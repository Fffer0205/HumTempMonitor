/******************************************************************************
 * 文件名 : battery.c
 * 功能   : 锂电池电量检测驱动（寄存器直接操作 ADC1）
 * 接线   : 电池正极 -- R1(100k) -- PA1 -- R2(100k) -- GND
 *          中点电压 = Vbat/2，故实际电压 = ADC电压 × 2
 * 说明   : 本项目未包含 HAL ADC 驱动库，因此 ADC1 采用寄存器方式初始化和读取
 *
 * ADC 开关的三条硬性规则（见参考手册 ADC_CR 寄存器）：
 *   1. ADEN 不能用软件直接写 0，必须置 ADDIS，由硬件在关闭完成后清零；
 *   2. ADCAL（校准）只能在 ADEN=0 时启动，否则写入被忽略；
 *   3. ADVREGEN 只能在 ADEN=0 时清除。
 *   违反任意一条，ADC 会“卡”在使能状态：校准永远等不到 EOCAL，
 *   后续读数全部为 0，界面就会显示 --% / BAT N/A。
 *
 * 调试：battery.h 里有两个开关
 *   BAT_ADC_POWER_CYCLE  置 0 可退回“ADC 常开”的老做法，用来定位问题
 *   BAT_DEBUG_SHOW_RAW   置 1 会把原始 ADC 值显示在屏幕底部
 *   另外 g_bat_diag 里记录了失败次数，可在 Keil 的 Watch 窗口观察。
 ******************************************************************************/
#include "battery.h"

#define BAT_CURVE_LEN   (sizeof(bat_curve) / sizeof(bat_curve[0]))

/* 诊断计数（全局，方便在调试器里观察） */
volatile BAT_Diag_t g_bat_diag;

/* ADC 外设配置是否完成（GPIO/时钟已就绪，可以尝试测量） */
static uint8_t s_hw_ready = 0;

/* 最近一次测量是否有效：界面据此决定显示实测值还是 N/A */
static uint8_t s_meas_ok = 0;

/* 低通滤波后的电压，抑制高阻分压带来的读数跳动 */
static float   s_vbat_filt = 0.0f;

/* 单节锂电池开路电压 -> 剩余电量对照表（由高到低，单位 mV / %） */
static const struct { uint32_t mv; uint32_t pct; } bat_curve[] =
{
    {4200, 100}, {4100,  90}, {4000,  80}, {3900,  70}, {3800,  60},
    {3750,  50}, {3700,  40}, {3650,  30}, {3600,  20}, {3500,  10},
    {3400,   5}, {3000,   0},
};

/* 等待 ISR 中的标志位置位，超时返回 1（防止 ADC 异常时卡死主循环） */
static uint8_t bat_wait_flag(uint32_t flag)
{
    uint32_t start = HAL_GetTick();

    while ((ADC1->ISR & flag) == 0UL)
    {
        if ((HAL_GetTick() - start) > BAT_ADC_TIMEOUT_MS)
            return 1;
    }
    return 0;
}

/* 关闭 ADC 并等待它真正停止。
 * 注意：ADEN 只能由硬件在 ADDIS 命令后清零，直接写 0 是无效的。 */
static void bat_adc_disable(void)
{
    volatile uint32_t i;

    if ((ADC1->CR & ADC_CR_ADEN) == 0UL)
        return;

    /* 只有正在转换时才需要 ADSTP；ADSTP 由硬件在转换真正停止后清零 */
    if ((ADC1->CR & ADC_CR_ADSTART) != 0UL)
    {
        ADC1->CR |= ADC_CR_ADSTP;
        for (i = 0; i < 10000UL && (ADC1->CR & ADC_CR_ADSTP) != 0UL; i++) { __NOP(); }
    }

    /* 关闭 ADC：ADEN 不能由软件直接写 0，必须置 ADDIS，由硬件清零 */
    ADC1->CR |= ADC_CR_ADDIS;
    for (i = 0; i < 10000UL && (ADC1->CR & ADC_CR_ADEN) != 0UL; i++) { __NOP(); }
}

/* ADC 基本配置：只能在 ADEN=0 时写（CFGR2.CKMODE 在 ADEN=1 时是写保护的）。
 * 每次上电都重写一遍，不依赖深度掉电期间寄存器内容是否保留。 */
static void bat_config(void)
{
    /* 同步时钟 PCLK/2（CKMODE=01，PCLK 32MHz -> 16MHz） */
    ADC1->CFGR2 = (ADC1->CFGR2 & ~ADC_CFGR2_CKMODE) | ADC_CFGR2_CKMODE_0;

    /* 单次转换 + 软件触发（复位值即如此，显式写一遍） */
    ADC1->CFGR1 &= ~(ADC_CFGR1_CONT | ADC_CFGR1_EXTEN | ADC_CFGR1_DMAEN);

    /* 采样时间取最长档（160.5 个 ADC 时钟），适配 100k/100k 高阻分压 */
    ADC1->SMPR = (ADC1->SMPR & ~ADC_SMPR_SMP) | ADC_SMPR_SMP;

    /* 选择通道 1（PA1 = ADC_IN1），其余通道全部关闭 */
    ADC1->CHSELR = BAT_ADC_CHSEL;
}

static void bat_power_down(void);

/* ADC 上电：确保关闭 -> 写配置 -> 开稳压器 -> 校准 -> 使能。
 * 校准必须在 ADC 使能（ADEN=0）时进行。 */
static uint8_t bat_power_up_once(void)
{
    volatile uint32_t i;

    /* 1. 确保 ADC 处于关闭状态（上次掉电异常时可能仍是使能态） */
    bat_adc_disable();
    if ((ADC1->CR & ADC_CR_ADEN) != 0UL)
        return 1;
    ADC1->ISR = ADC_ISR_ADRDY;                    /* 写 1 清除就绪标志 */

    /* 2. 重写配置（必须在 ADEN=0 时进行） */
    bat_config();

    /* 3. 打开 ADC 内部稳压器，等待建立时间（>10us） */
    ADC1->CR |= ADC_CR_ADVREGEN;
    for (i = 0; i < 5000UL; i++) { __NOP(); }

    /* 4. 校准（只能在 ADEN=0 时启动） */
    ADC1->ISR = ADC_ISR_EOCAL;                    /* 清旧的校准结束标志 */
    ADC1->CR |= ADC_CR_ADCAL;
    if (bat_wait_flag(ADC_ISR_EOCAL) != 0U)
        return 1;
    ADC1->ISR = ADC_ISR_EOCAL;

    /* 5. 使能 ADC，等待就绪 */
    ADC1->CR |= ADC_CR_ADEN;
    if (bat_wait_flag(ADC_ISR_ADRDY) != 0U)
        return 1;
    ADC1->ISR = ADC_ISR_ADRDY;

    return 0;
}

/* 上电失败时再试一次，排除偶发的时序抖动 */
static uint8_t bat_power_up(void)
{
    if (bat_power_up_once() == 0U)
        return 0;

    bat_power_down();                             /* 清干净再重来 */

    if (bat_power_up_once() == 0U)
        return 0;

    bat_power_down();
    return 1;
}

/* ADC 掉电：ADDIS 关闭 -> 关内部稳压器。
 * 总线时钟（ADCEN）保持常开——门控它省不了电，却可能引入时序问题。 */
static void bat_power_down(void)
{
    bat_adc_disable();                            /* 置 ADDIS 并等 ADEN 归零 */
    ADC1->CR &= ~ADC_CR_ADVREGEN;                 /* ADEN=0 后才能关内部稳压器 */
}

void BAT_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    s_hw_ready = 0;
    s_meas_ok  = 0;
    s_vbat_filt = 0.0f;

    g_bat_diag.attempts  = 0;
    g_bat_diag.up_fail   = 0;
    g_bat_diag.conv_fail = 0;
    g_bat_diag.raw_last  = 0;

    /* 1. 使能 GPIOA / ADC 时钟，PA1 配置为模拟输入，无上下拉 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    RCC->APB2ENR |= RCC_APB2ENR_ADCEN;

    GPIO_InitStruct.Pin  = BAT_ADC_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BAT_ADC_GPIO_Port, &GPIO_InitStruct);

    s_hw_ready = 1;                               /* 硬件配置完成，可以开始测量 */

#if (BAT_ADC_POWER_CYCLE == 0)
    /* 常开模式：初始化完成后一直保持使能（与改造前完全一致） */
    (void)bat_power_up();
#else
    /* 上电 + 校准做一次自检，然后立刻掉电。
     * 即使这次自检失败也不锁定状态：每次测量都会重新尝试上电。 */
    (void)bat_power_up();
    bat_power_down();
#endif
}

/* 最近一次测量是否有效（1=有效） */
uint8_t BAT_IsReady(void)
{
    return s_meas_ok;
}

uint16_t BAT_ReadRawADC(void)
{
    if (s_hw_ready == 0U)
        return 0;

    ADC1->CR |= ADC_CR_ADSTART;                   /* 启动单次转换 */
    if (bat_wait_flag(ADC_ISR_EOC) != 0U)         /* 等待转换结束 */
    {
        g_bat_diag.conv_fail++;
        ADC1->CR |= ADC_CR_ADSTP;                 /* 超时则停掉转换，避免影响下一次读取 */
        return 0;
    }
    return (uint16_t)ADC1->DR;                    /* 读 DR 自动清除 EOC */
}

float BAT_ReadVoltage(void)
{
    uint32_t sum = 0;
    uint8_t  i;
    float    vbat;

    if (s_hw_ready == 0U)
        return 0.0f;

    g_bat_diag.attempts++;

#if BAT_ADC_POWER_CYCLE
    if (bat_power_up() != 0U)                     /* 上电失败：沿用上次滤波值 */
    {
        g_bat_diag.up_fail++;
        s_meas_ok = 0;
        return s_vbat_filt;
    }
#endif

    /* 多次采样取平均，减少波动 */
    for (i = 0; i < BAT_ADC_SAMPLES; i++)
        sum += BAT_ReadRawADC();

    g_bat_diag.raw_last = (uint16_t)(sum / BAT_ADC_SAMPLES);

#if BAT_ADC_POWER_CYCLE
    bat_power_down();                             /* 测完立刻掉电 */
#endif

    if (sum == 0UL)                               /* 全部转换超时，读数无效 */
    {
        s_meas_ok = 0;
        return s_vbat_filt;
    }
    s_meas_ok = 1;

    /* 中点电压 = 均值 / 满量程 × 参考电压；实际电压 = 中点电压 × 分压比 */
    vbat = ((float)sum / (float)BAT_ADC_SAMPLES) / (float)BAT_ADC_FULL_SCALE
           * (float)BAT_ADC_REF_MV * BAT_DIVIDER_RATIO / 1000.0f;

    /* 一阶低通（系数 1/4）：分压源阻抗 50k 且无滤波电容时抑制读数跳动 */
    if (s_vbat_filt < 0.1f)
        s_vbat_filt = vbat;                       /* 首次测量直接采用 */
    else
        s_vbat_filt += (vbat - s_vbat_filt) * 0.25f;

    return s_vbat_filt;
}

uint8_t BAT_PercentFromVoltage(float voltage)
{
    uint32_t mv = (uint32_t)(voltage * 1000.0f + 0.5f);
    uint32_t i;

    if (mv >= bat_curve[0].mv) return 100;
    if (mv <= bat_curve[BAT_CURVE_LEN - 1].mv) return 0;

    /* 在相邻两个表项之间线性插值 */
    for (i = 1; i < BAT_CURVE_LEN; i++)
    {
        if (mv >= bat_curve[i].mv)
        {
            uint32_t span_mv  = bat_curve[i - 1].mv - bat_curve[i].mv;
            uint32_t span_pct = bat_curve[i - 1].pct - bat_curve[i].pct;

            return (uint8_t)(bat_curve[i].pct
                             + (mv - bat_curve[i].mv) * span_pct / span_mv);
        }
    }
    return 0;
}

uint8_t BAT_ReadPercent(void)
{
    return BAT_PercentFromVoltage(BAT_ReadVoltage());
}
