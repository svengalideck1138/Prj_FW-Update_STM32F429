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
#include "cmsis_os.h"
#include "lwip.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "flash_if.h"
#include "net_link.h"
#include "dbg_uart.h"
#include "wdg_monitor.h"
#include "uart_link.h"
#include "fw_info.h"
#include "ota.h"             /* FwTransport/FwTarget + Ota_EnterDownloadMode/Ota_RequestRollback */
#include <string.h>          /* 명령 매처의 memcmp/memset */
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
CRC_HandleTypeDef hcrc;

IWDG_HandleTypeDef hiwdg;

UART_HandleTypeDef huart3;
DMA_HandleTypeDef hdma_usart3_rx;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  /* [R1 디버깅] 기본값 128*4(512B)에서 4096B로 키움.
   * 이 태스크가 MX_LWIP_Init()을 실행하는데, 그 안의 HAL_ETH_Init()/LAN8742_Init()이
   * 큰 지역 구조체(ETH_MACConfigTypeDef 등)를 써서 512B로는 스택이 넘친다.
   * ※ 이 줄은 CubeMX 생성 영역이라 재생성 시 되돌아간다.
   *   확정되면 CubeMX(FREERTOS → Tasks and Queues → defaultTask → Stack Size = 1024 words)에도 반영할 것.
   * 이 태스크는 초기화 후 osThreadExit()로 종료되므로 스택은 곧 회수된다. */
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */

/* --- ledTask: 하트비트(LD2) 겸 CPU 기아 감지기 ---
 * 최저 우선순위(osPriorityLow)로 두는 것이 의도적인 설계다. 상위 태스크가 CPU를 독점하면
 * 이 태스크가 가장 먼저 굶으므로, R2에서 워치독 체크인을 붙이면 그대로 '기아 감지기'가 된다.
 * (하트비트 LED와 생존 감시가 같은 메커니즘이 된다) */
osThreadId_t ledTaskHandle;
const osThreadAttr_t ledTask_attributes = {
  .name       = "ledTask",
  /* 512 → 1024. 실측 사용량은 320~328B로 안정적이라 512B에서도 동작에 문제는 없었지만
   * (여유 36~38%), 다른 태스크가 42~87%인 것에 비해 혼자 얇아 여유를 맞췄다.
   * 힙 여유가 12KB라 비용이 작다. 사용량에는 App_PrintStatusLine()의 지역 버퍼
   * (STATUS_LINE_MAX=96B)가 포함된다. */
  .stack_size = 1024,
  .priority   = (osPriority_t) osPriorityLow,
};

/* --- wdgTask: 체크인 취합 후 IWDG pet ---
 * OTA/통신 태스크보다 '위'에 둔다(폭주 태스크가 감시 판정을 지연시키지 못하게).
 * 다만 네트워크 스레드(tcpip/EthIf)보다는 '아래'에 둬서 lwIP에 지터를 주지 않는다. */
osThreadId_t wdgTaskHandle;
const osThreadAttr_t wdgTask_attributes = {
  .name       = "wdgTask",
  .stack_size = 512,
  .priority   = (osPriority_t) osPriorityAboveNormal,
};

/* --- uartTask: UART "FWUPDATE" 감시 + OTA 수행 ---
 * Ota_EnterDownloadMode가 256B 청크 버퍼 등을 스택에 두므로 넉넉히 잡는다. */
osThreadId_t uartTaskHandle;
const osThreadAttr_t uartTask_attributes = {
  .name       = "uartTask",
  .stack_size = 2048,
  .priority   = (osPriority_t) osPriorityNormal,
};

/* --- netTask: TCP 서버 (R4는 echo, R5부터 OTA) ---
 * 소켓 API와 Ota_EnterDownloadMode(256B 청크 버퍼)를 쓰므로 uartTask와 같은 크기로 잡는다. */
