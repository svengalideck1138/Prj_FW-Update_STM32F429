/**
  ******************************************************************************
  * @file    sys_info.h
  * @brief   보드/장치 식별 정보 + 메모리 사용량 보고 (UI 접속 시 1회 조회).
  *
  *  ST-Link 없이 동작하는 것이 이 프로젝트의 전제이므로, CubeProgrammer가 보여주는
  *  종류의 정보를 **보드가 스스로 읽어서** 알려준다.
  *   · 장치 ID / 리비전 : DBGMCU->IDCODE
  *   · Flash 용량       : 출하 시 각인된 크기 레지스터(0x1FFF7A22)
  *   · UID              : 96비트 고유 ID(0x1FFF7A10)
  *   · 메모리 사용량    : 링커 심볼(_etext/_sdata/_ebss/...)로 계산
  *
  *  ⚠️ 전압(VDDA)은 포함하지 않는다. ADC + VREFINT를 켜야 하는데 그건 CubeMX 설정이라
  *     재생성 함정이 하나 늘어난다(README §9). 필요해지면 별도로 추가할 것.
  ******************************************************************************
  */
#ifndef SYS_INFO_H
#define SYS_INFO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief 한 줄을 내보내는 콜백. 전송 계층에 무관하게 쓰려고 함수 포인터로 받는다.
  * @note  줄바꿈은 구현체가 이미 붙여 넘긴다(호출자가 덧붙이지 말 것).
  */
typedef void (*SysInfo_EmitFn)(void *ctx, const char *line);

/**
  * @brief  보드 정보 + 메모리 사용량을 여러 줄로 보고한다.
  * @param  emit 줄 단위 출력 콜백
  * @param  ctx  콜백에 그대로 전달되는 사용자 데이터
  * @note   큰 버퍼를 쓰지 않는다 — 줄마다 작은 스택 버퍼에 조립해 바로 내보낸다.
  *         netTask 스택 여유가 빠듯하므로(수백 바이트) 의도적으로 이렇게 했다.
  */
void SysInfo_Report(SysInfo_EmitFn emit, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SYS_INFO_H */
