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
#include "cmsis_os2.h"
#include "gpio.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
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
#define RX_BUFFER_SIZE 64
uint8_t rx_byte;                // 接收单字节
char rx_buffer[RX_BUFFER_SIZE]; // 接收字符串缓冲区
uint8_t rx_index = 0;           // 缓冲区索引
uint8_t rx_complete_flag = 0;   // 接收完成标志
float g_kp = 0.0f;              // 解析出的 kp 值
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */
void Synex_SendFloat(float value);
void SynexTask(void *argument);
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
// 串口//
/**
 * @brief  使用 JustFloat 协议发送一个浮点数（一个通道）
 * @param  value: 要发送的浮点数
 *
 * 协议格式：[float32 小端 4字节] + [帧尾 00 00 80 7F]
 * 帧尾 0x7F800000 是 JustFloat 协议规定的固定值
 */
void Synex_SendFloat(float value)
{
  uint8_t tx_buffer[8];
  const uint8_t tail[4] = {0x00, 0x00, 0x80, 0x7f}; // JustFloat 帧尾

  // 将 float 的 4 字节拷贝到发送缓冲区
  // STM32 是小端模式，直接 memcpy 即可，无需手动调换字节
  memcpy(tx_buffer, &value, 4);
  // 拷贝帧尾
  memcpy(tx_buffer + 4, tail, 4);

  // 通过串口发送 8 个字节
  // 注意：huart1 需根据你实际使用的串口修改
  HAL_UART_Transmit(&huart1, tx_buffer, 8, 100);
}

/**
 * @brief  FreeRTOS 任务：处理串口收到的命令
 * @param  argument: 任务参数（未使用）
 */
void SynexTask(void *argument)
{
  // 启动串口中断接收，接收 1 个字节到 rx_byte
  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);

  for (;;)
  {
    if (rx_complete_flag)
    {
      rx_complete_flag = 0;

      float kp_value = 0.0f;
      // 解析 "kp=%.3f" 格式
      if (sscanf(rx_buffer, "kp=%f", &kp_value) == 1)
      {
        g_kp = kp_value;           // 更新全局 kp 变量
        Synex_SendFloat(kp_value); // 用 JustFloat 协议发回给 synex
      }
      memset(rx_buffer, 0, RX_BUFFER_SIZE);
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // 10ms 轮询一次
  }
}

/* ==================== 第五部分：M3508 电机控制 ==================== */
typedef struct
{
  uint16_t ecd;
  int16_t speed_rpm;
  int16_t given_current;
  uint8_t temperature;
  uint16_t last_ecd;
  int32_t round_cnt;
  int32_t total_ecd;
  float real_angle;
} Motor_Measure_t;

Motor_Measure_t motor1 = {0};

typedef struct
{
  float Kp, Ki, Kd;
  float integral, prev_error;
  float output;
  float out_max;
} PID_Controller;

PID_Controller angle_pid = {1.0f, 0.0f, 0.1f, 0, 0, 0, 8000.0f};
PID_Controller speed_pid = {10.0f, 0.5f, 0.0f, 0, 0, 0, 16000.0f};

float PID_Calc(PID_Controller *pid, float target, float measured)
{
  float error = target - measured;
  pid->integral += error;
  if (pid->integral > pid->out_max)
    pid->integral = pid->out_max;
  if (pid->integral < -pid->out_max)
    pid->integral = -pid->out_max;
  float derivative = error - pid->prev_error;
  pid->prev_error = error;
  float output = pid->Kp * error + pid->Ki * pid->integral + pid->Kd * derivative;
  if (output > pid->out_max)
    output = pid->out_max;
  if (output < -pid->out_max)
    output = -pid->out_max;
  return output;
}

void CAN_Send_Current(CAN_HandleTypeDef *hcan, int16_t m1, int16_t m2, int16_t m3, int16_t m4)
{
  CAN_TxHeaderTypeDef tx_header;
  uint8_t tx_data[8];
  uint32_t tx_mailbox;
  tx_header.StdId = 0x200;
  tx_header.IDE = CAN_ID_STD;
  tx_header.RTR = CAN_RTR_DATA;
  tx_header.DLC = 8;
  tx_data[0] = (uint8_t)(m1 >> 8);
  tx_data[1] = (uint8_t)(m1 & 0xFF);
  tx_data[2] = (uint8_t)(m2 >> 8);
  tx_data[3] = (uint8_t)(m2 & 0xFF);
  tx_data[4] = (uint8_t)(m3 >> 8);
  tx_data[5] = (uint8_t)(m3 & 0xFF);
  tx_data[6] = (uint8_t)(m4 >> 8);
  tx_data[7] = (uint8_t)(m4 & 0xFF);
  HAL_CAN_AddTxMessage(hcan, &tx_header, tx_data, &tx_mailbox);
}