osThreadId_t netTaskHandle;
const osThreadAttr_t netTask_attributes = {
  .name       = "netTask",
  .stack_size = 2048,
  .priority   = (osPriority_t) osPriorityNormal,
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
static void MX_IWDG_Init(void);
static void MX_CRC_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
static void StartLedTask(void *argument);
static void StartUartTask(void *argument);
static void StartNetTask(void *argument);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* 워치독 감시 슬롯 ID (StartDefaultTask에서 등록) */
static WdgId s_ledWdgId  = WDG_INVALID_ID;
static WdgId s_uartWdgId = WDG_INVALID_ID;
static WdgId s_netWdgId  = WDG_INVALID_ID;

/* OTA 세션 상호배제: UART와 TCP 두 경로가 동시에 다운로드를 시작하지 못하게 한다.
 * 반드시 try-lock(타임아웃 0)으로만 잡는다 — 블로킹으로 기다리면 진 쪽이 앞선 세션이
 * 끝난 뒤 '두 번째' 다운로드를 시작해 Staging을 망친다. 잡으면 풀지 않는다
 * (성공=재부팅, 실패=Wdg_Panic 후 정지). 부팅 후 모든 flash 접근도 이 뮤텍스로 직렬화된다. */
static osMutexId_t s_otaMutex = NULL;

/* USART3 송신권. 주기적인 버전 로그(ledTask)와 OTA 프로토콜 응답(uartTask)이 같은 UART를
 * 쓰기 때문에 반드시 배타 처리해야 한다. 로그가 청크 스트림 사이에 끼어들면 전송이 깨진다.
 *   · 로거  : try-lock. 못 잡으면 이번 주기 로그를 조용히 건너뛴다.
 *   · OTA   : 세션 '전체' 동안 잡는다(응답 하나하나가 아니라 세션 단위여야 안전).
 * TCP OTA는 다른 채널이므로 이 뮤텍스를 잡지 않는다 — 그동안 UART 로그는 계속 나온다. */
static osMutexId_t s_uartTxMutex = NULL;

/* 상태 한 줄이 들어갈 크기: 고정 문구(~30) + 버전 2개(FW_INFO_VER_LEN) + up 초(10) + 여유 */
#define STATUS_LINE_MAX   96U

/** @brief strnlen 대체 — nano.specs 조합에서 가시성이 옵션에 좌우되지 않도록 직접 둔다.
  *        Factory 슬롯에서 읽은 문자열은 널 종료가 깨져 있을 수 있어 상한이 반드시 필요하다. */
static uint32_t App_StrNLen(const char *s, uint32_t max)
{
  uint32_t n = 0U;
  while ((n < max) && (s[n] != '\0')) { n++; }
  return n;
}

/** @brief 부호 없는 10진수를 dst에 쓰고, 쓴 길이를 반환한다(널 종료 없음). */
static uint32_t App_U32ToDec(char *dst, uint32_t v)
{
  char     tmp[10];
  uint32_t n = 0U;
  uint32_t i = 0U;

  if (v == 0U) { dst[0] = '0'; return 1U; }

  while ((v > 0U) && (n < sizeof(tmp))) { tmp[n++] = (char)('0' + (v % 10U)); v /= 10U; }
  while (n > 0U) { dst[i++] = tmp[--n]; }
  return i;
}

/**
  * @brief  현재 상태 한 줄을 dst에 조립한다(널 종료). 반환값은 문자열 길이.
  * @note   형식이 UART 주기 로그와 '글자 단위로 같아야' 한다 — Ethernet 뷰어와 RS-232
  *         뷰어가 같은 화면으로 보이는 것이 이 기능의 목적이기 때문이다.
  *         그래서 UART/TCP 양쪽이 모두 이 함수를 통과한다.
  */
static uint32_t App_BuildStatusLine(char *dst, uint32_t cap)
{
  static const char P0[] = "[FW] app=";
  static const char P1[] = "  factory=";
  static const char P2[] = "  up=";
  static const char P3[] = "s\r\n";

  const char *app  = g_fwInfo.version;
  const char *fact = FwInfo_VersionOf(FACTORY_ADDRESS, FACTORY_SIZE);
  uint32_t    n    = 0U;

  /* cap은 STATUS_LINE_MAX로 넉넉히 잡혀 있지만, 버전 문자열이 손상돼 길어질 수 있으므로
   * (Factory 슬롯을 그대로 읽는다) 매 조각마다 남은 공간을 확인한다. */
  #define APPEND(src, len)  do {                                   \
      uint32_t _l = (uint32_t)(len);                               \
      if ((n + _l) >= cap) { dst[n] = '\0'; return n; }            \
      memcpy(&dst[n], (src), _l); n += _l;                         \
    } while (0)

  APPEND(P0, sizeof(P0) - 1U);
  APPEND(app,  App_StrNLen(app,  FW_INFO_VER_LEN));
  APPEND(P1, sizeof(P1) - 1U);
  APPEND(fact, App_StrNLen(fact, FW_INFO_VER_LEN));
  APPEND(P2, sizeof(P2) - 1U);

  if ((n + 10U) < cap) { n += App_U32ToDec(&dst[n], HAL_GetTick() / 1000U); }

  APPEND(P3, sizeof(P3) - 1U);
  #undef APPEND

  dst[n] = '\0';
  return n;
}

/**
  * @brief  현재 상태 한 줄을 USART3로 출력한다(ledTask의 주기 로그).
  * @note   호출자가 USART3 송신권을 이미 확보한 상태로 부를 것.
  */
