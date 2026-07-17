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
#include "net_link.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* 전송 추상화: OTA 프로토콜(App_EnterDownloadMode)이 UART/TCP에 무관하게
 * 동작하도록 고정 길이 recv/send와 워치독 pet을 함수 포인터로 노출한다.
 *   recv: 정확히 len 바이트 수신(timeoutMs==0이면 무한). 성공 true.
 *   send: len 바이트 전량 송신. 성공 true.
 *   pet : 워치독 갱신(+TCP면 lwIP 펌핑). 긴 erase/전송 중 리셋·연결끊김 방지. */
typedef struct {
  bool (*recv)(uint8_t *buf, uint32_t len, uint32_t timeoutMs);
  bool (*send)(const uint8_t *buf, uint32_t len);
  void (*pet)(void);
} FwTransport;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CRC_HandleTypeDef hcrc;

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
static void MX_CRC_Init(void);
/* USER CODE BEGIN PFP */
static void App_EnterDownloadMode(const FwTransport *t);
static uint16_t App_Crc16(const uint8_t *data, uint32_t len);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* --- UART 전송 백엔드 (USART3, 기존 경로) --- */
static bool Uart_Recv(uint8_t *buf, uint32_t len, uint32_t timeoutMs)
{
  uint32_t to = (timeoutMs == 0U) ? HAL_MAX_DELAY : timeoutMs;
  return HAL_UART_Receive(&huart3, buf, (uint16_t)len, to) == HAL_OK;
}
static bool Uart_Send(const uint8_t *buf, uint32_t len)
{
  return HAL_UART_Transmit(&huart3, (uint8_t *)buf, (uint16_t)len, HAL_MAX_DELAY) == HAL_OK;
}
static void Uart_Pet(void)
{
  IWDG->KR = 0x0000AAAAU;
}

/* --- TCP 전송 백엔드 (lwIP, net_link) --- */
static bool Tcp_Recv(uint8_t *buf, uint32_t len, uint32_t timeoutMs)
{
  return NetLink_Recv(buf, len, timeoutMs);
}
static bool Tcp_Send(const uint8_t *buf, uint32_t len)
{
  return NetLink_Send(buf, len);
}
static void Tcp_Pet(void)
{
  IWDG->KR = 0x0000AAAAU;   /* 워치독 갱신 */
  NetLink_Poll();           /* + lwIP 펌핑: 긴 erase/전송 중에도 연결(ACK/재전송) 유지 */
}

