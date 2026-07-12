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
#include "string.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "flash_if.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* APP_ADDRESS(0x0801_0000)는 flash_if.h에 정의됨 */

/* RAM 범위: 앱의 초기 스택 포인터가 이 안에 있어야 "유효한 앱"으로 판단 (RAM 192KB) */
#define RAM_START        0x20000000U
#define RAM_END          0x20030000U   /* 0x20000000 + 192KB */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

ETH_TxPacketConfig TxConfig;
ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

ETH_HandleTypeDef heth;

UART_HandleTypeDef huart3;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ETH_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
/* USER CODE BEGIN PFP */
static void BL_JumpToApplication(void);
static void BL_EnterUpdateMode(void);
static void BL_ApplyPendingUpdate(void);
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
  MX_ETH_Init();
  MX_USART3_UART_Init();
  MX_USB_OTG_FS_PCD_Init();
  /* USER CODE BEGIN 2 */

  /* 재부팅 시 '업데이트 대기' 플래그가 있으면 Staging을 실행영역에 복사(적용)한다.
   * 앱이 새 펌웨어를 Staging에 받고 재부팅하면 여기서 실제 적용이 일어난다. */
  BL_ApplyPendingUpdate();

  /* USER 버튼(파랑 B1, PC13)을 누른 채 리셋하면 '업데이트 모드'로 진입한다.
   * (나중에는 앱이 세팅한 '업데이트 플래그'로도 진입시킬 예정)
   * 업데이트 모드에서는 앱으로 점프하지 않고 UART로 펌웨어 수신을 대기한다. */
  if (HAL_GPIO_ReadPin(USER_Btn_GPIO_Port, USER_Btn_Pin) == GPIO_PIN_SET)
  {
    BL_EnterUpdateMode();   /* 돌아오지 않음 */
  }

  /* [진단용] 부트로더가 실행됐음을 확인 — 점프 시도 '전'에 LD3를 3회 빠르게 깜빡인다.
   * · 이 3회 깜빡임이 보이면       → 부트로더는 정상 실행됨
   * · 이후 LED가 멈추거나 바뀌면    → 앱으로 점프한 것(앱 영역에 데이터 있음)
   * · 이후 LD3가 계속 천천히 깜빡이면 → 유효 앱 없음(정상 부트로더 모드)
   * · 3회 깜빡임조차 안 보이면      → 부트로더 미실행(빌드/플래시/클럭 문제) */
  for (int i = 0; i < 6; i++)
  {
    HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
    HAL_Delay(100);
  }

  /* 부트로더 진입 직후: 유효한 앱이 있으면 앱으로 점프한다.
   * 유효한 앱이 없으면 아래 while 루프로 떨어져 LED로 '부트로더 모드'를 표시한다. */
  BL_JumpToApplication();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* === 부트로더 모드 표시 ===
     * 여기 도달했다 = 유효한 앱이 없어 점프하지 못하고 부트로더에 머무는 중.
     * LD3(빨강)를 천천히 깜빡여 '부트로더 모드'임을 알린다. */
    HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
    HAL_Delay(500);
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
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ETH Initialization Function
  * @param None
  * @retval None
  */
