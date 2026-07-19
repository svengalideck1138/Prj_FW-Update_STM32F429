/**
  ******************************************************************************
  * @file    ota.c
  * @brief   OTA 다운로드 프로토콜 구현. 설계 배경은 ota.h 참고.
  *
  *  main.c에서 그대로 옮겨온 코드다(동작 변경 없음).
  ******************************************************************************
  */
#include "ota.h"

#include "main.h"          /* HAL, IWDG->KR, LDx_GPIO_Port/Pin */
#include "cmsis_os.h"      /* osDelay */
#include "flash_if.h"      /* FlashIf_* (erase/write/crc32/meta) */
#include "dbg_uart.h"      /* Dbg_Puts / Dbg_PutU32 / Dbg_PutHex32 */
#include "wdg_monitor.h"   /* Wdg_Panic */
/* 실패 진단에서 UART 링크 통계(누락/오류 카운터)를 찍는다.
 * ⚠️ 전송 계층에 무관해야 할 코드가 UART에만 있는 값을 참조한다 — TCP 세션이 실패해도
 *    UART 카운터가 찍힌다. 옮기면서 발견했으나 동작을 바꾸지 않으려고 그대로 두었다. */
#include "uart_link.h"

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

/* ===========================================================================
 *  다운로드 세션
 *
 *  예전에는 한 함수(252줄)가 헤더수신·소거·청크·검증·마무리를 모두 했고, 실패는
 *  전부 `goto stop` 한 곳으로 모였다. 단계별로 쪼개면서 그 자리를 대신하는 것이
 *  이 구조체다 — 각 단계는 bool을 돌려주고, 실패 진단에 필요한 상태(어디까지
 *  기록했는지 등)는 여기에 남는다.
 * =========================================================================== */
typedef struct {
  const FwTransport *t;
  bool     toFactory;
  uint32_t baseAddr;      /* 기록 시작 주소 (Staging 또는 Factory) */
  uint32_t maxSize;       /* 대상 슬롯 크기 */
  uint32_t total;         /* 헤더로 받은 이미지 크기 */
  uint32_t expectCrc;     /* 헤더로 받은 전체 CRC32 */
  uint32_t addr;          /* 현재 기록 위치 — 실패 진단의 offset 계산에 쓴다 */
} OtaSession;

/* 프로토콜 상수 (PC 쪽 UI_Monitor와 값이 일치해야 한다) */
static const uint8_t OTA_ACK  = 0x79U;
static const uint8_t OTA_NACK = 0x1FU;

/**
  * @brief  1) 헤더 수신 — [전체크기 4B][CRC32 4B], little-endian.
  * @retval 크기가 0이거나 슬롯보다 크면 NACK를 보내고 false.
  */
static bool ota_recv_header(OtaSession *s)
{
  uint8_t hdr[8];

  if (!s->t->recv(hdr, 8U, 0U)) return false;

  s->total = (uint32_t)hdr[0]
           | ((uint32_t)hdr[1] << 8)
           | ((uint32_t)hdr[2] << 16)
           | ((uint32_t)hdr[3] << 24);
  s->expectCrc = (uint32_t)hdr[4]
               | ((uint32_t)hdr[5] << 8)
               | ((uint32_t)hdr[6] << 16)
               | ((uint32_t)hdr[7] << 24);

  if ((s->total == 0U) || (s->total > s->maxSize))
  {
    (void)s->t->send(&OTA_NACK, 1U);
    return false;
  }
  return true;
}

/**
  * @brief  2) 대상 영역 소거 — 반드시 '섹터 단위'로 쪼갠다.
  *
  * 한 번의 FlashIf_EraseRange로 최대 4섹터를 지우면 128KB 섹터당 typ 1s / max 4s이므로
  * 최악 16초 동안 블로킹된다. 섹터마다 쪼개면 각 구간이 1섹터 시간으로 제한된다.
  *
  * 여기서 쪼개는 목적은 **태스크 체크인과 CPU 양보**다. 워치독(IWDG) 자체는 별개 문제이며
  * FlashIf_EraseSectorFromRam()이 RAM에서 폴링하며 갱신하므로 소거 시간과 무관하게 안전하다
  * (자세한 배경은 flash_if.c의 해당 함수 주석 참고).
  *
  * @retval 성공 시 ACK까지 보내고 true. 실패하면 NACK를 보내고 false.
  */