static void App_PrintStatusLine(void)
{
  char buf[STATUS_LINE_MAX];
  (void)App_BuildStatusLine(buf, sizeof(buf));
  Dbg_Puts(buf);
}

/* --- UART 전송 백엔드 (USART3: 순환 DMA + StreamBuffer) --- */
static bool Uart_Recv(uint8_t *buf, uint32_t len, uint32_t timeoutMs)
{
  return UartLink_Recv(buf, len, timeoutMs);
}
static bool Uart_Send(const uint8_t *buf, uint32_t len)
{
  return UartLink_Send(buf, len);
}
static void Uart_Pet(void)
{
  /* [R3] 직접 IWDG를 갱신하지 않는다 — 체크인만 하고, 실제 pet은 wdgTask가
   * '등록된 모든 태스크가 기한 내 체크인'했을 때만 수행한다. */
  Wdg_CheckIn(s_uartWdgId);
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
  /* [R4] 예전에는 여기서 NetLink_Poll()로 lwIP를 손수 돌려야 했다. 베어메탈에서는
   * 긴 erase 동안 단일 루프가 멈추면 스택도 같이 죽었기 때문이다. 이제 lwIP가
   * 자체 tcpip_thread로 계속 돌아가므로 그 펌핑은 불필요하다 — 체크인만 하면 된다. */
  Wdg_CheckIn(s_netWdgId);
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

  /* === 잔류 인터럽트 정리 (RTOS 전환과 함께 필요해짐) ===
   * 부트로더는 점프 전에 HAL_DeInit까지 하지만 NVIC의 enable/pending 비트는 지우지 않는다.
   * 지금까지는 앱에 페리페럴 인터럽트가 하나도 없어서 무해했지만, 이제 TIM6/ETH/USART3/DMA
   * 핸들러가 생기므로 잔류 pending이 '핸들러는 있는데 핸들이 아직 초기화 안 된' 상태로
   * 튈 수 있다. (부트로더는 수정 대상이 아니므로 앱에서 방어한다) */
  for (int i = 0; i < 8; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFFU;   /* 전부 disable */
    NVIC->ICPR[i] = 0xFFFFFFFFU;   /* 전부 pending clear */
  }
  __DSB();
  __ISB();

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
  MX_DMA_Init();
  MX_USART3_UART_Init();
  MX_USB_OTG_FS_PCD_Init();
  MX_IWDG_Init();
  MX_CRC_Init();
  /* USER CODE BEGIN 2 */

  /* 시험 부팅(TRIAL) 중이면 자가진단 후 '정상 확인(CONFIRMED)'을 기록한다.
   * 여기까지 도달 = 앱이 부팅·초기화에 성공했다는 뜻. (실제 제품은 센서/통신 점검 후 확인)
   * CONFIRMED를 못 남기고 앱이 멈추면 → 워치독이 리셋 → 부트로더가 Factory로 롤백. */
  {
    const FwMeta *m = (const FwMeta *)METADATA_ADDRESS;

    /* [R1 디버그] 부팅 배너: 이 줄이 몇 초마다 반복되면 '리셋 루프'라는 뜻이고,
     * state 값으로 워치독이 무장된 상태(TRIAL)인지 바로 알 수 있다.
     *   NONE=0xFFFFFFFF  PENDING=0x50454E44  TRIAL=0x54524941  CONFIRMED=0x434F4E46 */
    /* 부팅 시 한 번: 실행 중인 이미지와 Factory 슬롯 이미지의 버전 */
    Dbg_Puts("\r\n[FW] app=");
    Dbg_Puts(g_fwInfo.version);
    Dbg_Puts("  factory=");
    Dbg_Puts(FwInfo_VersionOf(FACTORY_ADDRESS, FACTORY_SIZE));
    Dbg_Puts("\r\n");

    /* 이 펌웨어가 처리할 수 있는 명령 목록.
     * 버전 문자열(FW_App01 등)은 기능이 추가돼도 그대로라, 보드에 올라간 이미지가
     * 새 명령을 아는지 알 수 없다. 목록을 찍어두면 UI가 무응답일 때
     * '펌웨어가 구버전이라 명령을 모르는 것'을 즉시 구분할 수 있다. */
    Dbg_Puts("[FW] cmds=FWUPDATE,FWFACTRY,FWROLLBK\r\n");

    Dbg_Puts("[APP] boot  magic=");
    Dbg_PutHex32(m->magic);
    Dbg_Puts(" state=");
    Dbg_PutHex32(m->state);

    /* 리셋 원인(RCC_CSR 플래그) — '왜 재부팅됐는지'를 알려준다.
     *   IWDG = 워치독이 물었다(pet 누락)  SFT = NVIC_SystemReset() 호출
     *   PIN  = 리셋 버튼/ST-Link          POR/BOR = 전원 투입·전압 강하
     * CSR 원시값도 같이 찍는다: 플래그 매크로 해석이 어긋나거나 부트로더가 미리
     * 지워버린 경우를 구분하기 위함(디코드가 비었는데 원시값이 0이 아니면 매크로 문제,
     * 원시값의 상위 비트가 전부 0이면 우리보다 앞단에서 이미 클리어된 것). */
    Dbg_Puts("  CSR=");
    Dbg_PutHex32(RCC->CSR);
    Dbg_Puts("  rst:");
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST))  { Dbg_Puts(" POR");  }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST))  { Dbg_Puts(" BOR");  }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST))  { Dbg_Puts(" PIN");  }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))  { Dbg_Puts(" SFT");  }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) { Dbg_Puts(" IWDG"); }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) { Dbg_Puts(" WWDG"); }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)) { Dbg_Puts(" LPWR"); }
    /* 플래그를 지우지 않는다.
     * __HAL_RCC_CLEAR_RESET_FLAGS()는 RCC->CSR |= RMVF 인데, 한 번 지우고 나면 이후
     * 리셋 원인이 남지 않아(실측: 첫 부팅만 PIN, 그 뒤로는 계속 공백) 진단이 무력해졌다.
     * 지우지 않으면 마지막 POR 이후의 원인들이 '누적'되어 보이므로,
     * IWDG가 물었는지 여부를 확실히 알 수 있다(예: "PIN IWDG"). */
    Dbg_Puts("\r\n");

#if (FW_VARIANT == FW_VARIANT_APP02_BAD)
    /* [롤백 시험 전용] 자가확인 '직전'에 고의로 멈춘다.
     * 여기서 멈추면 CONFIRMED를 기록하지 못하고, 스케줄러도 시작되지 않아 워치독을
     * 갱신할 태스크가 아예 생기지 않는다. 부트로더가 TRIAL 점프 직전에 켜 둔 IWDG(약 4초)가
     * 곧 리셋시키고, 부트로더는 'TRIAL인데 확인 실패'로 판단해 Factory→App 롤백을 수행한다.
     * LD3(빨강)를 켜 두어 '멈춘 상태'임을 눈으로도 알 수 있게 한다. */
    Dbg_Puts("[TEST] BAD build - hanging before CONFIRMED; expect IWDG reset & rollback\r\n");
    HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET);
    while (1) { }
#endif

    if (m->magic == FW_UPDATE_MAGIC && m->state == FW_STATE_TRIAL)
    {
      IWDG->KR = 0x0000AAAAU;        /* 플래시 쓰기 전에 워치독 갱신(여유 확보) */
      FwMeta meta = *m;
      meta.state = FW_STATE_CONFIRMED;
      FlashIf_WriteMeta(&meta);
      Dbg_Puts("[APP] TRIAL -> CONFIRMED (self-test passed)\r\n");
    }
  }

  /* [R1] TCP 리슨 시작(NetLink_Init)은 여기서 하지 않는다.
   * RTOS 모드에서는 MX_LWIP_Init()이 StartDefaultTask 안에서(스케줄러 시작 후) 호출되므로,
   * 이 시점엔 lwIP가 아직 올라오지 않았다. TCP 서버는 R5에서 netTask가 담당한다. */

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* [R1] 여기 있던 베어메탈 단일 루프(워치독 pet + lwIP 수동 펌핑 + UART/TCP "FWUPDATE"
     * 감시 + 하트비트)는 FreeRTOS 태스크로 분해되어 이 자리에서 제거되었다.
     *   · 하트비트 + CPU 기아 감지 → ledTask            (R1, 아래)
     *   · 워치독 pet(체크인 취합)   → wdgTask            (R2)
     *   · UART "FWUPDATE" + OTA     → uartTask           (R3)
     *   · TCP "FWUPDATE" + OTA      → netTask            (R5)
     *   · lwIP 서비스               → tcpip_thread/EthIf (lwIP가 자동 생성)
     * osKernelStart()는 반환하지 않으므로 이 루프는 도달 불가(스케줄러 시작 실패 시의 안전망). */

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
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);

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
  * @brief  하트비트 태스크 — LD2를 100ms 주기로 토글한다(앱이 살아 있다는 표시).
  * @note   최저 우선순위(osPriorityLow)로 도는 것이 핵심이다. 상위 태스크가 CPU를
  *         독점하면 이 태스크가 가장 먼저 굶어 LED가 멈추므로, 눈으로도 이상을 알 수 있고
  *         R2에서 워치독 체크인을 추가하면 그대로 CPU 기아 감지기가 된다.
  */
