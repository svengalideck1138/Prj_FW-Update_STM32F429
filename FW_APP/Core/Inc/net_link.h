/**
  ******************************************************************************
  * @file    net_link.h
  * @brief   베어메탈(NO_SYS) lwIP RAW TCP 위의 "블로킹 전송 링크".
  *          OTA 프로토콜(App_EnterDownloadMode)이 UART처럼 고정 길이로
  *          recv/send 하도록, RX 링버퍼 + 블로킹 래퍼를 제공한다.
  *          (M2: TCP echo 검증 / M3: FwTransport의 TCP 백엔드로 재사용)
  ******************************************************************************
  */
#ifndef NET_LINK_H
#define NET_LINK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TCP 서버 리슨 포트 (C# UI_Monitor의 기본 접속 포트와 일치) */
#define NETLINK_PORT   7U

/** lwIP(MX_LWIP_Init) 초기화 후 1회 호출: TCP 리슨 시작. */
void     NetLink_Init(void);

/** 메인 루프/대기 루프에서 주기 호출: lwIP 서비스(수신/타이머/링크).
 *  NO_SYS이므로 이 호출이 없으면 스택이 전혀 진행되지 않는다. */
void     NetLink_Poll(void);

/** 클라이언트가 연결돼 있으면 true. */
bool     NetLink_Connected(void);

/** RX 링버퍼에 현재 쌓여 있는 바이트 수. */
uint32_t NetLink_Available(void);

/** 논블로킹 수신: 있는 만큼(최대 max) 읽어 실제 읽은 바이트 수를 반환. */
uint32_t NetLink_ReadAvailable(uint8_t *buf, uint32_t max);

/** 블로킹 수신: 정확히 len 바이트를 받을 때까지 대기.
 *  내부에서 lwIP 펌핑 + 워치독 갱신을 수행한다.
 *  @param timeoutMs 0이면 무한 대기. 연결 끊김 또는 타임아웃 시 false. */
bool     NetLink_Recv(uint8_t *buf, uint32_t len, uint32_t timeoutMs);

/** 블로킹 송신: len 바이트를 모두 송신 큐에 넣고 flush(tcp_output).
 *  내부에서 lwIP 펌핑 + 워치독 갱신. 실패/끊김/타임아웃 시 false. */
bool     NetLink_Send(const uint8_t *buf, uint32_t len);

/** 현재 연결을 닫는다(있을 때). */
void     NetLink_Close(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_LINK_H */
