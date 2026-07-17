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

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

ETH_TxPacketConfig TxConfig;
ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

ETH_HandleTypeDef heth;

IWDG_HandleTypeDef hiwdg;

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
static void MX_IWDG_Init(void);
/* USER CODE BEGIN PFP */
static void App_EnterDownloadMode(void);
static uint16_t App_Crc16(const uint8_t *data, uint32_t len);
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

  /* === 벡터 테이블 재배치 (★필수) ===
   * 이 앱은 0x0802_0000(실행영역)에 위치하므로, 인터럽트 벡터 테이블 위치를 CPU에 알려준다.
   * 이 줄이 없으면 인터럽트 발생 시 CPU가 부트로더(0x0800_0000)의 핸들러로 점프해 오동작한다. */
  SCB->VTOR = 0x08020000U;

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
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */

  /* 시험 부팅(TRIAL) 중이면 자가진단 후 '정상 확인(CONFIRMED)'을 기록한다.
   * 여기까지 도달 = 앱이 부팅·초기화에 성공했다는 뜻. (실제 제품은 센서/통신 점검 후 확인)
   * CONFIRMED를 못 남기고 앱이 멈추면 → 워치독이 리셋 → 부트로더가 Factory로 롤백. */
  {
    const FwMeta *m = (const FwMeta *)METADATA_ADDRESS;
    if (m->magic == FW_UPDATE_MAGIC && m->state == FW_STATE_TRIAL)
    {
      IWDG->KR = 0x0000AAAAU;        /* 플래시 쓰기 전에 워치독 갱신(여유 확보) */
      FwMeta meta = *m;
      meta.state = FW_STATE_CONFIRMED;
      FlashIf_WriteMeta(&meta);
    }
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* 워치독 갱신(pet): TRIAL 부팅 시 부트로더가 IWDG를 켜둔다. 이 루프가 계속 돌면
     * 리셋 안 됨. 앱이 멈추면 갱신이 끊겨 리셋 → 롤백. (IWDG 미가동 시엔 무해한 no-op) */
    IWDG->KR = 0x0000AAAAU;

    /* === 앱 정상 모드 ===
     * · LD1(초록)을 0.5초마다 토글 = 앱 정상 실행 중(하트비트)
     * · 동시에 UART로 "FWUPDATE" 명령을 감시 → 수신되면 다운로드 모드로 전환
     * (베어메탈: 짧은 타임아웃 폴링으로 통신 감시와 LED를 함께 처리) */
    static const char FW_UPDATE_CMD[] = "FWUPDATE";
    static uint8_t  cmdIdx   = 0U;
    static uint32_t lastBlink = 0U;
    uint8_t ch;

    /* UART 1바이트 폴링 (10ms 타임아웃) */
    if (HAL_UART_Receive(&huart3, &ch, 1U, 10U) == HAL_OK)
    {
      if (ch == (uint8_t)FW_UPDATE_CMD[cmdIdx])
      {
        cmdIdx++;
        if (FW_UPDATE_CMD[cmdIdx] == '\0')   /* "FWUPDATE" 전체 매칭 */
        {
          App_EnterDownloadMode();           /* 다운로드 모드 진입 (현재는 돌아오지 않음) */
          cmdIdx = 0U;
        }
      }
      else
      {
        /* 불일치: 방금 글자가 명령 첫 글자면 1, 아니면 0으로 리셋 */
        cmdIdx = (ch == (uint8_t)FW_UPDATE_CMD[0]) ? 1U : 0U;
      }
    }

    /* 하트비트: 0.5초마다 초록 LED 토글 */
    if ((HAL_GetTick() - lastBlink) >= 100U)
    {
      HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
      lastBlink = HAL_GetTick();
    }
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
  * @brief  CRC16-CCITT (poly=0x1021, init=0xFFFF, 반사 없음). 청크 무결성 검사용.
  * @note   PC(C#) 측 Crc16Ccitt 와 동일 파라미터여야 값이 일치한다.
  */
static uint16_t App_Crc16(const uint8_t *data, uint32_t len)
{
  uint16_t crc = 0xFFFFU;
  for (uint32_t i = 0U; i < len; i++)
  {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);
    for (uint8_t b = 0U; b < 8U; b++)
    {
      crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

/**
  * @brief  "FWUPDATE" 명령 수신 시 진입하는 다운로드 모드. [4b 단계]
  * @note   프로토콜:
  *           PC → "READY" 응답을 보고 [전체크기 4B, LE] 전송
  *           MCU → Staging 지우기 → ACK/NACK
  *           PC → 256B 청크 반복 전송, MCU는 청크마다 Staging 기록 후 ACK
  *           MCU → 전부 받으면 "DONE" 전송
  *         [5단계 예정] DONE 후 플래그 세팅 + 재부팅. 지금은 파랑 켠 채 멈춤.
  */
static void App_EnterDownloadMode(void)
{
  const char ready[]  = "READY\r\n";
  const char done[]   = "DONE\r\n";
  const char crcerr[] = "CRCERR\r\n";
  uint8_t  ack = 0x79U, nack = 0x1FU;
  uint8_t  buf[256];
  uint8_t  hdr[8];
  uint32_t total, expectCrc, addr, remaining;

  HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);   /* 초록 끔 */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);     /* 파랑: 다운로드 모드 */
  HAL_UART_Transmit(&huart3, (uint8_t *)ready, sizeof(ready) - 1U, HAL_MAX_DELAY);

  /* 1) 헤더 수신: [전체크기 4B][CRC32 4B] (little-endian) */
  if (HAL_UART_Receive(&huart3, hdr, 8U, HAL_MAX_DELAY) != HAL_OK) goto stop;
  total = (uint32_t)hdr[0]
        | ((uint32_t)hdr[1] << 8)
        | ((uint32_t)hdr[2] << 16)
        | ((uint32_t)hdr[3] << 24);
  expectCrc = (uint32_t)hdr[4]
            | ((uint32_t)hdr[5] << 8)
            | ((uint32_t)hdr[6] << 16)
            | ((uint32_t)hdr[7] << 24);

  /* 크기 유효성 검사 */
  if ((total == 0U) || (total > STAGING_SIZE))
  {
    HAL_UART_Transmit(&huart3, &nack, 1U, HAL_MAX_DELAY);
    goto stop;
  }

  /* 2) Staging 영역에서 필요한 만큼 지우기 */
  IWDG->KR = 0x0000AAAAU;   /* 지우기 직전 워치독 갱신 (큰 erase가 타임아웃 넘기지 않도록) */
  if (FlashIf_EraseRange(STAGING_ADDRESS, STAGING_ADDRESS + total - 1U) != FLASH_IF_OK)
  {
    HAL_UART_Transmit(&huart3, &nack, 1U, HAL_MAX_DELAY);
    goto stop;
  }
  HAL_UART_Transmit(&huart3, &ack, 1U, HAL_MAX_DELAY);   /* 지우기 완료 ACK */

  /* 3) 청크 수신 → CRC16 검증 → Staging 기록.
   *    각 청크: [데이터 n바이트][CRC16 2바이트, LE]
   *    CRC 불일치 → NACK (PC가 같은 청크 재전송) / 정상 → 기록 후 ACK */
  addr = STAGING_ADDRESS;
  remaining = total;
  while (remaining > 0U)
  {
    uint32_t n = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
    uint8_t  crcBuf[2];
    uint8_t  retries = 0U;

    IWDG->KR = 0x0000AAAAU;   /* 청크마다 워치독 갱신 (긴 전송 중 리셋 방지) */

    /* 이 청크가 CRC를 통과할 때까지 재수신 */
    for (;;)
    {
      if (HAL_UART_Receive(&huart3, buf, (uint16_t)n, HAL_MAX_DELAY) != HAL_OK) goto stop;
      if (HAL_UART_Receive(&huart3, crcBuf, 2U, HAL_MAX_DELAY) != HAL_OK) goto stop;

      uint16_t rxCrc = (uint16_t)crcBuf[0] | ((uint16_t)crcBuf[1] << 8);
      if (App_Crc16(buf, n) == rxCrc)
      {
        break;   /* 데이터 정상 → 기록으로 진행 */
      }

      /* 깨짐 → NACK, PC가 같은 청크를 재전송한다 */
      HAL_UART_Transmit(&huart3, &nack, 1U, HAL_MAX_DELAY);
      if (++retries >= 5U) goto stop;   /* 같은 청크 5회 실패 → 포기 */
    }

    /* Flash는 4바이트 단위 → 꼬리는 0xFF로 패딩 후 기록 */
    uint32_t nAligned = (n + 3U) & ~3U;
    for (uint32_t i = n; i < nAligned; i++) buf[i] = 0xFFU;
    if (FlashIf_Write(addr, buf, nAligned) != FLASH_IF_OK)
    {
      HAL_UART_Transmit(&huart3, &nack, 1U, HAL_MAX_DELAY);
      goto stop;
    }
    HAL_UART_Transmit(&huart3, &ack, 1U, HAL_MAX_DELAY);   /* 청크 확정 ACK */

    addr += n;
    remaining -= n;
    HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);            /* 진행 표시 */
  }

  /* 4) 수신 완료 → Staging 전체 CRC 검증 (깨진 전송 차단) */
  if (FlashIf_Crc32((const uint8_t *)STAGING_ADDRESS, total) != expectCrc)
  {
    /* 불일치 → 플래그를 남기지 않는다. 깨진 펌웨어는 절대 적용되지 않음. */
    HAL_UART_Transmit(&huart3, (uint8_t *)crcerr, sizeof(crcerr) - 1U, HAL_MAX_DELAY);
    goto stop;
  }
  HAL_UART_Transmit(&huart3, (uint8_t *)done, sizeof(done) - 1U, HAL_MAX_DELAY);

  /* 5) '업데이트 대기' 플래그(메타데이터) 기록 후 재부팅 → 부트로더가 적용 */
  {
    FwMeta meta;
    meta.magic    = FW_UPDATE_MAGIC;
    meta.state    = FW_STATE_PENDING;   /* 부트로더에게 '적용 예정'을 알림 */
    meta.size     = total;
    meta.crc      = expectCrc;          /* 부트로더가 복사 전/후 검증에 사용 */
    meta.attempts = 0U;
    meta.reserved = 0U;
    FlashIf_WriteMeta(&meta);
  }
  HAL_Delay(100);          /* UART 송신/플래시 마무리 여유 */
  NVIC_SystemReset();      /* 재부팅 (돌아오지 않음) */

stop:
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);   /* 파랑 유지 (오류 시) */
  while (1) { }
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