static void StartLedTask(void *argument)
{
  (void)argument;

  uint32_t logTick = 0U;   /* 100ms 주기 × 10 = 1초마다 버전 로그 */
  uint32_t resTick = 0U;   /* 버전 로그 30회 = 30초마다 자원 여유 보고 */

  for (;;)
  {
    /* [R2] 직접 pet하지 않고 '체크인'만 한다.
     * 실제 IWDG pet은 wdgTask가 '등록된 모든 태스크가 기한 내 체크인'했을 때만 수행한다.
     * → 이 태스크(최저 우선순위)가 굶으면 곧 pet이 끊겨 리셋 → Factory 롤백. */
    Wdg_CheckIn(s_ledWdgId);

    /* --- 빌드 변형별 LED 패턴 (어떤 이미지가 돌고 있는지 눈으로 구분) ---
     * panic 상태에서는 LD3가 '장애 표시'로 점등되므로 LED 제어를 멈춰 혼동을 막는다. */
    if (!Wdg_IsPanicked())
    {
#if (FW_VARIANT == FW_VARIANT_FACTORY)
      /* LED 3개가 '차올랐다 빠지고, 반대로 다시 차올랐다 빠지는' 12단계 패턴.
       * 각 단계 500ms → 한 바퀴 6초.
       *    1~3 : LD1 → LD2 → LD3 순서로 하나씩 켜짐   (전부 켜짐)
       *    4~6 : LD1 → LD2 → LD3 순서로 하나씩 꺼짐   (전부 꺼짐)
       *    7~9 : LD3 → LD2 → LD1 순서로 하나씩 켜짐   (전부 켜짐)
       *   10~12: LD3 → LD2 → LD1 순서로 하나씩 꺼짐   (전부 꺼짐)
       *
       * ※ HAL_Delay(500)를 12번 늘어놓는 방식은 쓸 수 없다. 그러면 이 태스크가 한 바퀴에
       *   6초를 통째로 붙잡아:
       *     (a) 워치독 체크인이 루프당 1회뿐이라 기한(6초)에 딱 걸려 panic이 나고,
       *     (b) 1초마다 나와야 할 버전 로그가 6초에 한 번만 나온다.
       *   게다가 HAL_Delay는 busy-wait이라 RTOS에서 CPU를 태운다(osDelay와 달리 양보하지 않음).
       *   → 대신 100ms 주기인 이 루프가 5회 돌 때마다(=500ms) 한 단계씩 진행시킨다.
       *     보이는 동작은 완전히 동일하고, 체크인·로그 주기는 그대로 유지된다. */
      static const uint8_t ledPattern[12] = {
        /* bit0=LD1(초록) bit1=LD2(파랑) bit2=LD3(빨강) */
        0x01U,  /*  1) LD1                 */
        0x03U,  /*  2) LD1+LD2             */
        0x07U,  /*  3) LD1+LD2+LD3 (전부)  */
        0x06U,  /*  4) LD1 꺼짐            */
        0x04U,  /*  5) LD2 꺼짐            */
        0x00U,  /*  6) LD3 꺼짐 (전부 꺼짐)*/
        0x04U,  /*  7) LD3                 */
        0x06U,  /*  8) LD3+LD2             */
        0x07U,  /*  9) LD3+LD2+LD1 (전부)  */
        0x03U,  /* 10) LD3 꺼짐            */
        0x01U,  /* 11) LD2 꺼짐            */
        0x00U   /* 12) LD1 꺼짐 (전부 꺼짐)*/
      };
      static uint8_t patIdx = 0U;   /* 현재 단계 (0~11) */
      static uint8_t patSub = 0U;   /* 100ms 카운터 — 5회마다 다음 단계 */

      if (++patSub >= 1U)
      {
        patSub = 0U;
        patIdx = (uint8_t)((patIdx + 1U) % 12U);
      }

      uint8_t m = ledPattern[patIdx];
      HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, (m & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
      HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, (m & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
      HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, (m & 0x04U) ? GPIO_PIN_SET : GPIO_PIN_RESET);

#elif (FW_VARIANT == FW_VARIANT_APP02) || (FW_VARIANT == FW_VARIANT_APP02_BAD)
      HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);      /* 초록 점멸 */
#else
      HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);      /* 파랑 점멸 (APP01) */
#endif
    }

    /* --- 1초마다 버전 로그 (실행 중인 이미지 + Factory 슬롯 이미지) ---
     * USART3는 UART OTA 채널이기도 하므로 송신권을 try-lock으로 잡을 때만 출력한다.
     * OTA 세션 중에는 잡히지 않아 자동으로 건너뛴다 → 프로토콜이 깨지지 않는다. */
    if ((++logTick >= 10U) && (s_uartTxMutex != NULL))
    {
      logTick = 0U;

      if (osMutexAcquire(s_uartTxMutex, 0U) == osOK)
      {
        App_PrintStatusLine();

        /* [R6] 30초마다 자원 여유 보고.
         * 스택 크기는 처음에 경험칙으로 정했으므로 '실제로 얼마나 쓰는지'를 재서 확인한다.
         * osThreadGetStackSpace()는 그 태스크가 지금까지 남긴 '최소' 여유(바이트)라,
         * OTA처럼 깊게 들어가는 작업을 한 번 거친 뒤의 값이 진짜 최악값이다.
         * heapMin은 부팅 이후 최저 힙 여유(= 가장 빠듯했던 순간). */
        if (++resTick >= 30U)
        {
          resTick = 0U;

          Dbg_Puts("[RES] stack free(B) led=");
          Dbg_PutU32(osThreadGetStackSpace(ledTaskHandle));
          Dbg_Puts(" uart=");
          Dbg_PutU32(osThreadGetStackSpace(uartTaskHandle));
          Dbg_Puts(" net=");
          Dbg_PutU32(osThreadGetStackSpace(netTaskHandle));
          Dbg_Puts(" wdg=");
          Dbg_PutU32(osThreadGetStackSpace(wdgTaskHandle));
          Dbg_Puts("  heap now=");
          Dbg_PutU32((uint32_t)xPortGetFreeHeapSize());
          Dbg_Puts(" min=");
          Dbg_PutU32((uint32_t)xPortGetMinimumEverFreeHeapSize());
          Dbg_Puts("\r\n");
        }

        osMutexRelease(s_uartTxMutex);
      }
    }

    /* [R6] R2에서 쓰던 'USER 버튼으로 태스크 멈춤 흉내내기' 시험 코드는 제거했다.
     *  · 워치독 체크인 사슬은 R2에서 이미 실증됐고,
     *  · 롤백 검증은 FW_VARIANT_APP02_BAD 빌드가 더 현실적으로(자가확인 실패) 대신하며,
     *  · USER 버튼은 부트로더의 '업데이트 모드 진입'에도 쓰여 혼동을 준다. */

    osDelay(100);
  }
}

