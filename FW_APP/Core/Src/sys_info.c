/**
  ******************************************************************************
  * @file    sys_info.c
  * @brief   보드/장치 정보 + 메모리 사용량 보고. 설계 배경은 sys_info.h 참고.
  ******************************************************************************
  */
#include "sys_info.h"
#include "main.h"
#include "flash_if.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 *  링커 심볼 — 값이 아니라 '주소'가 정보다(그래서 &를 취해 쓴다).
 *  STM32F429ZITX_FLASH.ld 가 정의한다. 이름이 바뀌면 여기도 같이 바꿔야 한다.
 * ------------------------------------------------------------------------- */
extern uint32_t _sdata;      /* .data 시작 (RAM)            */
extern uint32_t _edata;      /* .data 끝   (RAM)            */
extern uint32_t _sbss;       /* .bss  시작 (RAM)            */
extern uint32_t _ebss;       /* .bss  끝   (RAM)            */
extern uint32_t _sccmram;    /* .ccmram 시작 (CCM)          */
extern uint32_t _eccmram;    /* .ccmram 끝   (CCM)          */
extern uint32_t _siccmram;   /* .ccmram 초기값의 Flash 적재 주소 */

/* 메모리 영역 (링커 MEMORY 블록과 일치해야 한다) */
#define RAM_ORIGIN      0x20000000UL
#define RAM_SIZE        (192UL * 1024UL)
#define CCM_ORIGIN      0x10000000UL
#define CCM_SIZE        (64UL * 1024UL)

/* 출하 시 각인된 정보 (STM32F42x/F43x 기준) */
#define FLASH_SIZE_REG  (*(volatile uint16_t *)0x1FFF7A22UL)   /* 단위: KB */
#define UID_BASE_ADDR   0x1FFF7A10UL

/* 한 줄 조립 버퍼. netTask 스택 여유가 얇아 넉넉히 잡지 않는다. */
#define LINE_MAX        96U

/* --- 작은 문자열 조립 헬퍼 (printf를 쓰지 않는다: newlib 재진입/힙 회피) --- */

static uint32_t sicat(char *dst, uint32_t n, uint32_t cap, const char *src)
{
  while ((*src != '\0') && (n < (cap - 1U))) { dst[n++] = *src++; }
  dst[n] = '\0';
  return n;
}

static uint32_t si_u32(char *dst, uint32_t n, uint32_t cap, uint32_t v)
{
  char tmp[11];
  uint32_t k = 0U;

  if (v == 0U) { if (n < (cap - 1U)) { dst[n++] = '0'; } dst[n] = '\0'; return n; }
  while ((v > 0U) && (k < sizeof(tmp))) { tmp[k++] = (char)('0' + (v % 10U)); v /= 10U; }
  while ((k > 0U) && (n < (cap - 1U))) { dst[n++] = tmp[--k]; }
  dst[n] = '\0';
  return n;
}

static uint32_t si_hex(char *dst, uint32_t n, uint32_t cap, uint32_t v, uint8_t digits)
{
  static const char hex[] = "0123456789ABCDEF";
  for (int8_t i = (int8_t)(digits - 1U); i >= 0; i--)
  {
    if (n < (cap - 1U)) { dst[n++] = hex[(v >> (4 * i)) & 0xFU]; }
  }
  dst[n] = '\0';
  return n;
}

/** @brief 바이트를 "123.4" (KB, 소수 1자리)로 붙인다. 부동소수점을 쓰지 않는다. */
static uint32_t si_kb(char *dst, uint32_t n, uint32_t cap, uint32_t bytes)
{
  uint32_t tenths = (bytes * 10U + 512U) / 1024U;   /* 반올림 */
  n = si_u32(dst, n, cap, tenths / 10U);
  n = sicat(dst, n, cap, ".");
  return si_u32(dst, n, cap, tenths % 10U);
}

/** @brief "used / total KB (xx.x%)" 한 덩어리. */
static uint32_t si_usage(char *dst, uint32_t n, uint32_t cap, uint32_t used, uint32_t total)
{
  n = si_kb(dst, n, cap, used);
  n = sicat(dst, n, cap, " / ");
  n = si_kb(dst, n, cap, total);
  n = sicat(dst, n, cap, " KB (");

  /* 백분율도 정수 연산으로. total이 0이면 0%로 둔다(방어). */
  uint32_t pct10 = (total > 0U) ? (uint32_t)(((uint64_t)used * 1000U) / total) : 0U;
  n = si_u32(dst, n, cap, pct10 / 10U);
  n = sicat(dst, n, cap, ".");
  n = si_u32(dst, n, cap, pct10 % 10U);
  return sicat(dst, n, cap, "%)");
}

/** @brief DEV_ID로 장치 계열 이름을 고른다. 이 프로젝트는 F429뿐이지만 값을 눈으로 확인하려고 둔다. */
static const char *si_device_name(uint16_t devId)
{
  switch (devId)
  {
    case 0x419: return "STM32F42xxx/F43xxx";
    case 0x413: return "STM32F405/407/415/417";
    case 0x431: return "STM32F411xx";
    case 0x434: return "STM32F469/479";
    default:    return "Unknown";
  }
}