static bool ota_erase(OtaSession *s)
{
  const uint32_t endAddr = s->baseAddr + s->total - 1U;
  bool           eraseOk = true;

  for (uint32_t a = s->baseAddr; a <= endAddr; a = FlashIf_NextSectorAddr(a))
  {
    /* 워치독 자체는 FlashIf_EraseSectorFromRam()이 책임진다 — 소거 중 BSY 폴링 루프가
     * RAM에서 돌며 IWDG를 계속 갱신하므로, 섹터 소거가 얼마나 걸리든 물리지 않는다.
     * 여기서 하는 것은 '태스크 체크인'뿐이다(둘은 다른 문제다. 아래 osDelay 주석 참고). */
    s->t->pet();                                  /* 섹터마다 체크인 */

    if (FlashIf_EraseRange(a, a) != FLASH_IF_OK)  /* a가 속한 섹터 1개만 소거 */
    {
      eraseOk = false;
      break;
    }

    /* ★ 섹터 사이에서 반드시 양보한다.
     * 소거 루틴은 BSY를 busy-wait 폴링하므로 CPU를 놓지 않는다. 이 태스크
     * (Normal)보다 낮은 ledTask(Low)는 그동안 아예 스케줄되지 못해 체크인이 끊기고,
     * 여러 섹터를 연속 소거하면 그 굶주림이 수 초~십수 초로 누적된다.
     * osDelay(1)로 한 틱 양보하면 낮은 우선순위 태스크가 밀린 체크인을 처리할 수 있어,
     * ledTask가 견뎌야 하는 시간이 '섹터 1개분'으로 제한된다. */
    osDelay(1);
  }

  s->t->pet();

  if (!eraseOk)
  {
    (void)s->t->send(&OTA_NACK, 1U);
    return false;
  }
  return s->t->send(&OTA_ACK, 1U);                /* 지우기 완료 ACK */
}

/**
  * @brief  같은 청크가 CRC를 통과할 때까지 재수신한다(최대 5회).
  * @param  buf  수신 버퍼(호출자 소유, 최소 n바이트)
  * @retval 통과하면 true. 5회 실패하면 진단을 찍고 false.
  */
static bool ota_recv_one_chunk(OtaSession *s, uint8_t *buf, uint32_t n)
{
  uint8_t retries = 0U;

  for (;;)
  {
    uint8_t crcBuf[2];

    if (!s->t->recv(buf, n, 0U))    return false;
    if (!s->t->recv(crcBuf, 2U, 0U)) return false;

    uint16_t rxCrc   = (uint16_t)crcBuf[0] | ((uint16_t)crcBuf[1] << 8);
    uint16_t calcCrc = App_Crc16(buf, n);
    if (calcCrc == rxCrc) return true;            /* 정상 → 기록으로 진행 */

    /* 깨짐 → NACK, PC가 같은 청크를 재전송한다 */
    (void)s->t->send(&OTA_NACK, 1U);

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
      return false;                               /* 같은 청크 5회 실패 → 포기 */
    }
  }
}

/**
  * @brief  3) 청크 루프 — 수신·검증·기록을 total 바이트만큼 반복한다.
  * @note   256B 버퍼가 이 함수의 지역변수다. 세션 전 구간이 아니라 이 호출 동안만
  *         스택을 점유한다.
  */
