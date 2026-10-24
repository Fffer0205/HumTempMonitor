/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"
#include <stdio.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#include "oled.h"
#include "aht21.h"
#include "ds18b20.h"
#include "ds1302.h"
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  /* USER CODE BEGIN 2 */
  OLED_Init();			//OLED  
  OLED_Clear();
  OLED_ShowString(20,0,"Hum_Temp_Monitor",16); //
  AHT21_Init();       //AHT21 温湿度传感器初始化
  DS18B20_Init();     //DS18B20 温度传感器初始化
  DS1302_Init();      //DS1302 实时时钟初始化
  DS1302_StartClock();//确保时钟在走

  /* 首次上电时写入一个初始时间（可按需修改）：
   * 2026-10-24 周六 12:00:00
   * 若外部晶振/后备电池正常，再次上电后时间会持续累加，此句可注释掉 */
  {
    DS1302_Time_t init_time = {26, 10, 24, 7, 12, 0, 0};
    DS1302_SetTime(&init_time);
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  char buf[12];
  float humidity = 0.0f, temperature = 0.0f, ds_temp = 0.0f;
  DS1302_Time_t edit;
  DS1302_GetTime(&edit);        /* 首次初始化编辑副本，避免未定义值 */
  uint8_t page = 0;              /* 0=温湿度页, 1=时间页 */
  uint32_t pageTick = HAL_GetTick();   /* 自动翻页时刻 */
  const uint32_t PAGE_MS = 5000;      /* 空闲时每页停留 5 秒 */
  const uint32_t EDIT_TIMEOUT_MS = 10000; /* 编辑时 10 秒无操作自动恢复翻页 */

  /* 星期英文名：index = (week-1)，1=周日...7=周六 */
  static const char *week_name[] = {"Sunday","Monday","Tuesday","Wednesday",
                                    "Thursday","Friday","Saturday"};

  /* 时间编辑状态 */
  uint8_t key1_last = 1, key2_last = 1;  /* 上一次按键电平，用于沿检测 */
  uint8_t edit_idx = 0;                  /* 0=不编辑；1..6=选择年/月/日/时/分/秒 */
  uint32_t lastActivity = 0;             /* 最近一次按键活动时刻，用于编辑锁定 */
  const char *edit_hint[] = {"", "Year", "Month", "Day", "Hour", "Min", "Sec"};
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();

    /* 编辑锁定：编辑中若 10 秒无任何按键，自动保存并退出，恢复自动翻页 */
    if (edit_idx != 0 && now - lastActivity >= EDIT_TIMEOUT_MS)
    {
      edit_idx = 0;
      DS1302_SetTime(&edit);
    }

    /* 自动翻页：仅在非编辑状态下进行，编辑中锁定当前页不翻页 */
    if (edit_idx == 0 && now - pageTick >= PAGE_MS)
    {
      pageTick = now;
      page = !page;              /* 0/1 交替 */
      OLED_Clear();
      if (page == 0)
        OLED_ShowString(20, 0, "Hum_Temp_Monitor", 16);
    }

    /* ---- 按键处理（低电平有效，沿检测） ---- */
    if (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == 0 && key1_last == 1)
    {
      lastActivity = now;        /* 刷新活动时刻，锁定编辑 */
      if (page != 1)             /* 从任意页按下，强制进入时间页 */
      {
        page = 1;
        pageTick = now;
        OLED_Clear();
      }
      edit_idx++;                /* KEY1：切换编辑字段/退出保存 */
      if (edit_idx > 6)
        edit_idx = 0;
      if (edit_idx == 0)
      {
        DS1302_SetTime(&edit);   /* 循环回 0：保存退出 */
        OLED_Clear();
        OLED_ShowString(20, 0, "Hum_Temp_Monitor", 16);
      }
    }
    key1_last = (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == 0) ? 0 : 1;

    if (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == 0 && key2_last == 1)
    {
      lastActivity = now;        /* 刷新活动时刻 */
      if (page != 1)
      {
        page = 1;
        pageTick = now;
        OLED_Clear();
      }
      if (edit_idx == 0)         /* 未编辑时先进入编辑（选中小时） */
        edit_idx = 4;
      else
      {
        /* KEY2：当前被选字段 +1 */
        switch (edit_idx)
        {
          case 1: edit.year   = (edit.year + 1) % 100;    break;
          case 2: edit.month  = (edit.month % 12) + 1;    break;
          case 3: edit.day    = (edit.day % 31) + 1;      break;
          case 4: edit.hour   = (edit.hour % 24) + 1;     break;
          case 5: edit.minute = (edit.minute % 60) + 1;   break;
          case 6: edit.second = (edit.second % 60) + 1;   break;
        }
      }
    }
    key2_last = (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == 0) ? 0 : 1;

    if (page == 0)
    {
      /* 第 1 页：温湿度 + DS18B20（16 字号布局） */
      if (AHT21_ReadData(&humidity, &temperature) == 0)
      {
        OLED_ShowString(0, 2, "Temp:", 16);
        sprintf(buf, "%5.1f", temperature);
        OLED_ShowString(56, 2, (uint8_t *)buf, 16);
        OLED_ShowString(100, 2, " C", 16);

        OLED_ShowString(0, 4, "Humi:", 16);
        sprintf(buf, "%5.1f", humidity);
        OLED_ShowString(56, 4, (uint8_t *)buf, 16);
        OLED_ShowString(100, 4, " %", 16);
      }

      if (DS18B20_GetTemp(&ds_temp) == 0)
      {
        OLED_ShowString(0, 6, "DS18:", 16);
        sprintf(buf, "%5.1f", ds_temp);
        OLED_ShowString(56, 6, (uint8_t *)buf, 16);
        OLED_ShowString(100, 6, " C", 16);
      }
    }
    else
    {
      /* 第 2 页：时间编辑/显示。用 16 大字号直接显示日期和时间，
       * 编辑时直接在原数字上修改，不做缩小。无顶部标题，全屏用于内容。
       * 仅在非编辑状态才从 DS1302 刷新 edit，
       * 避免覆盖编辑中 KEY2 已修改的值。 */
      if (edit_idx == 0)
        DS1302_GetTime(&edit);

      /* Date: 2026-10-24  */
      OLED_ShowString(0, 0, "Date:", 16);
      sprintf(buf, "20%02d-%02d-%02d", edit.year, edit.month, edit.day);
      OLED_ShowString(40, 0, (uint8_t *)buf, 16);

      /* Week: 星期几英文 */
      OLED_ShowString(0, 2, "Week:", 16);
      if (edit.week >= 1 && edit.week <= 7)
        OLED_ShowString(40, 2, (uint8_t *)week_name[edit.week - 1], 16);

      /* Time: 12:34:56 */
      OLED_ShowString(0, 4, "Time:", 16);
      sprintf(buf, "%02d:%02d:%02d", edit.hour, edit.minute, edit.second);
      OLED_ShowString(40, 4, (uint8_t *)buf, 16);

      /* 编辑状态提示（8 字号，位于最底部一行，不挤占内容） */
      if (edit_idx != 0)
      {
        OLED_ShowString(0, 6, "Edit>", 8);
        OLED_ShowString(40, 6, (uint8_t *)edit_hint[edit_idx], 8);
      }
    }

    HAL_Delay(100);   /* 10Hz 刷新 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLLMUL_4;
  RCC_OscInitStruct.PLL.PLLDIV = RCC_PLLDIV_2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */