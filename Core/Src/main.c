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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include "app_power.h"
#include "app_sensor.h"
#include "app_time.h"
#include "app_ui.h"
#include "app_imu.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_DRAW_PERIOD_MS   200UL   /* 亮屏时的重绘周期（5Hz） */
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
  OLED_Init();            /* OLED 初始化（清屏 + 开显示） */
  OLED_Clear();

  AppPower_Init();        /* 按键 EXTI、显示状态、STOP 相关初始化 */
  AppSensor_Init();       /* AHT21 / DS18B20 / 电池 ADC */
  AppTime_Init();         /* DS1302 初始化、处理时钟停走、读入缓存 */
  AppIMU_Init();          /* ADXL345：抬手检测 + INT2 唤醒源 */

  AppPower_DisplayOn();   /* 上电先点亮屏幕 */
  AppIMU_ScreenOn();      /* 亮屏期间关掉活动中断 */
  AppSensor_ForceSample(HAL_GetTick());
  AppTime_Invalidate();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t last_draw = 0;
  uint8_t  need_draw = 1;

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();

    /* ---- 熄屏：进入 STOP，等按键或加速度计把 MCU 叫醒 ---- */
    if (!AppPower_IsDisplayOn())
    {
      uint8_t woke = 0;

      AppIMU_PrepareSleep();          /* 降速到 25Hz + 打开活动中断 */

#if (APP_STOP_WHILE_DEBUGGED == 0)
      if (AppPower_DebuggerAttached())
      {
        /* 台架模式：调试器连着时进 STOP 会让 SWD 掉线，就不睡了，
         * 直接轮询动作判定。功耗较高，只用于带调试器验证算法。 */
        if (AppIMU_DetectWake() || AppPower_PollWakeKey())
          woke = 1;
        else
          HAL_Delay(100);
      }
      else
#endif
      {
        uint8_t src = AppPower_EnterStop();  /* 返回时时钟已恢复，屏幕仍熄灭 */

        if (src & APP_WAKE_KEY)
          woke = 1;               /* 按键唤醒：直接亮屏，这一下不进入编辑 */
        else if ((src & APP_WAKE_IMU) && AppIMU_DetectWake())
          woke = 1;               /* 活动中断之后还要确认是不是“要看的动作” */
      }

      if (!woke)
        continue;                 /* 不是操作：回去接着睡 */

      AppPower_DisplayOn();
      AppIMU_ScreenOn();
      now = HAL_GetTick();

      /* STOP 期间 SysTick 停止，tick 不会前进：
       * 按“现在”重新对齐采样节拍并强制重读时钟，否则会以为没到采样时间 */
      AppSensor_ForceSample(now);
      AppTime_Invalidate();
      need_draw = 1;
    }

    /* ---- 按键 ----
     * 亮屏时：把按下沿交给时间编辑器，自己处理不判断具体语义 */
    {
      uint8_t events = AppPower_PollKeys(now);
      if (events != 0)
      {
        AppTime_HandleKey(events, now);
        need_draw = 1;
      }
    }

    /* ---- 亮屏：刷新时钟/传感器，并按 5Hz 重绘 ---- */
    AppTime_Update(now);
    AppSensor_Update(now);

    if (need_draw || (uint32_t)(now - last_draw) >= APP_DRAW_PERIOD_MS)
    {
      last_draw = now;
      need_draw = 0;
      AppUI_Draw();
    }

    /* ---- 15 秒无按键：熄屏，下一圈进入 STOP ---- */
    if (AppPower_IdleExpired(now))
      AppPower_DisplayOff();

    HAL_Delay(20);
    /* USER CODE END 3 */
  }
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