static bool ota_recv_chunks(OtaSession *s)
{
  uint8_t  buf[256];
  uint32_t remaining = s->total;

  s->addr = s->baseAddr;

  while (remaining > 0U)
  {
    uint32_t n = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;

    s->t->pet();   /* 청크마다 워치독 갱신(+TCP 펌핑) */

    if (!ota_recv_one_chunk(s, buf, n)) return false;

    /* Flash는 4바이트 단위 → 꼬리는 0xFF로 패딩 후 기록 */
    uint32_t nAligned = (n + 3U) & ~3U;
    for (uint32_t i = n; i < nAligned; i++) buf[i] = 0xFFU;

    if (FlashIf_Write(s->addr, buf, nAligned) != FLASH_IF_OK)
    {
      (void)s->t->send(&OTA_NACK, 1U);
      return false;
    }
    if (!s->t->send(&OTA_ACK, 1U)) return false;  /* 청크 확정 ACK */

    s->addr   += n;
    remaining -= n;
    HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);   /* 진행 표시 */
  }
  return true;
}

/**
  * @brief  4) 기록한 영역 전체 CRC32 검증 → DONE 송신.
  * @retval 불일치면 CRCERR를 보내고 false(플래그를 남기지 않으므로 절대 적용되지 않는다).
  */
static bool ota_verify(OtaSession *s)
{
  static const char done[]   = "DONE\r\n";
  static const char crcerr[] = "CRCERR\r\n";

  if (FlashIf_Crc32((const uint8_t *)s->baseAddr, s->total) != s->expectCrc)
  {
    (void)s->t->send((const uint8_t *)crcerr, sizeof(crcerr) - 1U);
    return false;
  }
  return s->t->send((const uint8_t *)done, sizeof(done) - 1U);
}

/**
  * @brief  5) 마무리 — 여기서만 대상별로 동작이 갈린다.
  * @note   STAGING 경로는 재부팅하므로 **돌아오지 않는다.**
  */
static void ota_finish(OtaSession *s)
{
  if (s->toFactory)
  {
    /* Factory 교체는 '지금 실행 중인 펌웨어'와 무관한 정비 작업이다.
     * 메타데이터를 건드리지 않고 재부팅도 하지 않는다 → 호출자에게 정상 복귀하며,
     * 호출자가 OTA 뮤텍스를 풀고 계속 서비스한다. */
    Dbg_Puts("\r\n[APP] FACTORY image replaced (");
    Dbg_PutU32(s->total);
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
    meta.size     = s->total;
    meta.crc      = s->expectCrc;       /* 부트로더가 복사 전/후 검증에 사용 */
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
    while ((HAL_GetTick() - t0) < 150U) { s->t->pet(); }
  }

  /* 전송 계층을 조용히 만든 뒤 리셋한다(TCP면 소켓 정상 종료). 그냥 리셋하면 상대가
   * 재전송을 계속 쏘는 와중에 PHY가 하드웨어 리셋돼 오토네고가 깨진다 — 재부팅 후
   * 링크 LED가 안 켜지던 원인. 자세한 내용은 ota.h의 FwTransport 주석 참고. */
  if (s->t->quiesce != NULL) { s->t->quiesce(); }
  {
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < 100U) { s->t->pet(); }   /* FIN이 나갈 시간 */
  }

  NVIC_SystemReset();      /* 재부팅 (돌아오지 않음) */
}

/**
  * @brief  실패 경로 — 진단을 찍고, 대상에 따라 복귀하거나 정지한다.
  *
  * @note   [R3] 베어메탈에서는 실패 시 while(1)로 멈추면 '아무도 pet하지 않으니'
  *         워치독이 물어 롤백으로 이어졌다. RTOS에서는 다른 태스크가 여전히 체크인하므로
  *         그대로 두면 wdgTask가 계속 pet해 자동 롤백이 조용히 무력화된다.
  *         → Wdg_Panic()으로 pet을 영구 중단시켜 베어메탈과 동일한 의미를 복원한다.
  */
