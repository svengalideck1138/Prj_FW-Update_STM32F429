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

/**
  * @brief  "FWUPDATE" 명령 수신 시 진입하는 다운로드 모드. [4b 단계]
  * @note   프로토콜:
  *           PC → "READY" 응답을 보고 [전체크기 4B, LE] 전송
  *           MCU → Staging 지우기 → ACK/NACK
  *           PC → 256B 청크 반복 전송, MCU는 청크마다 Staging 기록 후 ACK
  *           MCU → 전부 받으면 "DONE" 전송
  *         [5단계 예정] DONE 후 플래그 세팅 + 재부팅. 지금은 파랑 켠 채 멈춤.
  */
void Ota_EnterDownloadMode(const FwTransport *t, FwTarget target)
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

  NVIC_SystemReset();   /* 돌아오지 않음 */
}
