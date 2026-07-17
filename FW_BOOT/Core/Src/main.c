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
#include "lwip.h"

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
IWDG_HandleTypeDef hiwdg;

UART_HandleTypeDef huart3;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
static void MX_IWDG_Init(void);
/* USER CODE BEGIN PFP */
static void BL_JumpToApplication(void);
static void BL_EnterUpdateMode(void);
static void BL_HandleUpdate(void);
static uint8_t BL_ApplyStagingToApp(uint32_t size, uint32_t expectCrc);
static void BL_Rollback(void);
static void BL_StartWatchdog(void);
static void BL_EnsureFactory(void);
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
  MX_USART3_UART_Init();
  MX_USB_OTG_FS_PCD_Init();
  MX_IWDG_Init();
  MX_LWIP_Init();
  /* USER CODE BEGIN 2 */

  /* 최초 부팅: Factory 슬롯이 비어있으면 현재 App을 Factory로 복사해 골든 이미지를 확보한다.
   * (첫 ST-Link 펌웨어가 롤백 복귀처가 됨. 이후로는 절대 안 건드림) */
  BL_EnsureFactory();

  /* Metadata 상태 머신 처리:
   *   PENDING  → Staging 적용 → TRIAL(워치독 켜고 시험 부팅)
   *   TRIAL    → (확인 실패로 재리셋됨) → 롤백(Factory→App)
   *   CONFIRMED→ 메타 클리어(정상 확정) */
  BL_HandleUpdate();

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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
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
  * @brief IWDG Initialization Function
  * @param None
  * @retval None
  */
static void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
  hiwdg.Init.Reload = 500;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */

  /* USER CODE END IWDG_Init 2 */

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
  * @brief  Staging → 실행영역으로 복사·검증한다 (erase + copy + CRC 검증).
  * @return 1: 성공, 0: 실패(Staging 손상 / flash 오류)
  */
static uint8_t BL_ApplyStagingToApp(uint32_t size, uint32_t expectCrc)
{
  if ((size == 0U) || (size > APP_SIZE)) return 0U;

  /* Staging 무결성 (보관 중 손상 확인) */
  if (FlashIf_Crc32((const uint8_t *)STAGING_ADDRESS, size) != expectCrc) return 0U;

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);   /* 적용 중: 파랑 */

  if (FlashIf_EraseRange(APP_ADDRESS, APP_ADDRESS + size - 1U) != FLASH_IF_OK) return 0U;
  if (FlashIf_Write(APP_ADDRESS, (const uint8_t *)STAGING_ADDRESS, (size + 3U) & ~3U) != FLASH_IF_OK) return 0U;
  if (FlashIf_Crc32((const uint8_t *)APP_ADDRESS, size) != expectCrc) return 0U;   /* 복사 결과 검증 */

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
  return 1U;
}

/**
  * @brief  Factory(골든 이미지)를 실행영역으로 복사하여 롤백한다.
  */
static void BL_Rollback(void)
{
  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET);   /* 롤백 중: 빨강 */
  if (FlashIf_EraseRange(APP_ADDRESS, APP_END_ADDRESS) == FLASH_IF_OK)
  {
    FlashIf_Write(APP_ADDRESS, (const uint8_t *)FACTORY_ADDRESS, APP_SIZE);
  }
  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);
}

/**
  * @brief  독립 워치독(IWDG)을 켠다 (약 4초). 시험 부팅 앱이 주기적으로 갱신(pet)하지 않으면 리셋된다.
  * @note   IWDG는 한 번 켜지면 리셋 전까지 못 끈다 → 시험(TRIAL) 부팅에서만 켠다.
  */
static void BL_StartWatchdog(void)
{
  /* CubeMX가 생성한 설정(프리스케일 /256, 리로드 500 ≈ 4초)으로 IWDG를 시작한다.
   * HAL_IWDG_Init()가 내부에서 IWDG를 켜므로, 이 시점(=TRIAL 점프 직전) 이후로는
   * 앱이 주기적으로 갱신(pet)하지 않으면 리셋된다. */
  MX_IWDG_Init();
}

/**
  * @brief  Metadata 상태 머신을 처리한다 (적용/시험/롤백/확인). 부팅마다 호출된다.
  */
static void BL_HandleUpdate(void)
{
  const FwMeta *m = (const FwMeta *)METADATA_ADDRESS;
  FwMeta meta;

  if (m->magic != FW_UPDATE_MAGIC)
  {
    return;   /* 유효 메타 없음 → 정상 부팅 */
  }
  meta = *m;

  switch (meta.state)
  {
    case FW_STATE_PENDING:
      /* 새 펌웨어 적용 성공 → TRIAL로 전환, 워치독 켜고 시험 부팅 */
      if (BL_ApplyStagingToApp(meta.size, meta.crc))
      {
        meta.state    = FW_STATE_TRIAL;
        meta.attempts = 1U;
        FlashIf_WriteMeta(&meta);
        BL_StartWatchdog();
      }
      /* 적용 실패 → PENDING 유지, 다음 부팅 재시도 */
      break;

    case FW_STATE_TRIAL:
      /* 여기 도달 = 직전 시험 부팅이 CONFIRMED에 못 가고 (워치독으로) 리셋됨 */
      if (meta.attempts >= FW_MAX_TRIALS)
      {
        BL_Rollback();          /* 시험 실패 → Factory로 복귀 */
        FlashIf_ClearMeta();
      }
      else
      {
        meta.attempts++;
        FlashIf_WriteMeta(&meta);
        BL_StartWatchdog();     /* 한 번 더 시험 */
      }
      break;

    case FW_STATE_CONFIRMED:
      FlashIf_ClearMeta();      /* 정상 확인됨 → 메타 클리어, 이후 정상 부팅 */
      break;

    default:
      break;                    /* NONE 등 → 정상 부팅 */
  }
}

/**
  * @brief  Factory 슬롯이 비어있으면 현재 App 슬롯 전체를 Factory로 복사한다.
  * @note   최초 1회만 실행(첫 펌웨어를 골든 이미지로 확보). 이후엔 Factory가 유효하므로 즉시 리턴.
  *         App 슬롯 전체(512KB)를 복사하므로 최초 부팅은 수 초 걸릴 수 있다(1회성).
  */
static void BL_EnsureFactory(void)
{
  uint32_t factorySp = *(volatile uint32_t *)FACTORY_ADDRESS;
  uint32_t appSp     = *(volatile uint32_t *)APP_ADDRESS;

  /* Factory가 이미 유효(SP가 RAM 범위)하면 그대로 둔다 */
  if (factorySp >= RAM_START && factorySp <= RAM_END)
  {
    return;
  }

  /* App이 유효하지 않으면 복사할 원본이 없다 */
  if (appSp < RAM_START || appSp > RAM_END)
  {
    return;
  }

  /* 캡처 중 표시: 초록+파랑 켜기 */
  HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);

  /* App 슬롯 전체 → Factory 복사 */
  if (FlashIf_EraseRange(FACTORY_ADDRESS, FACTORY_END_ADDRESS) == FLASH_IF_OK)
  {
    FlashIf_Write(FACTORY_ADDRESS, (const uint8_t *)APP_ADDRESS, APP_SIZE);
  }

  HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);
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