void SysInfo_Report(SysInfo_EmitFn emit, void *ctx)
{
  char     line[LINE_MAX];
  uint32_t n;

  if (emit == NULL) return;

  const uint32_t idcode = DBGMCU->IDCODE;
  const uint16_t devId  = (uint16_t)(idcode & 0x0FFFU);
  const uint16_t revId  = (uint16_t)(idcode >> 16);

  /* --- 장치 식별 --- */
  emit(ctx, "[SYS] Board       : NUCLEO-F429ZI\r\n");

  n = 0U;
  n = sicat(line, n, LINE_MAX, "[SYS] Device name : ");
  n = sicat(line, n, LINE_MAX, si_device_name(devId));
  n = sicat(line, n, LINE_MAX, " (id=0x");
  n = si_hex(line, n, LINE_MAX, devId, 3U);
  n = sicat(line, n, LINE_MAX, " rev=0x");
  n = si_hex(line, n, LINE_MAX, revId, 4U);
  n = sicat(line, n, LINE_MAX, ")\r\n");
  emit(ctx, line);

  emit(ctx, "[SYS] Device type : MCU\r\n");
  emit(ctx, "[SYS] Device CPU  : Cortex-M4\r\n");

  n = 0U;
  n = sicat(line, n, LINE_MAX, "[SYS] NVM size    : ");
  n = si_u32(line, n, LINE_MAX, (uint32_t)FLASH_SIZE_REG);
  n = sicat(line, n, LINE_MAX, " KB\r\n");
  emit(ctx, line);

  /* 96비트 UID — 보드를 개체 단위로 구분해야 할 때 쓴다 */
  {
    const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE_ADDR;
    n = 0U;
    n = sicat(line, n, LINE_MAX, "[SYS] Unique ID   : ");
    n = si_hex(line, n, LINE_MAX, uid[0], 8U);
    n = sicat(line, n, LINE_MAX, "-");
    n = si_hex(line, n, LINE_MAX, uid[1], 8U);
    n = sicat(line, n, LINE_MAX, "-");
    n = si_hex(line, n, LINE_MAX, uid[2], 8U);
    n = sicat(line, n, LINE_MAX, "\r\n");
    emit(ctx, line);
  }

  /* --- 메모리 사용량 ---
   * FLASH: 이 앱 이미지가 Flash에서 차지하는 크기.
   *
   * ⚠️ `_etext`를 끝으로 삼으면 안 된다. 링커 스크립트에서 `_etext` **뒤에**
   *    `.rodata`(문자열·상수 테이블) / `.ARM.extab` / `.ARM.exidx` /
   *    `.preinit_array` / `.init_array` / `.fini_array` 가 더 실린다.
   *    lwIP·HAL이 들어간 이 프로젝트는 `.rodata`만 십수 KB라 크게 어긋난다
   *    (실측: _etext 기준 125.8KB vs 실제 이미지 ≈140KB).
   *
   * `.ccmram`이 **마지막 적재 섹션**이므로 그 적재 주소 + 크기가 이미지의 끝이다.
   * 이렇게 잡으면 중간 섹션을 하나하나 세지 않아도 전부 포함된다.
   *
   * 분모는 칩 전체(2MB)가 아니라 **App 슬롯 512KB**다 — 넘치면 Factory를 침범하므로
   * 실제로 지켜야 하는 한도가 그쪽이다. */
  {
    const uint32_t ccmSz  = (uint32_t)&_eccmram - (uint32_t)&_sccmram;
    const uint32_t flashEnd = (uint32_t)&_siccmram + ccmSz;   /* 이미지 끝(Flash) */
    const uint32_t flashUsed = flashEnd - APP_ADDRESS;

    n = 0U;
    n = sicat(line, n, LINE_MAX, "[MEM] FLASH  : ");
    n = si_usage(line, n, LINE_MAX, flashUsed, APP_SIZE);
    n = sicat(line, n, LINE_MAX, "  <- App slot\r\n");
    emit(ctx, line);

    /* RAM: .data + .bss (정적 할당). FreeRTOS 힙도 .bss 안의 배열이라 여기 포함된다.
     * 스택은 RAM 최상단에서 아래로 자라므로 이 수치에 잡히지 않는다. */
    const uint32_t ramStatic = (uint32_t)&_ebss - RAM_ORIGIN;
    n = 0U;
    n = sicat(line, n, LINE_MAX, "[MEM] RAM    : ");
    n = si_usage(line, n, LINE_MAX, ramStatic, RAM_SIZE);
    n = sicat(line, n, LINE_MAX, "  <- static(.data+.bss)\r\n");
    emit(ctx, line);

    n = 0U;
    n = sicat(line, n, LINE_MAX, "[MEM] CCMRAM : ");
    n = si_usage(line, n, LINE_MAX, ccmSz, CCM_SIZE);
    n = sicat(line, n, LINE_MAX, "\r\n");
    emit(ctx, line);
  }

  /* 힙은 위 RAM 수치에 이미 '전체 크기'로 포함돼 있다. 여기서는 그 안에서 실제로
   * 얼마나 쓰고 있는지를 따로 보여준다(태스크 스택이 여기서 할당된다). */
  {
    const uint32_t heapTotal = (uint32_t)configTOTAL_HEAP_SIZE;
    const uint32_t heapFree  = (uint32_t)xPortGetFreeHeapSize();
    const uint32_t heapMin   = (uint32_t)xPortGetMinimumEverFreeHeapSize();

    n = 0U;
    n = sicat(line, n, LINE_MAX, "[MEM] HEAP   : ");
    n = si_usage(line, n, LINE_MAX, heapTotal - heapFree, heapTotal);
    n = sicat(line, n, LINE_MAX, "  min free=");
    n = si_u32(line, n, LINE_MAX, heapMin);
    n = sicat(line, n, LINE_MAX, "B\r\n");
    emit(ctx, line);
  }
}