static void ota_report_failure(const OtaSession *s)
{
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);   /* 파랑 유지 (오류 시) */

  /* 실패 원인 규명용 수치. 이미 프로토콜이 깨진 뒤이므로 UART로 찍어도 무방하다.
   *   offset  : 대상 슬롯 기준 어디까지 기록했는지(= 실패한 청크 위치)
   *   dropped : StreamBuffer가 가득 차 버린 바이트 수(>0 이면 버퍼 부족)
   *   uartErr : UART 오류 횟수와 마지막 ErrorCode(>0 이면 ORE 등으로 수신이 어긋남)
   *             HAL_UART_ERROR_PE=1 NE=2 FE=4 ORE=8 DMA=16 */
  Dbg_Puts("\r\n*** OTA FAIL  target=");
  Dbg_Puts(s->toFactory ? "FACTORY" : "STAGING");
  Dbg_Puts("  offset=");
  Dbg_PutU32(s->addr - s->baseAddr);
  Dbg_Puts("  dropped=");
  Dbg_PutU32(UartLink_Dropped());
  Dbg_Puts("  uartErr=");
  Dbg_PutU32(UartLink_Errors());
  Dbg_Puts(" code=");
  Dbg_PutHex32(UartLink_LastErrorCode());
  Dbg_Puts("\r\n");

  if (s->toFactory)
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
  * @brief  세션 본체 — 단계를 순서대로 밟는다. 어느 단계든 실패하면 즉시 false.
  * @note   STAGING 성공 시 ota_finish()가 재부팅하므로 이 함수는 돌아오지 않는다.
  */
static bool ota_run(OtaSession *s)
{
  static const char ready[] = "READY\r\n";

  if (!s->t->send((const uint8_t *)ready, sizeof(ready) - 1U)) return false;
  if (!ota_recv_header(s))  return false;
  if (!ota_erase(s))        return false;
  if (!ota_recv_chunks(s))  return false;
  if (!ota_verify(s))       return false;

  ota_finish(s);
  return true;
}

/**
  * @brief  "FWUPDATE"/"FWFACTRY" 수신 시 진입하는 다운로드 모드.
  * @note   프로토콜(자세한 내용은 README §3):
  *           MCU → "READY"
  *           PC  → [전체크기 4B][CRC32 4B]
  *           MCU → 대상 슬롯 소거 → ACK/NACK
  *           PC  → [데이터 256B][CRC16 2B] 반복, MCU는 청크마다 기록 후 ACK
  *           MCU → 전체 CRC32 검증 → "DONE" / "CRCERR"
  *         STAGING은 메타데이터 기록 후 재부팅(돌아오지 않음),
  *         FACTORY는 기록만 하고 복귀한다.
  */
void Ota_EnterDownloadMode(const FwTransport *t, FwTarget target)
{
  /* 모든 wire I/O는 전송 계층(t)을 통한다 → UART/TCP 동일 로직으로 동작.
   *   t->recv(buf,len,0)  : 정확 len 수신(무한 대기)
   *   t->send(buf,len)    : len 전량 송신
   *   t->pet()            : 워치독 갱신(+TCP면 lwIP 펌핑) */
  OtaSession s;
  s.t         = t;
  s.toFactory = (target == FW_TARGET_FACTORY);
  s.baseAddr  = s.toFactory ? FACTORY_ADDRESS : STAGING_ADDRESS;
  s.maxSize   = s.toFactory ? FACTORY_SIZE    : STAGING_SIZE;
  s.total     = 0U;
  s.expectCrc = 0U;
  s.addr      = s.baseAddr;   /* 실패 진단의 offset이 0이 되도록 미리 맞춰 둔다 */

  HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);   /* 초록 끔 */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);     /* 파랑: 다운로드 모드 */

  if (!ota_run(&s))
  {
    ota_report_failure(&s);
  }
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
void Ota_RequestRollback(const FwTransport *t)
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

  /* 리셋 전에 전송 계층을 조용히 만든다 — ota_finish와 같은 이유(ota.h 주석 참고). */
  if (t->quiesce != NULL) { t->quiesce(); }
  {
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < 100U) { t->pet(); }   /* FIN이 나갈 시간 */
  }

  NVIC_SystemReset();   /* 돌아오지 않음 */
}
