/**
  ******************************************************************************
  * @file    uart_link.h
  * @brief   USART3 수신을 '순환 DMA + IDLE 이벤트 → FreeRTOS StreamBuffer'로 처리한다.
  *
  *  왜 바꿨나:
  *   베어메탈 시절엔 메인 루프가 HAL_UART_Receive(..., 10ms)로 1바이트씩 폴링했고,
  *   이 블로킹 호출이 사실상 루프 전체의 속도를 정했다. RTOS에서는 그 방식이
  *   태스크를 10ms씩 붙잡아 두기만 할 뿐 이득이 없다.
  *
  *  구조:
  *   DMA가 s_dmaBuf에 순환 기록 → HT/TC/IDLE 시점마다 HAL_UARTEx_RxEventCallback이 떠서
  *   '지난번 위치~현재 위치' 구간을 StreamBuffer로 밀어 넣는다(ISR). 소비자(uartTask)는
  *   StreamBuffer에서 블로킹으로 꺼내 쓴다. IDLE 덕분에 8바이트짜리 "FWUPDATE" 같은
  *   짧은 전문도 지연 없이 올라오고, 청크 전송 중에는 DMA가 대량으로 받아 인터럽트가 적다.
  *
  *  송신은 폴링(HAL_UART_Transmit) 그대로 둔다 — 응답이 1~8바이트라 DMA 이득이 없다.
  *  ⚠️ USART3 송신자는 uartTask 하나여야 한다(부팅 배너/장애 보고 제외).
  ******************************************************************************
  */
#ifndef UART_LINK_H
#define UART_LINK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief StreamBuffer 생성 + 순환 DMA 수신 시작. 스케줄러 시작 후 uartTask에서 호출. */
bool     UartLink_Init(void);

/** @brief 수신 버퍼를 비운다(프로토콜 시작 전 잔여 바이트 제거). */
void     UartLink_Flush(void);

/**
  * @brief  정확히 len 바이트를 받을 때까지 블로킹 수신.
  * @param  timeoutMs 0이면 무한 대기(기존 HAL_MAX_DELAY 의미와 동일).
  *                   0이 아니면 '전체 호출'에 대한 총 데드라인이다.
  * @retval 전부 받았으면 true, 타임아웃이면 false.
  */
bool     UartLink_Recv(uint8_t *buf, uint32_t len, uint32_t timeoutMs);

/** @brief len 바이트 전량 송신(폴링). */
bool     UartLink_Send(const uint8_t *buf, uint32_t len);

/** @brief StreamBuffer가 가득 차 버려진 바이트 수(0이 아니면 버퍼 부족 신호). */
uint32_t UartLink_Dropped(void);

/** @brief UART 오류(ORE/FE/NE/PE) 발생 횟수. 0이 아니면 그 시점에 수신이 어긋났을 수 있다. */
uint32_t UartLink_Errors(void);

/** @brief 마지막 UART 오류의 HAL ErrorCode(누적 OR). 어떤 오류였는지 식별용. */
uint32_t UartLink_LastErrorCode(void);

#ifdef __cplusplus
}
#endif

#endif /* UART_LINK_H */