void Motor_CAN_RxCallback(CAN_HandleTypeDef *hcan)
{
  CAN_RxHeaderTypeDef rx_header;
  uint8_t rx_data[8];
  if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK)
    return;

  if (rx_header.StdId == 0x201)
  {
    // 收到电调反馈，LED翻转指示
    HAL_GPIO_TogglePin(GPIOH, GPIO_PIN_13);

    motor1.last_ecd = motor1.ecd;
    motor1.ecd = (uint16_t)(rx_data[0] << 8 | rx_data[1]);
    motor1.speed_rpm = (int16_t)(rx_data[2] << 8 | rx_data[3]);
    motor1.given_current = (int16_t)(rx_data[4] << 8 | rx_data[5]);
    motor1.temperature = rx_data[6];

    if (motor1.ecd - motor1.last_ecd > 4096)
      motor1.round_cnt--;
    else if (motor1.ecd - motor1.last_ecd < -4096)
      motor1.round_cnt++;
    motor1.total_ecd = motor1.ecd + motor1.round_cnt * 8192;
    motor1.real_angle = (float)motor1.total_ecd * (360.0f / 8192.0f);
  }
}

// 任务5：角度闭环控制
void AngleControlTask(void *argument)
{
  vTaskDelay(pdMS_TO_TICKS(100));
  float start_angle = motor1.real_angle;
  float target_seq[3] = {0.0f, 90.0f, -90.0f};
  uint8_t seq_index = 0;
  uint32_t last_time = xTaskGetTickCount();

  for (;;)
  {
    angle_pid.Kp = g_kp;

    float target = start_angle + target_seq[seq_index];
    float angle_output = PID_Calc(&angle_pid, target, motor1.real_angle);
    float speed_feedback = (float)motor1.speed_rpm * (8192.0f / 60.0f);
    float speed_output = PID_Calc(&speed_pid, angle_output, speed_feedback);

    int16_t current_cmd = (int16_t)speed_output;
    if (current_cmd > 16384)
      current_cmd = 16384;
    if (current_cmd < -16384)
      current_cmd = -16384;
    CAN_Send_Current(&hcan1, current_cmd, 0, 0, 0);

    if ((xTaskGetTickCount() - last_time) >= pdMS_TO_TICKS(2000))
    {
      last_time = xTaskGetTickCount();
      seq_index = (seq_index + 1) % 3;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// 任务6：Synex 打印电机数据
void MotorDataTask(void *argument)
{
  vTaskDelay(pdMS_TO_TICKS(100));
  for (;;)
  {
    Synex_SendFloat((float)motor1.given_current);
    Synex_SendFloat(motor1.real_angle);
    Synex_SendFloat((float)motor1.speed_rpm);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
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
  /*HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_3); // PH12 红
  HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_2); // PH11 绿
  HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_1); // PH10 蓝
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);*/
  // 串口接收初始化（启动串口1接收中断）
  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
  // CAN1 启动
  /*HAL_CAN_Start(&hcan1);
  HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);
*/
  // 创建任务（CMSIS_V2）
  /*osThreadNew(LedFlowTask, NULL, NULL);
  osThreadNew(WS2812Task, NULL, NULL);
  osThreadNew(BuzzerTask, NULL, NULL);
*/
  // 新增串口任务
  const osThreadAttr_t synexTask_attributes = {
      .name = "SynexTask",
      .stack_size = 512 * 4,
      .priority = (osPriority_t)osPriorityNormal,
  };
  osThreadNew(SynexTask, NULL, &synexTask_attributes);
  // 电机任务
  /* osThreadNew(AngleControlTask, NULL, NULL);
   osThreadNew(MotorDataTask, NULL, NULL);*/
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
/**
 * @brief  串口接收完成回调函数
 * @param  huart: 串口句柄
 */
void Synex_RxCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1) // 根据实际串口修改
  {
    // 接收到回车符 '\r'，表示一帧结束
    if (rx_byte == '\r')
    {
      rx_buffer[rx_index] = '\0'; // 字符串结尾加上 \0
      rx_complete_flag = 1;       // 置位完成标志
      rx_index = 0;               // 重置索引
    }
    else
    {
      // 普通字符，存入缓冲区
      if (rx_index < RX_BUFFER_SIZE - 1)
      {
        rx_buffer[rx_index++] = rx_byte;
      }
      else
      {
        rx_index = 0; // 缓冲区溢出保护
      }
    }

    // 重新启动接收中断，准备接收下一个字节
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
  }
}
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
