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
#include <string.h>          /* 명령 매처의 memcmp/memset */
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

/* 다운로드 대상 슬롯. 프로토콜(크기+CRC32, 청크+CRC16)은 완전히 동일하고
 * '어디에 쓰는가'와 '끝나고 무엇을 하는가'만 다르다.
 *
 *  STAGING : 정상 OTA. Staging에 저장 → Metadata=PENDING 기록 → 재부팅
 *            → 부트로더가 검증 후 App으로 복사(기존 동작 그대로).
 *  FACTORY : 골든 이미지 직접 교체. 메타데이터를 건드리지 않고 재부팅도 하지 않는다.
 *            롤백이 실제로 복원되는지 시험하기 위해 Factory에 '다른' 펌웨어를 넣는 용도다.
 *            ⚠️ Factory는 롤백 복귀처이므로, 여기에 고장난 이미지를 쓰면 롤백 시
 *               고장난 펌웨어로 복구된다(ST-Link로만 회복 가능). 시험 목적 전용.
 *            ⚠️ Factory(0x080A_0000~)는 앱이 실행되는 Bank1을 포함한다 → 소거 중
 *               CPU가 멈춰 어떤 태스크도 못 돈다. 체크인이 아니라 '직접 IWDG pet'이 필요. */
typedef enum {
  FW_TARGET_STAGING = 0,
  FW_TARGET_FACTORY
} FwTarget;

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
 * App_EnterDownloadMode가 256B 청크 버퍼 등을 스택에 두므로 넉넉히 잡는다. */
osThreadId_t uartTaskHandle;
const osThreadAttr_t uartTask_attributes = {
  .name       = "uartTask",
  .stack_size = 2048,
  .priority   = (osPriority_t) osPriorityNormal,
};

