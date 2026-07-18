/**
  ******************************************************************************
  * @file    uart_link.c
  * @brief   USART3 순환 DMA + IDLE → StreamBuffer 수신. 설계 배경은 uart_link.h 참고.
  ******************************************************************************
  */
#include "uart_link.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

/* DMA 착지 버퍼. 반드시 일반 SRAM(.bss)에 둘 것 — CCMRAM은 DMA가 접근하지 못한다. */
#define UART_DMA_BUF_SZ    512U
/* 소비자(uartTask)가 잠시 늦어도 흘리지 않도록 DMA 버퍼보다 크게 잡는다. */
#define UART_STREAM_SZ     1024U

extern UART_HandleTypeDef huart3;

static uint8_t              s_dmaBuf[UART_DMA_BUF_SZ];
static StreamBufferHandle_t s_rxStream = NULL;
static volatile uint16_t    s_lastPos  = 0U;   /* 지난번까지 StreamBuffer로 옮긴 위치 */
static volatile uint32_t    s_dropped  = 0U;
static volatile uint32_t    s_errors   = 0U;   /* UART 오류(ORE 등) 발생 횟수 */
static volatile uint32_t    s_lastErr  = 0U;   /* 마지막 HAL ErrorCode(누적 OR) */

/* ISR에서 StreamBuffer로 밀어 넣기. 넘치면 버리고 카운트만 올린다
 * (청크 CRC16 + NACK 재전송이 있어 치명적이지 않지만, 0이 아니면 버퍼가 부족하다는 신호). */
static void uart_push(const uint8_t *p, size_t n, BaseType_t *woken)
{
  if ((n == 0U) || (s_rxStream == NULL)) return;

  size_t sent = xStreamBufferSendFromISR(s_rxStream, p, n, woken);
  if (sent < n)
  {
    s_dropped += (uint32_t)(n - sent);
  }
}

bool UartLink_Init(void)
{
  if (s_rxStream == NULL)
  {
    /* trigger level 1: 1바이트만 들어와도 대기 중인 소비자를 깨운다(ACK 응답 지연 방지) */
    s_rxStream = xStreamBufferCreate(UART_STREAM_SZ, 1U);
    if (s_rxStream == NULL) return false;
  }

  s_lastPos = 0U;
  s_dropped = 0U;

  /* HT/TC/IDLE 어느 쪽이든 HAL_UARTEx_RxEventCallback을 부른다. */
  return (HAL_UARTEx_ReceiveToIdle_DMA(&huart3, s_dmaBuf, UART_DMA_BUF_SZ) == HAL_OK);
}

void UartLink_Flush(void)
{
  if (s_rxStream != NULL)
  {
    (void)xStreamBufferReset(s_rxStream);
  }
}

uint32_t UartLink_Dropped(void)
{
  return s_dropped;
}

uint32_t UartLink_Errors(void)
{
  return s_errors;
}

uint32_t UartLink_LastErrorCode(void)
{
  return s_lastErr;
}

bool UartLink_Recv(uint8_t *buf, uint32_t len, uint32_t timeoutMs)
{
  if ((buf == NULL) || (s_rxStream == NULL)) return false;
  if (len == 0U) return true;

  uint32_t   got   = 0U;
  TickType_t start = xTaskGetTickCount();
  TickType_t total = (timeoutMs == 0U) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);

  while (got < len)
  {
    TickType_t wait;

    if (timeoutMs == 0U)
    {
      wait = portMAX_DELAY;                 /* 무한 대기 */
    }
    else
    {
      TickType_t elapsed = xTaskGetTickCount() - start;   /* 언랩 안전 */
      if (elapsed >= total) return false;                 /* 총 데드라인 초과 */
      wait = total - elapsed;
    }

    size_t n = xStreamBufferReceive(s_rxStream, buf + got, (size_t)(len - got), wait);
    got += (uint32_t)n;
    /* n == 0 이면 타임아웃 — 루프 상단에서 데드라인을 다시 판정한다. */
  }

  return true;
}

