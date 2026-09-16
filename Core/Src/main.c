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
#include "can.h"
#include "cmsis_os.h"
#include "gpio.h"
#include "spi.h"
#include "stm32f407xx.h"
#include "tim.h"
#include "usart.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "task.h"
#include <stdlib.h>
#include <string.h>

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
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// ---------------- WS2812 发送函数 ----------------
void WS2812_Send(uint8_t r, uint8_t g, uint8_t b)
{
  uint8_t buf[24];
  uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
  for (int i = 0; i < 24; i++)
  {
    buf[i] = (grb & (1 << (23 - i))) ? 0xF0 : 0xC0;
  }
  HAL_SPI_Transmit(&hspi2, buf, 24, 100);
  uint8_t reset[40] = {0};
  HAL_SPI_Transmit(&hspi2, reset, 40, 100);
}

// ---------------- 任务1：三色LED流水灯 ----------------
// 共阳极，低电平点亮：0 = 亮，999 = 灭
void LedFlowTask(void *argument)
{
  for (;;)
  {
    // 红灯亮 (PH12, TIM5_CH3)
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_3, 0);
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_2, 999);
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_1, 999);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 绿灯亮 (PH11, TIM5_CH2)
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_3, 999);
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_2, 0);
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_1, 999);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 蓝灯亮 (PH10, TIM5_CH1)
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_3, 999);
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_2, 999);
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_1, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ---------------- 任务2：WS2812 流水变色 ----------------
void WS2812Task(void *argument)
{
  for (;;)
  {
    WS2812_Send(255, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    WS2812_Send(0, 255, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    WS2812_Send(0, 0, 255);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
// ---------------- 蜂鸣器音调播放函数 ----------------
// freq_hz: 频率(Hz)，0 表示静音
// duration_ms: 持续时间(ms)
void Buzzer_PlayTone(uint32_t freq_hz, uint32_t duration_ms)
{
  if (freq_hz == 0)
  {
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return;
  }
  uint32_t period = 1000000 / freq_hz;
  __HAL_TIM_SET_AUTORELOAD(&htim4, period - 1);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, period / 2);
  vTaskDelay(pdMS_TO_TICKS(duration_ms));
}

// ---------------- 任务3：蜂鸣器音调（响两次后停止）----------------
void BuzzerTask(void *argument)
{
  // ============ 1. 上电/复位响一声（5分）============
  Buzzer_PlayTone(1000, 200); // 1kHz 响 200ms
  Buzzer_PlayTone(0, 200);    // 静音 200ms

  vTaskDelay(pdMS_TO_TICKS(1000)); // 间隔1秒

  // ============ 2. 响两次报错音调后停止 ============
  for (int i = 0; i < 2; i++)
  {
    // 车辆识别音调（上行三音）
    Buzzer_PlayTone(523, 200); // Do
    Buzzer_PlayTone(659, 200); // Mi
    Buzzer_PlayTone(784, 200); // Sol
    Buzzer_PlayTone(0, 600);   // 静音

    // 报错音调1（双短促高音）
    Buzzer_PlayTone(880, 150); // 高音 La
    Buzzer_PlayTone(0, 100);
    Buzzer_PlayTone(880, 150); // 高音 La
    Buzzer_PlayTone(0, 600);

    // 报错音调2（下行三音）
    Buzzer_PlayTone(784, 200); // Sol
    Buzzer_PlayTone(659, 200); // Mi
    Buzzer_PlayTone(523, 200); // Do
    Buzzer_PlayTone(0, 800);   // 静音
  }

  // ============ 3. 关闭蜂鸣器，任务自我删除 ============
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0); // 确保 PWM 占空比为0
  vTaskDelete(NULL);                               // 删除自身，停止运行
}

/*串口*/
#define JUSTFLOAT_TAIL 0x7F800000

volatile float g_kp = 0.0f;
static uint8_t uart_rx_byte;
static uint8_t uart_rx_buf[64];
static uint16_t uart_rx_index = 0;

// 发送一个 JustFloat 帧（单通道）
void Synex_SendFloat(float value)
{
  uint8_t tx_buf[8];
  uint32_t tail = JUSTFLOAT_TAIL;
  memcpy(tx_buf, &value, 4);
  memcpy(tx_buf + 4, &tail, 4);
  HAL_UART_Transmit(&huart1, tx_buf, 8, 100);
}

// 串口接收中断回调（只收数据，不做其他事）
void Synex_RxCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    if (uart_rx_byte == '\r' || uart_rx_byte == '\n')
    {
      uart_rx_buf[uart_rx_index] = '\0';
      if (uart_rx_index > 0)
      {
        char *p = strstr((char *)uart_rx_buf, "kp=");
        if (p != NULL)
          g_kp = atof(p + 3);
      }
      uart_rx_index = 0;
      memset(uart_rx_buf, 0, 64);
    }
    else
    {
      if (uart_rx_index < 63)
      {
        uart_rx_buf[uart_rx_index++] = uart_rx_byte;
      }
    }
    // 重新启动接收中断
    HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
  }
}

// 串口发送任务（只发数据，不做其他事）
void SynexTask(void *argument)
{
  vTaskDelay(pdMS_TO_TICKS(100)); // 等系统稳定
  for (;;)
  {
    Synex_SendFloat(g_kp);         // 把 g_kp 发回 Synex
    vTaskDelay(pdMS_TO_TICKS(10)); // 100Hz
  }
}
/* ==================== 串口代码结束 ==================== */
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
  MX_TIM4_Init();
  MX_SPI2_Init();
  MX_TIM5_Init();
  MX_CAN1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  // 启动TIM5的三路PWM
  HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_3); // PH12 红
  HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_2); // PH11 绿
  HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_1); // PH10 蓝
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);

  // 串口接收初始化（启动串口1接收中断）
  HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
  // 创建任务（CMSIS_V2）
  osThreadNew(LedFlowTask, NULL, NULL);
  osThreadNew(WS2812Task, NULL, NULL);
  osThreadNew(BuzzerTask, NULL, NULL);

  // 新增串口任务
  osThreadNew(SynexTask, NULL, NULL);
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize(); /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    vTaskDelay(pdMS_TO_TICKS(1000));
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
 * @brief  Period elapsed callback in non blocking mode
 * @note   This function is called  when TIM1 interrupt took place, inside
 * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
 * a global variable "uwTick" used as application time base.
 * @param  htim : TIM handle
 * @retval None
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