static const FwTransport uartTransport = { Uart_Recv, Uart_Send, Uart_Pet };
static const FwTransport tcpTransport  = { Tcp_Recv,  Tcp_Send,  Tcp_Pet  };

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
  MX_USART3_UART_Init();
  MX_USB_OTG_FS_PCD_Init();
  MX_IWDG_Init();
  MX_CRC_Init();
  MX_LWIP_Init();
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

  /* TCP 링크 초기화: 포트 7 리슨 시작 (MX_LWIP_Init 이후여야 함) */
  NetLink_Init();

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

    /* lwIP 서비스: 수신 패킷 처리(ethernetif_input) + 타이머(ARP/TCP) + 링크상태 점검.
     * 베어메탈(NO_SYS)이므로 메인 루프가 주기적으로 직접 호출해야 스택이 동작한다.
     * NetLink_Poll() == MX_LWIP_Process() 래퍼. */
    NetLink_Poll();

    /* === 앱 정상 모드 — UART/TCP 두 경로에서 동시에 "FWUPDATE"를 감시 ===
     * · LD1(초록)을 0.5초마다 토글 = 앱 정상 실행 중(하트비트)
     * · UART 또는 TCP로 "FWUPDATE"가 오면 해당 전송계층으로 다운로드 모드 진입
     * (베어메탈: 짧은 타임아웃 폴링으로 통신 감시와 LED를 함께 처리) */
    static const char FW_UPDATE_CMD[] = "FWUPDATE";
    static uint8_t  cmdIdx    = 0U;   /* UART 매처 인덱스 */
    static uint8_t  tcpCmdIdx = 0U;   /* TCP 매처 인덱스 */
    static uint32_t lastBlink = 0U;
    uint8_t ch;

    /* UART 1바이트 폴링 (10ms 타임아웃) → 매칭 시 UART 전송계층으로 다운로드 */
    if (HAL_UART_Receive(&huart3, &ch, 1U, 10U) == HAL_OK)
    {
      if (ch == (uint8_t)FW_UPDATE_CMD[cmdIdx])
      {
        cmdIdx++;
        if (FW_UPDATE_CMD[cmdIdx] == '\0')   /* "FWUPDATE" 전체 매칭 */
        {
          cmdIdx = 0U;
          App_EnterDownloadMode(&uartTransport);   /* 성공 시 재부팅되어 돌아오지 않음 */
        }
      }
      else
      {
        /* 불일치: 방금 글자가 명령 첫 글자면 1, 아니면 0으로 리셋 */
        cmdIdx = (ch == (uint8_t)FW_UPDATE_CMD[0]) ? 1U : 0U;
      }
    }

    /* TCP: 연결돼 있으면 수신 링버퍼의 바이트를 훑어 "FWUPDATE" 매칭 → TCP 전송계층으로 다운로드.
     * 매처는 정확히 "FWUPDATE" 8바이트만 소비하고 멈추므로, 이후 헤더/청크는 그대로 남아
     * App_EnterDownloadMode(&tcpTransport)의 t->recv가 이어서 읽는다.
     * (프로토콜상 PC는 READY 응답을 받은 뒤에야 헤더를 보내므로 오버랩 없음) */
    if (NetLink_Connected())
    {
      uint8_t tb;
      while (NetLink_ReadAvailable(&tb, 1U) == 1U)
      {
        if (tb == (uint8_t)FW_UPDATE_CMD[tcpCmdIdx])
        {
          tcpCmdIdx++;
          if (FW_UPDATE_CMD[tcpCmdIdx] == '\0')
          {
            tcpCmdIdx = 0U;
            App_EnterDownloadMode(&tcpTransport);   /* 성공 시 재부팅 */
            break;
          }
        }
        else
        {
          tcpCmdIdx = (tb == (uint8_t)FW_UPDATE_CMD[0]) ? 1U : 0U;
        }
      }
    }

    /* 하트비트: 0.5초마다 초록 LED 토글 */
    if ((HAL_GetTick() - lastBlink) >= 100U)
    {
      HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
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
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */

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
static void App_EnterDownloadMode(const FwTransport *t)
{
  const char ready[]  = "READY\r\n";
  const char done[]   = "DONE\r\n";
  const char crcerr[] = "CRCERR\r\n";
  uint8_t  ack = 0x79U, nack = 0x1FU;
  uint8_t  buf[256];
  uint8_t  hdr[8];
  uint32_t total, expectCrc, addr, remaining;

  /* 모든 wire I/O는 전송 계층(t)을 통한다 → UART/TCP 동일 로직으로 동작.
   *   t->recv(buf,len,0)  : 정확 len 수신(무한 대기)
   *   t->send(buf,len)    : len 전량 송신
   *   t->pet()            : 워치독 갱신(+TCP면 lwIP 펌핑) */

  HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);   /* 초록 끔 */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);     /* 파랑: 다운로드 모드 */
  if (!t->send((const uint8_t *)ready, sizeof(ready) - 1U)) goto stop;

  /* 1) 헤더 수신: [전체크기 4B][CRC32 4B] (little-endian) */
  if (!t->recv(hdr, 8U, 0U)) goto stop;
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
    (void)t->send(&nack, 1U);
    goto stop;
  }

  /* 2) Staging 영역에서 필요한 만큼 지우기 */
  t->pet();   /* 지우기 직전 워치독 갱신(+TCP 펌핑) — 큰 erase가 타임아웃/연결끊김 넘기지 않도록 */
  if (FlashIf_EraseRange(STAGING_ADDRESS, STAGING_ADDRESS + total - 1U) != FLASH_IF_OK)
  {
    (void)t->send(&nack, 1U);
    goto stop;
  }
  if (!t->send(&ack, 1U)) goto stop;   /* 지우기 완료 ACK */

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

    t->pet();   /* 청크마다 워치독 갱신(+TCP 펌핑) */

    /* 이 청크가 CRC를 통과할 때까지 재수신 */
    for (;;)
    {
      if (!t->recv(buf, n, 0U)) goto stop;
      if (!t->recv(crcBuf, 2U, 0U)) goto stop;

      uint16_t rxCrc = (uint16_t)crcBuf[0] | ((uint16_t)crcBuf[1] << 8);
      if (App_Crc16(buf, n) == rxCrc)
      {
        break;   /* 데이터 정상 → 기록으로 진행 */
      }

      /* 깨짐 → NACK, PC가 같은 청크를 재전송한다 */
      (void)t->send(&nack, 1U);
      if (++retries >= 5U) goto stop;   /* 같은 청크 5회 실패 → 포기 */
    }

    /* Flash는 4바이트 단위 → 꼬리는 0xFF로 패딩 후 기록 */
    uint32_t nAligned = (n + 3U) & ~3U;
    for (uint32_t i = n; i < nAligned; i++) buf[i] = 0xFFU;
    if (FlashIf_Write(addr, buf, nAligned) != FLASH_IF_OK)
    {
      (void)t->send(&nack, 1U);
      goto stop;
    }
    if (!t->send(&ack, 1U)) goto stop;   /* 청크 확정 ACK */

    addr += n;
    remaining -= n;
    HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);            /* 진행 표시 */
  }

  /* 4) 수신 완료 → Staging 전체 CRC 검증 (깨진 전송 차단) */
  if (FlashIf_Crc32((const uint8_t *)STAGING_ADDRESS, total) != expectCrc)
  {
    /* 불일치 → 플래그를 남기지 않는다. 깨진 펌웨어는 절대 적용되지 않음. */
    (void)t->send((const uint8_t *)crcerr, sizeof(crcerr) - 1U);
    goto stop;
  }
  if (!t->send((const uint8_t *)done, sizeof(done) - 1U)) goto stop;

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

  /* 재부팅 전 잠깐: 마지막 응답(DONE)이 실제로 상대에 도달하도록 전송계층을 계속 펌핑.
   * UART는 이미 블로킹 송신 완료 상태지만, TCP는 lwIP가 세그먼트를 flush/재전송할
   * 시간이 필요하다(HAL_Delay로 멈추면 lwIP가 못 돌아 재전송 불가). */
  {
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < 150U) { t->pet(); }
  }
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
