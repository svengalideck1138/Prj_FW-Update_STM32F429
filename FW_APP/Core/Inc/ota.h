/**
  ******************************************************************************
  * @file    ota.h
  * @brief   OTA 다운로드 프로토콜 (UART/TCP 공통).
  *
  *  main.c에서 분리했다. 이 모듈은 '전송 계층'과 '어느 슬롯에 쓰는가'만 인자로 받고,
  *  그 밖의 것은 헤더로 공개된 모듈(flash_if / dbg_uart / wdg_monitor)만 쓴다.
  *  main.c의 파일 스코프 변수에는 전혀 의존하지 않는다.
  *
  *  프로토콜 자체는 README §3 참고. 여기서는 MCU 쪽 수신·기록만 담당한다.
  ******************************************************************************
  */
#ifndef OTA_H
#define OTA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 전송 계층 추상화 — UART(uart_link)와 TCP(net_link)를 같은 프로토콜로 다루기 위한 vtable.
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

/**
  * @brief  다운로드 모드 — 헤더/청크를 받아 대상 슬롯에 기록한다.
  * @param  t      전송 계층(uartTransport / tcpTransport)
  * @param  target 기록 대상 슬롯
  * @note   STAGING 경로는 성공 시 재부팅하므로 **돌아오지 않는다.**
  *         실패 시에는 Wdg_Panic() 후 호출자에게 돌아오지 않을 수도 있다.
  *         FACTORY 경로만 정상적으로 복귀한다.
  * @note   호출자가 OTA 세션 뮤텍스를 잡은 상태로 부를 것.
  */
void Ota_EnterDownloadMode(const FwTransport *t, FwTarget target);

/**
  * @brief  강제 롤백 요청 — Factory→App 복원을 예약하고 재부팅한다.
  * @note   Factory 슬롯이 비었거나 손상됐으면 "FAILED"를 보내고 그냥 복귀한다.
  *         성공 시 재부팅하므로 돌아오지 않는다.
  */
void Ota_RequestRollback(const FwTransport *t);

#ifdef __cplusplus
}
#endif

#endif /* OTA_H */