/* --- netTask: TCP 서버 (R4는 echo, R5부터 OTA) ---
 * 소켓 API와 App_EnterDownloadMode(256B 청크 버퍼)를 쓰므로 uartTask와 같은 크기로 잡는다. */
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
static void App_EnterDownloadMode(const FwTransport *t, FwTarget target);
static void App_RequestRollback(const FwTransport *t);
static uint16_t App_Crc16(const uint8_t *data, uint32_t len);
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
static void App_EnterDownloadMode(const FwTransport *t, FwTarget target)
{
  const char ready[]  = "READY\r\n";
  const char done[]   = "DONE\r\n";
  const char crcerr[] = "CRCERR\r\n";
  uint8_t  ack = 0x79U, nack = 0x1FU;
  uint8_t  buf[256];
  uint8_t  hdr[8];
  uint32_t total = 0U, expectCrc = 0U, remaining = 0U;

  /* 대상 슬롯에 따라 기준 주소/최대 크기만 달라진다. 프로토콜은 완전히 동일. */
  const bool     toFactory = (target == FW_TARGET_FACTORY);
  const uint32_t baseAddr  = toFactory ? FACTORY_ADDRESS : STAGING_ADDRESS;
  const uint32_t maxSize   = toFactory ? FACTORY_SIZE    : STAGING_SIZE;

  /* stop: 진단에서 offset을 찍으므로 미초기화 상태로 점프해도 안전하게 초기화해 둔다 */
  uint32_t addr = baseAddr;

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
  if ((total == 0U) || (total > maxSize))
  {
    (void)t->send(&nack, 1U);
    goto stop;
  }

  /* 2) Staging 영역에서 필요한 만큼 지우기 — 반드시 '섹터 단위'로 쪼갠다.
   *
   * 한 번의 FlashIf_EraseRange로 최대 4섹터를 지우면 128KB 섹터당 typ 1s / max 4s이므로
   * 최악 16초 동안 블로킹된다. 그동안 체크인이 없으면 워치독이 먼저 물어버린다
   * (지금까지는 앱이 작아 1섹터로 끝나서 우연히 문제가 없었다).
   * 섹터마다 체크인하면 각 구간이 1섹터 시간으로 제한된다.
   *
   * Staging(0x0812_0000~0x0819_FFFF)은 전부 128KB 섹터(17~20)라 128KB씩 전진하면 된다.
   * (F429 2MB 맵: 128KB 섹터 구간은 0x0802_0000~ 및 0x0812_0000~) */
  {
    uint32_t endAddr = baseAddr + total - 1U;
    bool eraseOk = true;

    for (uint32_t a = baseAddr; a <= endAddr; a = FlashIf_NextSectorAddr(a))
    {
      /* Factory는 앱이 실행되는 Bank1을 포함한다. 그 구간을 지우는 동안에는 명령어 인출이
       * 멈춰 어떤 태스크도(감시 태스크조차) 돌지 못하므로, 체크인만으로는 워치독을 막을 수
       * 없다. LSI로 독립 구동되는 IWDG는 계속 카운트하므로 '직접' 갱신해 창을 새로 연다.
       * (Staging은 Bank2라 해당 없지만, 무해하므로 구분 없이 갱신한다) */
      IWDG->KR = 0x0000AAAAU;
      t->pet();                                   /* 섹터마다 체크인 */

      if (FlashIf_EraseRange(a, a) != FLASH_IF_OK) /* a가 속한 섹터 1개만 소거 */
      {
        eraseOk = false;
        break;
      }

      /* ★ 섹터 사이에서 반드시 양보한다.
       * HAL_FLASHEx_Erase는 BSY를 busy-wait 폴링하므로 CPU를 놓지 않는다. 이 태스크
       * (Normal)보다 낮은 ledTask(Low)는 그동안 아예 스케줄되지 못해 체크인이 끊기고,
       * 여러 섹터를 연속 소거하면 그 굶주림이 수 초~십수 초로 누적된다.
       * osDelay(1)로 한 틱 양보하면 낮은 우선순위 태스크가 밀린 체크인을 처리할 수 있어,
       * ledTask가 견뎌야 하는 시간이 '섹터 1개분'으로 제한된다. */
      osDelay(1);
    }
    IWDG->KR = 0x0000AAAAU;
    t->pet();

    if (!eraseOk)
    {
      (void)t->send(&nack, 1U);
      goto stop;
    }
  }
  if (!t->send(&ack, 1U)) goto stop;   /* 지우기 완료 ACK */

  /* 3) 청크 수신 → CRC16 검증 → Staging 기록.
   *    각 청크: [데이터 n바이트][CRC16 2바이트, LE]
   *    CRC 불일치 → NACK (PC가 같은 청크 재전송) / 정상 → 기록 후 ACK */
  addr = baseAddr;
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

      uint16_t rxCrc   = (uint16_t)crcBuf[0] | ((uint16_t)crcBuf[1] << 8);
      uint16_t calcCrc = App_Crc16(buf, n);
      if (calcCrc == rxCrc)
      {
        break;   /* 데이터 정상 → 기록으로 진행 */
      }

      /* 깨짐 → NACK, PC가 같은 청크를 재전송한다 */
      (void)t->send(&nack, 1U);
      if (++retries >= 5U)
      {
        /* [R3 진단] 5회 연속 실패 = 단순 비트오류가 아니다. 실제 수신 내용을 찍어
         * '스트림이 밀렸는지(정렬 어긋남)'를 판별한다.
         *   · 재전송인데 매번 rx/calc가 '똑같다'  → PC가 보낸 것을 그대로 받았는데
         *                                           길이/경계 해석이 어긋난 경우
         *   · 매번 값이 '달라진다'                → 타이밍성 유실
         * head는 청크 선두 4바이트, tail은 말미 4바이트다. */
        Dbg_Puts("\r\n*** CHUNK FAIL  n=");
        Dbg_PutU32(n);
        Dbg_Puts(" rxCrc=");
        Dbg_PutHex32(rxCrc);
        Dbg_Puts(" calcCrc=");
        Dbg_PutHex32(calcCrc);
        Dbg_Puts(" head=");
        Dbg_PutHex32(((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                     ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3]);
        Dbg_Puts(" tail=");
        Dbg_PutHex32(((uint32_t)buf[n - 4] << 24) | ((uint32_t)buf[n - 3] << 16) |
                     ((uint32_t)buf[n - 2] << 8)  |  (uint32_t)buf[n - 1]);
        Dbg_Puts("\r\n");
        goto stop;                      /* 같은 청크 5회 실패 → 포기 */
      }
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

  /* 4) 수신 완료 → 기록한 영역 전체 CRC 검증 (깨진 전송 차단) */
  if (FlashIf_Crc32((const uint8_t *)baseAddr, total) != expectCrc)
  {
    /* 불일치 → 플래그를 남기지 않는다. 깨진 펌웨어는 절대 적용되지 않음. */
    (void)t->send((const uint8_t *)crcerr, sizeof(crcerr) - 1U);
    goto stop;
  }
  if (!t->send((const uint8_t *)done, sizeof(done) - 1U)) goto stop;

  /* 5) 마무리 — 여기서만 대상별로 동작이 갈린다. */
  if (toFactory)
  {
    /* Factory 교체는 '지금 실행 중인 펌웨어'와 무관한 정비 작업이다.
     * 메타데이터를 건드리지 않고 재부팅도 하지 않는다 → 호출자에게 정상 복귀하며,
     * 호출자가 OTA 뮤텍스를 풀고 계속 서비스한다. */
    Dbg_Puts("\r\n[APP] FACTORY image replaced (");
    Dbg_PutU32(total);
    Dbg_Puts(" bytes) - rollback target changed\r\n");

    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);   /* 파랑 끔 */
    return;
  }

  /* --- 이하 STAGING(정상 OTA) 경로 --- */

  /* '업데이트 대기' 플래그(메타데이터) 기록 후 재부팅 → 부트로더가 적용 */
  {
    FwMeta meta;
    meta.magic    = FW_UPDATE_MAGIC;
    meta.state    = FW_STATE_PENDING;   /* 부트로더에게 '적용 예정'을 알림 */
    meta.size     = total;
    meta.crc      = expectCrc;          /* 부트로더가 복사 전/후 검증에 사용 */
    meta.attempts = 0U;
    meta.reserved = 0U;

    /* Metadata(0x0801_0000)는 Bank1 = 앱이 실행되는 뱅크다. 소거 중 CPU가 멈춰
     * 어떤 태스크도 못 도므로 체크인이 아니라 직접 갱신으로 창을 새로 연다. */
    IWDG->KR = 0x0000AAAAU;
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
  /* [R3] 실패 경로. 베어메탈에서는 여기서 그냥 while(1)로 멈추면 '아무도 pet하지 않으니'
   * 워치독이 물어 롤백으로 이어졌다. RTOS에서는 다른 태스크가 여전히 체크인하므로
   * 그대로 두면 wdgTask가 계속 pet해 자동 롤백이 조용히 무력화된다.
   * → Wdg_Panic()으로 pet을 영구 중단시켜 베어메탈과 동일한 의미를 복원한다. */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);   /* 파랑 유지 (오류 시) */

  /* 실패 원인 규명용 수치. 이미 프로토콜이 깨진 뒤이므로 UART로 찍어도 무방하다.
   *   offset  : Staging 기준 어디까지 기록했는지(= 실패한 청크 위치)
   *   dropped : StreamBuffer가 가득 차 버린 바이트 수(>0 이면 버퍼 부족)
   *   uartErr : UART 오류 횟수와 마지막 ErrorCode(>0 이면 ORE 등으로 수신이 어긋남)
   *             HAL_UART_ERROR_PE=1 NE=2 FE=4 ORE=8 DMA=16 */
  Dbg_Puts("\r\n*** OTA FAIL  target=");
  Dbg_Puts(toFactory ? "FACTORY" : "STAGING");
  Dbg_Puts("  offset=");
  Dbg_PutU32(addr - baseAddr);
  Dbg_Puts("  dropped=");
  Dbg_PutU32(UartLink_Dropped());
  Dbg_Puts("  uartErr=");
  Dbg_PutU32(UartLink_Errors());
  Dbg_Puts(" code=");
  Dbg_PutHex32(UartLink_LastErrorCode());
  Dbg_Puts("\r\n");

  if (toFactory)
  {
    /* Factory 기록 실패는 '지금 돌고 있는 앱이 고장났다'는 뜻이 아니다. 정비 작업이
     * 실패했을 뿐이므로 panic(=강제 리셋·롤백)을 걸지 않고 정상 복귀해 계속 서비스한다.
     * (다만 Factory가 부분적으로 지워졌을 수 있으니 다시 기록해 두는 것이 좋다) */
    Dbg_Puts("*** Factory image may be INCOMPLETE - rewrite it\r\n");
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
    return;
  }

  Wdg_Panic("OTA download failed");
  vTaskSuspend(NULL);                                        /* 이 태스크는 여기서 끝 */
  while (1) { }                                              /* 도달 불가 */
}