/**
  * @brief  UART 통신 태스크 — "FWUPDATE" 명령을 감시하고 OTA를 수행한다.
  * @note   기존 베어메탈 루프의 10ms 블로킹 폴링을 대체한다. 이제 수신은 DMA+StreamBuffer가
  *         처리하고, 이 태스크는 100ms 타임아웃으로 1바이트씩 꺼내며 매처를 돌린다
  *         (타임아웃은 '무트래픽 상태에서도 깨어나 체크인하기 위한' 것).
  */
static void StartUartTask(void *argument)
{
  (void)argument;

  /* 명령은 둘 다 8바이트다. 마지막 8바이트를 슬라이딩 윈도로 들고 비교하면
   * 명령이 늘어나도 매처 로직이 단순하게 유지된다.
   *   "FWUPDATE" → Staging (정상 OTA, 적용 후 재부팅)
   *   "FWFACTRY" → Factory (골든 이미지 교체, 재부팅 없음)
   * 매처는 명령 8바이트만 소비하므로 뒤따르는 헤더/청크는 스트림에 그대로 남는다. */
  static const char CMD_STAGING[8]  = { 'F','W','U','P','D','A','T','E' };
  static const char CMD_FACTORY[8]  = { 'F','W','F','A','C','T','R','Y' };
  static const char CMD_ROLLBACK[8] = { 'F','W','R','O','L','L','B','K' };
  uint8_t win[8] = {0};

  if (!UartLink_Init())
  {
    Wdg_Panic("UartLink_Init failed");
    vTaskSuspend(NULL);
  }

  for (;;)
  {
    Wdg_CheckIn(s_uartWdgId);

    uint8_t ch;
    if (!UartLink_Recv(&ch, 1U, 100U))
    {
      continue;                                  /* 타임아웃 — 체크인만 하고 계속 감시 */
    }

    /* 한 바이트 밀어 넣기 */
    for (int i = 0; i < 7; i++) { win[i] = win[i + 1]; }
    win[7] = ch;

    bool isStaging  = (memcmp(win, CMD_STAGING,  8) == 0);
    bool isFactory  = (memcmp(win, CMD_FACTORY,  8) == 0);
    bool isRollback = (memcmp(win, CMD_ROLLBACK, 8) == 0);
    if (!isStaging && !isFactory && !isRollback)
    {
      continue;
    }

    /* try-lock: 이미 다른 경로(TCP)가 세션 중이면 조용히 무시하고 계속 감시. */
    if (osMutexAcquire(s_otaMutex, 0U) != osOK)
    {
      continue;
    }

    /* 세션 '전체' 동안 UART 송신권을 잡는다 → 주기 버전 로그가 프로토콜 사이에
     * 끼어들지 못한다. 로거는 try-lock이라 최대 수 ms만 기다리면 된다. */
    (void)osMutexAcquire(s_uartTxMutex, osWaitForever);

    if (isRollback)
    {
      Ota_RequestRollback(&uartTransport);   /* 성공 시 재부팅, 거부되면 복귀 */
    }
    else
    {
      Ota_EnterDownloadMode(&uartTransport,
                            isFactory ? FW_TARGET_FACTORY : FW_TARGET_STAGING);
    }

    /* 여기에 도달하는 건 Factory 경로뿐이다(정상 OTA는 재부팅하거나 Wdg_Panic으로 정지).
     * 다음 명령을 받을 수 있도록 송신권과 세션을 반납하고 윈도를 비운다. */
    osMutexRelease(s_uartTxMutex);
    osMutexRelease(s_otaMutex);
    memset(win, 0, sizeof(win));
  }
}

