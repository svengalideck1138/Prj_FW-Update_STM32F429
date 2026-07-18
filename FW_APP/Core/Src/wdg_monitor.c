/**
  ******************************************************************************
  * @file    wdg_monitor.c
  * @brief   태스크 체크인 기반 워치독 감시 구현. 설계 배경은 wdg_monitor.h 참고.
  ******************************************************************************
  */
#include "wdg_monitor.h"
#include "main.h"
#include "dbg_uart.h"

#include "FreeRTOS.h"
#include "task.h"

/* 감시 슬롯 수 — ledTask/uartTask/netTask 등. 여유 있게. */
#define WDG_MAX_SLOTS          8U

/* 감시 주기. IWDG 최악 창(LSI 47kHz 기준 ~2.7초)에 비해 충분히 짧다. */
#define WDG_MONITOR_PERIOD_MS  100U

typedef enum
{
  WDG_SLOT_FREE = 0,
  WDG_SLOT_ACTIVE,
  WDG_SLOT_SUSPENDED
} WdgSlotState;

typedef struct
{
  const char       *name;
  uint32_t          deadlineTicks;
  volatile uint32_t last;      /* 마지막 체크인 시각(tick) */
  volatile uint8_t  state;     /* WdgSlotState */
} WdgSlot;

static WdgSlot            s_slot[WDG_MAX_SLOTS];
static uint32_t           s_count       = 0U;
static volatile bool      s_panic       = false;
static const char        *s_panicReason = NULL;
static const char        *s_lateName    = NULL;

/* IWDG 갱신(pet). HAL 핸들 상태와 무관하게 동작하도록 레지스터에 직접 쓴다
 * (부트로더가 켠 워치독도 갱신해야 하기 때문). */
static inline void wdg_pet(void)
{
  IWDG->KR = 0x0000AAAAU;
}

WdgId Wdg_Register(const char *name, uint32_t deadlineMs)
{
  if (s_count >= WDG_MAX_SLOTS)
  {
    return WDG_INVALID_ID;
  }

  WdgId id = (WdgId)s_count;
  s_count++;

  s_slot[id].name          = name;
  s_slot[id].deadlineTicks = pdMS_TO_TICKS(deadlineMs);
  s_slot[id].last          = (uint32_t)xTaskGetTickCount();
  s_slot[id].state         = (uint8_t)WDG_SLOT_ACTIVE;

  return id;
}

void Wdg_CheckIn(WdgId id)
{
  if ((id < 0) || ((uint32_t)id >= s_count)) return;
  s_slot[id].last = (uint32_t)xTaskGetTickCount();
}

void Wdg_Suspend(WdgId id)
{
  if ((id < 0) || ((uint32_t)id >= s_count)) return;
  s_slot[id].state = (uint8_t)WDG_SLOT_SUSPENDED;
}

void Wdg_Resume(WdgId id)
{
  if ((id < 0) || ((uint32_t)id >= s_count)) return;
  s_slot[id].last  = (uint32_t)xTaskGetTickCount();   /* 복귀 즉시 기한 초과되지 않도록 */
  s_slot[id].state = (uint8_t)WDG_SLOT_ACTIVE;
}

bool Wdg_IsPanicked(void)
{
  return s_panic;
}

void Wdg_Panic(const char *reason)
{
  if (s_panic) return;          /* 래칭: 최초 1회만 보고 */
  s_panic       = true;
  s_panicReason = reason;

  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET);   /* 빨강 점등 = panic */

  /* 출력은 ASCII 영문으로 — 터미널 인코딩에 상관없이 읽히도록(한글은 ????로 깨진다) */
  Dbg_Puts("\r\n*** WDG PANIC: ");
  Dbg_Puts((reason != NULL) ? reason : "(unknown)");
  if (s_lateName != NULL)
  {
    Dbg_Puts(" [task: ");
    Dbg_Puts(s_lateName);
    Dbg_Puts("]");
  }
  Dbg_Puts("\r\n*** IWDG pet stopped -> reset & Factory rollback expected\r\n");
}

void Wdg_MonitorTask(void *argument)
{
  (void)argument;

  TickType_t       next   = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(WDG_MONITOR_PERIOD_MS);

  /* 등록~스케줄러 시작 사이의 지연을 흡수하기 위해 기준 시각을 지금으로 맞춘다. */
  {
    uint32_t now = (uint32_t)xTaskGetTickCount();
    for (uint32_t i = 0U; i < s_count; i++)
    {
      s_slot[i].last = now;
    }
  }

  for (;;)
  {
    vTaskDelayUntil(&next, period);

    if (!s_panic)
    {
      uint32_t now = (uint32_t)xTaskGetTickCount();

      for (uint32_t i = 0U; i < s_count; i++)
      {
        if (s_slot[i].state != (uint8_t)WDG_SLOT_ACTIVE) continue;

        /* 부호 없는 뺄셈이라 tick 랩어라운드에도 안전 */
        if ((now - s_slot[i].last) > s_slot[i].deadlineTicks)
        {
          s_lateName = s_slot[i].name;
          Wdg_Panic("task check-in timeout");
          break;
        }
      }
    }

    /* 모든 슬롯이 정상일 때만 pet한다.
     * panic 상태에서는 의도적으로 pet하지 않아 IWDG가 리셋시키도록 둔다. */
    if (!s_panic)
    {
      wdg_pet();
    }
  }
}