/**
  * @brief  사용자 요청에 의한 강제 롤백 — 다음 부팅에서 Factory→App 복원을 유도한다.
  *
  * @note   왜 필요한가: 자동 롤백은 '앱이 자가확인에 실패(=사실상 멈춤)'했을 때만 발동한다.
  *         앱이 살아서 통신은 되는데 기능이 잘못된 경우에는 워치독이 개입하지 않으므로,
  *         사용자가 UI에서 직접 되돌릴 수단이 필요하다.
  *
  * @note   부트로더를 수정하지 않는다. 부트로더의 기존 상태머신이
  *           TRIAL + attempts >= FW_MAX_TRIALS  →  BL_Rollback()
  *         이므로, 앱이 그 상태를 그대로 써 넣고 재부팅하면 롤백이 수행된다.
  *         (BL_Rollback은 size/crc 없이 Factory 512KB를 통째로 복사한다)
  *
  * @note   안전장치: Factory가 비어 있거나 깨져 있으면 거부한다. 그대로 롤백하면 App이
  *         0xFF로 덮여 부팅 불가가 되고 ST-Link로만 복구할 수 있다.
  */
static void App_RequestRollback(const FwTransport *t)
{
  const char done[]   = "DONE\r\n";
  const char failed[] = "FAILED\r\n";

  /* Factory 유효성 검사: 이미지 선두 4바이트는 초기 스택 포인터여야 하고,
   * 그 값이 RAM 범위(0x2000_0000~0x2003_0000) 안에 있어야 쓸 만한 이미지다.
   * (부트로더가 앱 유효성을 판단할 때 쓰는 것과 같은 기준) */
  uint32_t factorySp = *(const uint32_t *)FACTORY_ADDRESS;
  if ((factorySp < 0x20000000U) || (factorySp > 0x20030000U))
  {
    Dbg_Puts("\r\n[APP] rollback REFUSED - FACTORY slot is empty/invalid\r\n");
    (void)t->send((const uint8_t *)failed, sizeof(failed) - 1U);
    return;
  }

  {
    FwMeta meta;
    meta.magic    = FW_UPDATE_MAGIC;
    meta.state    = FW_STATE_TRIAL;      /* '시험 중' 상태로 두고 */
    meta.attempts = FW_MAX_TRIALS;       /* 시도횟수를 이미 소진한 것으로 표시 */
    meta.size     = 0U;                  /* 롤백 경로에서는 쓰이지 않는다 */
    meta.crc      = 0U;
    meta.reserved = 0U;

    /* Metadata(0x0801_0000)는 Bank1 = 앱이 실행되는 뱅크다. 소거 중 CPU가 멈춰
     * 어떤 태스크도 못 도므로 체크인이 아니라 직접 갱신으로 창을 새로 연다. */
    IWDG->KR = 0x0000AAAAU;
    FlashIf_WriteMeta(&meta);
  }

  Dbg_Puts("\r\n[APP] rollback requested - rebooting to restore FACTORY\r\n");
  (void)t->send((const uint8_t *)done, sizeof(done) - 1U);

  /* 응답이 실제로 상대에 도달하도록 잠시 전송계층을 돌린다(TCP는 flush/재전송 시간 필요). */
  {
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < 150U) { t->pet(); }
  }

  NVIC_SystemReset();   /* 돌아오지 않음 */
}

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
      App_RequestRollback(&uartTransport);   /* 성공 시 재부팅, 거부되면 복귀 */
    }
    else
    {
      App_EnterDownloadMode(&uartTransport,
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
  *         그대로 남아 App_EnterDownloadMode의 t->recv가 이어서 읽는다.
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
        App_RequestRollback(&tcpTransport);   /* 성공 시 재부팅, 거부되면 복귀 */
      }
      else
      {
        App_EnterDownloadMode(&tcpTransport,
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