/**
  * @brief  TCP 서버 태스크 — "FWUPDATE"/"FWFACTRY"를 감시하고 OTA를 수행한다.
  * @note   [R5] uartTask와 완전히 같은 매처·같은 프로토콜을 쓴다. 다른 것은 전송 계층뿐이다
  *         (tcpTransport). 매처는 명령 8바이트만 소비하므로 뒤따르는 헤더/청크는 소켓에
  *         그대로 남아 Ota_EnterDownloadMode의 t->recv가 이어서 읽는다.
  */
static void StartNetTask(void *argument)
{
  (void)argument;

  static const char CMD_STAGING[8]  = { 'F','W','U','P','D','A','T','E' };
  static const char CMD_FACTORY[8]  = { 'F','W','F','A','C','T','R','Y' };
  static const char CMD_ROLLBACK[8] = { 'F','W','R','O','L','L','B','K' };
  /* 상태 조회 — TCP 전용이다. UART는 1초마다 배너가 알아서 나오므로 필요 없다. */
  static const char CMD_INFO[8]     = { 'F','W','I','N','F','O','?','?' };

  NetLink_Init();

  if (!NetLink_ServerOpen())
  {
    Wdg_Panic("NetLink_ServerOpen failed");
    vTaskSuspend(NULL);
  }

  for (;;)
  {
    Wdg_CheckIn(s_netWdgId);

    /* 200ms마다 빠져나와 체크인한다(접속이 없어도 '살아있음'을 알려야 하므로). */
    if (!NetLink_Accept(200U))
    {
      continue;
    }

    /* --- 세션 시작: 명령 감시 --- */
    uint8_t win[8] = {0};

    for (;;)
    {
      Wdg_CheckIn(s_netWdgId);

      uint8_t ch;
      if (!NetLink_Recv(&ch, 1U, 200U))
      {
        /* 타임아웃이면 계속 대기, 연결이 끊겼으면 세션 종료 */
        if (!NetLink_Connected()) break;
        continue;
      }

      /* 한 바이트 밀어 넣기 */
      for (int i = 0; i < 7; i++) { win[i] = win[i + 1]; }
      win[7] = ch;

      /* --- 상태 조회 (요청-응답, 한 줄 텍스트) ---
       * OTA 세션 뮤텍스를 잡지 않는다. 플래시를 건드리지 않고 읽기만 하는 데다,
       * 응답을 여기(netTask)에서 즉시 보내므로 소켓 사용이 겹칠 여지도 없다.
       *
       * ※ 이 응답은 반드시 '요청에 대한 응답'으로만 나가야 한다. 보드가 스스로 주기적으로
       *   보내면 OTA 청크/ACK 스트림 사이에 끼어 프로토콜이 깨진다. 폴링 주기는 PC가 쥔다. */
      if (memcmp(win, CMD_INFO, 8) == 0)
      {
        char     line[STATUS_LINE_MAX];
        uint32_t len = App_BuildStatusLine(line, sizeof(line));
        (void)NetLink_Send((const uint8_t *)line, len);
        memset(win, 0, sizeof(win));
        continue;
      }

      bool isStaging  = (memcmp(win, CMD_STAGING,  8) == 0);
      bool isFactory  = (memcmp(win, CMD_FACTORY,  8) == 0);
      bool isRollback = (memcmp(win, CMD_ROLLBACK, 8) == 0);
      if (!isStaging && !isFactory && !isRollback) continue;

      /* try-lock: UART 쪽이 이미 세션 중이면 조용히 무시하고 계속 감시한다.
       * 블로킹으로 기다리면 앞선 세션이 끝난 뒤 '두 번째' 다운로드가 시작돼 위험하다. */
      if (osMutexAcquire(s_otaMutex, 0U) != osOK) continue;

      if (isRollback)
      {
        Ota_RequestRollback(&tcpTransport);   /* 성공 시 재부팅, 거부되면 복귀 */
      }
      else
      {
        Ota_EnterDownloadMode(&tcpTransport,
                              isFactory ? FW_TARGET_FACTORY : FW_TARGET_STAGING);
      }

      /* 여기에 도달하는 건 Factory 경로뿐이다(정상 OTA는 재부팅하거나 Wdg_Panic으로 정지).
       * 연결은 살아 있으므로 세션만 반납하고 같은 소켓에서 계속 명령을 감시한다. */
      osMutexRelease(s_otaMutex);
      memset(win, 0, sizeof(win));
    }

    NetLink_CloseClient();
  }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for LWIP */
  MX_LWIP_Init();
  /* USER CODE BEGIN 5 */

  /* defaultTask는 '초기화 전용' 태스크로 쓴다.
   * 태스크 테이블을 CubeMX 'Tasks and Queues' 탭이 아니라 USER CODE에 두면
   * .ioc를 재생성해도 구성이 흔들리지 않는다.
   *
   * ※ ledTask는 위 MX_LWIP_Init()보다 먼저 만들 수 없다(생성 코드가 USER CODE 밖에 있음).
   *   대신 진단을 위해: LD2가 점멸하면 "스케줄러 + lwIP 초기화까지 성공"을 뜻하고,
   *   점멸하지 않으면 MX_LWIP_Init() 도중에 죽은 것이다(스택 부족/PHY 초기화 실패 등).
   *   스택 오버플로/힙 부족은 freertos.c의 훅이 LD3로 알려준다. */
  /* OTA 세션 뮤텍스 (UART/TCP 경합 방지) + USART3 송신권 뮤텍스 (로그 vs 프로토콜) */
  s_otaMutex    = osMutexNew(NULL);
  s_uartTxMutex = osMutexNew(NULL);
  if ((s_otaMutex == NULL) || (s_uartTxMutex == NULL))
  {
    Wdg_Panic("mutex create failed");
  }

  /* 워치독 감시 슬롯 등록은 태스크 생성보다 먼저 (감시 태스크가 즉시 판정에 들어가므로).
   *
   * 기한은 '정상 동작 중 발생할 수 있는 최장 공백'보다 커야 한다. 여기서 지배적인 요인은
   * 128KB 섹터 1개 소거(typ 1s, 최악 4s)다 — HAL이 busy-wait 폴링을 하므로 그동안
   * 더 낮은 우선순위 태스크는 아예 돌지 못한다(섹터 사이 osDelay(1)로 1섹터분으로 제한).
   *  · ledTask (Low)    : 평소 100ms마다 체크인하지만 소거 중엔 선점당해 굶는다 → 6초
   *  · uartTask (Normal): OTA 중 체크인 간격이 곧 섹터 1개 소거 시간 → 6초
   * (참고: wdgTask는 AboveNormal이라 소거 중에도 uartTask를 선점해 IWDG pet은 계속된다) */
  s_ledWdgId  = Wdg_Register("ledTask",  6000U);
  s_uartWdgId = Wdg_Register("uartTask", 6000U);
  s_netWdgId  = Wdg_Register("netTask",  6000U);

  ledTaskHandle  = osThreadNew(StartLedTask,     NULL, &ledTask_attributes);
  uartTaskHandle = osThreadNew(StartUartTask,    NULL, &uartTask_attributes);
  netTaskHandle  = osThreadNew(StartNetTask,     NULL, &netTask_attributes);
  wdgTaskHandle  = osThreadNew(Wdg_MonitorTask,  NULL, &wdgTask_attributes);

  /* 할 일을 끝냈으므로 스스로 종료해 스택을 반납한다(무한루프로 남기지 않는다). */
  osThreadExit();
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
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
  /* [R2] 조용히 멈추지 않는다.
   *  · UART로 알리고 Wdg_Panic()으로 IWDG pet을 영구 중단시켜, 리셋 → Factory 롤백으로 잇는다.
   *  · __disable_irq()는 하지 않는다: 인터럽트를 끄면 진단 출력도 어려워지고,
   *    (IWDG는 인터럽트와 무관하지만) 굳이 정보를 버릴 이유가 없다.
   *  · 아주 이른 시점(클럭 설정 실패 등)에 불릴 수 있는데, 그때 huart3는 아직 RESET 상태라
   *    HAL_UART_Transmit이 즉시 HAL_BUSY로 빠져나오므로 안전하다. */
  Dbg_Puts("\r\n*** Error_Handler()\r\n");
  Wdg_Panic("Error_Handler");

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
