/**
  ******************************************************************************
  * @file    net_link.h
  * @brief   TCP 링크 (BSD 소켓 기반). 보드가 서버, PC(UI_Monitor)가 클라이언트.
  *
  *  [R4] 왜 다시 썼나:
  *   베어메탈(NO_SYS) 시절에는 lwIP RAW API + 4KB 링버퍼 + 수동 펌핑(MX_LWIP_Process)으로
  *   '블로킹 수신'을 흉내 내야 했다. FreeRTOS로 오면서 lwIP가 자체 tcpip_thread를 갖게 되어
  *   그 구조는 (a) 더 이상 필요 없고 (b) RAW API를 tcpip 스레드 밖에서 부르는 것이라
  *   스레드 안전하지도 않다. 소켓 API는 '정확히 n바이트 블로킹 수신'을 그대로 제공하므로
  *   링버퍼·백프레셔·펌핑 코드가 통째로 사라진다.
  *
  *  OTA 프로토콜은 UART와 완전히 동일하다(FwTransport 뒤에 숨는다).
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

/** @brief 내부 상태 초기화. 서버를 열기 전에 한 번 호출. */
void NetLink_Init(void);

/** @brief 리슨 소켓 생성(socket/bind/listen). 실패 시 false. */
bool NetLink_ServerOpen(void);

/**
  * @brief  클라이언트 접속을 수락한다.
  * @param  timeoutMs 이 시간 안에 접속이 없으면 false를 반환(호출자가 체크인하러 나올 수 있게).
  * @note   수락한 소켓에는 TCP_NODELAY를 건다. 이 프로토콜은 청크마다 1바이트 ACK를
  *         주고받는 stop-and-wait이라, Nagle이 켜져 있으면 지연 ACK와 맞물려
  *         매 청크가 수백 ms씩 밀려 전송이 수 분대로 무너진다.
  */
bool NetLink_Accept(uint32_t timeoutMs);

/** @brief 클라이언트가 연결돼 있으면 true. */
bool NetLink_Connected(void);

/** @brief 현재 클라이언트 연결만 닫는다(리슨 소켓은 유지). */
void NetLink_CloseClient(void);

/**
  * @brief  정확히 len 바이트를 받을 때까지 블로킹 수신.
  * @param  timeoutMs 0이면 무한 대기(UART의 HAL_MAX_DELAY와 같은 의미).
  *                   0이 아니면 '이 호출 전체'에 대한 총 데드라인이다.
  * @retval 전부 받으면 true. 타임아웃/연결종료/오류면 false.
  */
bool NetLink_Recv(uint8_t *buf, uint32_t len, uint32_t timeoutMs);

/**
  * @brief  있는 만큼만 수신(부분 수신 허용).
  * @retval >0 읽은 바이트 수, 0 타임아웃, -1 연결 종료/오류.
  */
int  NetLink_RecvSome(uint8_t *buf, uint32_t maxLen, uint32_t timeoutMs);

/** @brief len 바이트를 전부 송신. 실패/타임아웃 시 false. */
bool NetLink_Send(const uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* NET_LINK_H */
