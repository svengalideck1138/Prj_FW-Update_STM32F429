/**
  ******************************************************************************
  * @file    dbg_uart.h
  * @brief   USART3(115200)로 내보내는 최소 디버그 출력.
  *
  *  · printf를 쓰지 않는다 — newlib 재진입/힙 사용을 피하기 위함.
  *  · HAL 폴링 송신이라 스케줄러 시작 전/인터럽트 마스킹 상태/훅 안에서도 동작한다.
  *
  *  ⚠️ USART3는 R3부터 UART OTA 프로토콜이 쓰는 채널이다. 따라서 이 출력은
  *     '부팅 배너'와 '치명적 장애 보고'처럼 프로토콜과 겹치지 않는 시점에만 쓸 것.
  *     주기적 로그를 넣으면 OTA 청크 스트림이 깨진다.
  ******************************************************************************
  */
#ifndef DBG_UART_H
#define DBG_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 문자열 출력(널 종료). NULL은 무시. */
void Dbg_Puts(const char *s);

/** 32비트 값을 "0xXXXXXXXX" 형식으로 출력. */
void Dbg_PutHex32(uint32_t v);

/** 32비트 값을 10진수로 출력. */
void Dbg_PutU32(uint32_t v);

#ifdef __cplusplus
}
#endif

#endif /* DBG_UART_H */