bool UartLink_Send(const uint8_t *buf, uint32_t len)
{
  if ((buf == NULL) || (len == 0U)) return (len == 0U);

  return (HAL_UART_Transmit(&huart3, (uint8_t *)buf, (uint16_t)len, HAL_MAX_DELAY) == HAL_OK);
}

/* ===== HAL 콜백 ===== */

/**
  * @brief  수신 이벤트(HT/TC/IDLE) — '지난 위치 → 현재 위치' 구간을 StreamBuffer로 옮긴다.
  *
  * @note   ★ 인자 Size를 쓰지 않는다. ST HAL은 이벤트별로 Size의 의미가 다르다:
  *           · IDLE → 실제 DMA 기록 위치
  *           · HT   → 항상 고정 RxXferSize/2 (실제 위치 아님)
  *           · TC   → 항상 고정 RxXferSize   (실제 위치 아님)
  *         게다가 IDLE(USART3 IRQ)과 HT/TC(DMA1 IRQ)는 선점 우선순위가 같아 처리 순서가
  *         뒤바뀔 수 있다. 그 경우 "IDLE로 lastPos=300이 된 뒤 HT가 256으로 들어옴"처럼
  *         작아진 값이 오는데, 이를 랩어라운드로 오인하면 버퍼를 한 바퀴 통째로 다시 밀어
  *         넣어 스트림이 영구히 어긋난다(재전송해도 복구 불가).
  *         → DMA 카운터에서 '진짜 위치'를 직접 계산하면 어떤 이벤트로 들어오든 항상 옳다.
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  (void)Size;
  if (huart->Instance != USART3) return;

  BaseType_t woken = pdFALSE;

  /* 남은 전송 카운트 → 현재 기록 위치 (순환 DMA에서 항상 유효) */
  uint32_t remain = __HAL_DMA_GET_COUNTER(huart->hdmarx);
  uint16_t pos    = (remain >= UART_DMA_BUF_SZ) ? 0U
                                                : (uint16_t)(UART_DMA_BUF_SZ - remain);

  if (pos != s_lastPos)
  {
    if (pos > s_lastPos)
    {
      uart_push(&s_dmaBuf[s_lastPos], (size_t)(pos - s_lastPos), &woken);
    }
    else
    {
      /* 끝까지 밀고, 앞부분을 이어서 */
      uart_push(&s_dmaBuf[s_lastPos], (size_t)(UART_DMA_BUF_SZ - s_lastPos), &woken);
      if (pos > 0U)
      {
        uart_push(&s_dmaBuf[0], (size_t)pos, &woken);
      }
    }
    /* pos는 카운터에서 계산했으므로 항상 [0, UART_DMA_BUF_SZ-1] 범위다. */
    s_lastPos = pos;
  }

  portYIELD_FROM_ISR(woken);
}

/**
  * @brief  수신 오류(ORE/FE/NE 등) 시 DMA 수신을 되살린다.
  * @note   이 처리가 없으면 노이즈 한 번에 수신이 영구히 멈춘다.
  */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART3) return;

  s_errors++;
  s_lastErr |= huart->ErrorCode;

  /* ⚠️ 여기서 DMA를 재시작하면 진행 중이던 수신 위치(s_lastPos)가 0으로 돌아가고
   * 버퍼에 남아 있던 바이트가 버려져 '스트림이 어긋난다'. 그러면 이후 모든 청크 읽기가
   * 밀려서 재전송을 해도 복구되지 않는다(= 같은 오프셋에서 계속 실패).
   * 그래서 재시작은 '수신이 실제로 멈춘 경우'로 한정한다. ORE(오버런) 같은 오류는
   * 순환 DMA가 계속 돌고 있으므로 플래그만 지우고 그대로 둔다. */
  if (huart->RxState == HAL_UART_STATE_READY)
  {
    /* 수신이 정말 중단된 상태 → 되살린다 */
    s_lastPos = 0U;
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart3, s_dmaBuf, UART_DMA_BUF_SZ);
  }

  huart->ErrorCode = HAL_UART_ERROR_NONE;
}