static void MX_ETH_Init(void)
{

  /* USER CODE BEGIN ETH_Init 0 */

  /* USER CODE END ETH_Init 0 */

   static uint8_t MACAddr[6];

  /* USER CODE BEGIN ETH_Init 1 */

  /* USER CODE END ETH_Init 1 */
  heth.Instance = ETH;
  MACAddr[0] = 0x00;
  MACAddr[1] = 0x80;
  MACAddr[2] = 0xE1;
  MACAddr[3] = 0x00;
  MACAddr[4] = 0x00;
  MACAddr[5] = 0x00;
  heth.Init.MACAddr = &MACAddr[0];
  heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
  heth.Init.TxDesc = DMATxDscrTab;
  heth.Init.RxDesc = DMARxDscrTab;
  heth.Init.RxBuffLen = 1524;

  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

  if (HAL_ETH_Init(&heth) != HAL_OK)
  {
    Error_Handler();
  }

  memset(&TxConfig, 0 , sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;
  /* USER CODE BEGIN ETH_Init 2 */

  /* USER CODE END ETH_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief USB_OTG_FS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_FS_PCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_FS_Init 0 */

  /* USER CODE END USB_OTG_FS_Init 0 */

  /* USER CODE BEGIN USB_OTG_FS_Init 1 */

  /* USER CODE END USB_OTG_FS_Init 1 */
  hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
  hpcd_USB_OTG_FS.Init.dev_endpoints = 4;
  hpcd_USB_OTG_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_FS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_OTG_FS.Init.Sof_enable = ENABLE;
  hpcd_USB_OTG_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.vbus_sensing_enable = ENABLE;
  hpcd_USB_OTG_FS.Init.use_dedicated_ep1 = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_FS_Init 2 */

  /* USER CODE END USB_OTG_FS_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD1_Pin|LD3_Pin|LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USB_PowerSwitchOn_GPIO_Port, USB_PowerSwitchOn_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : USER_Btn_Pin */
  GPIO_InitStruct.Pin = USER_Btn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USER_Btn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD1_Pin LD3_Pin LD2_Pin */
  GPIO_InitStruct.Pin = LD1_Pin|LD3_Pin|LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = USB_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USB_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_OverCurrent_Pin */
  GPIO_InitStruct.Pin = USB_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
  * @brief  APP_ADDRESS에 유효한 애플리케이션이 있으면 그곳으로 점프한다.
  *         유효한 앱이 없으면 그대로 리턴하여 부트로더에 머문다.
  * @note   리셋 시 하드웨어가 하는 동작(SP 로드 -> Reset_Handler 점프)을
  *         소프트웨어로 재현한다.
  */
static void BL_JumpToApplication(void)
{
  /* 벡터 테이블의 맨 앞 2개 워드 = [초기 스택 포인터][Reset_Handler 주소] */
  uint32_t appStack = *(volatile uint32_t *)(APP_ADDRESS);
  uint32_t appEntry = *(volatile uint32_t *)(APP_ADDRESS + 4U);

  /* --- 앱 유효성 검사 ---
   * 앱이 아직 안 구워졌으면 Flash가 지워진 상태(0xFFFFFFFF)라 SP가 RAM 범위를 벗어난다.
   * 초기 스택 포인터가 RAM(0x2000_0000~) 안에 있어야 정상 앱으로 판단한다. */
  if (appStack < RAM_START || appStack > RAM_END)
  {
    return;   /* 유효한 앱 없음 -> 부트로더에 머무름 */
  }

  /* --- 인계(handoff) 전 정리 ---
   * 부트로더가 켜둔 주변장치/인터럽트/SysTick을 끄지 않으면
   * 앱 초기화 도중 남은 인터럽트가 튀어 오동작한다. */
  HAL_RCC_DeInit();
  HAL_DeInit();

  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

  __disable_irq();

  /* 앱의 벡터 테이블로 전환 (★ 앱도 자기 쪽에서 다시 설정하지만 여기서도 맞춰준다) */
  SCB->VTOR = APP_ADDRESS;

  /* 스택 포인터를 앱 것으로 교체 */
  __set_MSP(appStack);

  /* 인터럽트 다시 켜기 (★ 정상 리셋 상태 재현)
   * 안 켜면 앱의 SysTick 인터럽트가 안 울려 HAL_Delay()가 영원히 멈춘다. */
  __enable_irq();

  ((void (*)(void))appEntry)();   /* 여기서 앱이 시작되며, 돌아오지 않는다 */
}

/**
  * @brief  업데이트 모드: 앱으로 점프하지 않고 UART(USART3)로 데이터 수신을 대기한다.
  * @note   [3a 단계] 아직 프로토콜은 없고, UART 링크 검증용이다.
  *         · 진입 시 배너 문자열 송신 + LD2(파랑) 켜기
  *         · 1초간 수신 없으면 '.' 하트비트 송신 (수신 전용 뷰어로도 링크 확인)
  *         · 바이트 수신 시 그대로 echo + LD3(빨강) 토글
  *         이후 3b에서 프로토콜 파서 + FlashIf_Write() 로 확장한다. (돌아오지 않음)
  */
static void BL_EnterUpdateMode(void)
{
  const char banner[] = "\r\n[FW_BOOT] Update mode ready (USART3, 115200 8N1)\r\n";
  uint8_t rxByte;

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);   /* 파랑: 업데이트 모드 */
  HAL_UART_Transmit(&huart3, (uint8_t *)banner, sizeof(banner) - 1U, HAL_MAX_DELAY);

  while (1)
  {
    /* USART3으로 1바이트 수신 (1초 타임아웃) */
    if (HAL_UART_Receive(&huart3, &rxByte, 1U, 1000U) == HAL_OK)
    {
      HAL_UART_Transmit(&huart3, &rxByte, 1U, HAL_MAX_DELAY);   /* echo */
      HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);              /* 수신 표시 */
    }
    else
    {
      /* 1초간 수신 없음 → 하트비트 '.' 송신 */
      HAL_UART_Transmit(&huart3, (uint8_t *)".", 1U, HAL_MAX_DELAY);
    }
  }
}

/**
  * @brief  재부팅 시 '업데이트 대기' 플래그(메타데이터)가 있으면
  *         Staging → 실행영역으로 복사하여 새 펌웨어를 적용한다.
  * @note   메타데이터가 없으면 즉시 리턴(정상 부팅).
  *         복사 실패 시 플래그를 유지해 다음 부팅에 재시도한다.
  */
static void BL_ApplyPendingUpdate(void)
{
  const FwMeta *m = (const FwMeta *)METADATA_ADDRESS;

  if (m->magic != FW_UPDATE_MAGIC)
  {
    return;   /* 대기 중인 업데이트 없음 → 정상 부팅 */
  }

  uint32_t size = m->size;
  if ((size == 0U) || (size > APP_SIZE))
  {
    FlashIf_ClearMeta();   /* 무효한 메타 → 클리어 후 정상 부팅 */
    return;
  }

  /* 적용 중 표시: 파랑 LED 켜기 (복사 동안 CPU는 잠깐 멈춰있을 수 있음) */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);

  /* 1) 실행영역에서 필요한 만큼 지우기 */
  if (FlashIf_EraseRange(APP_ADDRESS, APP_ADDRESS + size - 1U) != FLASH_IF_OK)
  {
    return;   /* 실패: 플래그 유지 → 다음 부팅 재시도 */
  }

  /* 2) Staging → 실행영역 복사 (Staging은 메모리 매핑이라 포인터로 바로 읽힘) */
  if (FlashIf_Write(APP_ADDRESS, (const uint8_t *)STAGING_ADDRESS, (size + 3U) & ~3U) != FLASH_IF_OK)
  {
    return;   /* 실패: 플래그 유지 → 다음 부팅 재시도 */
  }

  /* 3) 성공: 플래그 클리어 (한 번만 적용) */
  FlashIf_ClearMeta();

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
}

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
