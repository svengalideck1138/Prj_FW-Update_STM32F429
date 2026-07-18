/**
  ******************************************************************************
  * @file    wdg_monitor.h
  * @brief   태스크 체크인 기반 워치독 감시.
  *
  *  왜 필요한가:
  *   베어메탈 시절에는 단일 메인 루프가 IWDG를 pet했기 때문에 "루프가 멈춘다 = pet이
  *   끊긴다 = 리셋 = Factory 롤백"이 자동으로 성립했다. 이것이 이 프로젝트 자동 롤백의
  *   전제다. RTOS에서 pet 전용 태스크를 순진하게 두면, 통신/OTA 태스크가 죽어도 그 태스크는
  *   멀쩡히 계속 pet 하므로 "멈췄는데 리셋이 안 되는" 상태가 되어 안전장치가 무력화된다.
  *
  *  해결:
  *   각 태스크가 자기 슬롯에 주기적으로 체크인하고, 감시 태스크는 **등록된 모든 슬롯이
  *   기한 내에 체크인했을 때만** IWDG를 pet한다. 하나라도 늦으면 panic을 '래치'하고
  *   영영 pet하지 않는다 → IWDG 리셋 → 부트로더가 Factory로 롤백.
  *
  *  참고: FW_APP은 main()의 MX_IWDG_Init()에서 스스로 IWDG(~4초)를 켠다(부트로더의 TRIAL
  *  무장과는 별개). 따라서 이 감시는 평상시 부팅에서도 상시 유효하다.
  ******************************************************************************
  */
#ifndef WDG_MONITOR_H
#define WDG_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t WdgId;
#define WDG_INVALID_ID   ((WdgId)-1)

/**
  * @brief  감시 슬롯 등록. 스케줄러 시작 전(또는 해당 태스크 첫 체크인 전)에 호출한다.
  * @param  name        진단 출력에 쓰일 이름(정적 문자열이어야 함 — 복사하지 않는다)
  * @param  deadlineMs  이 시간 안에 체크인이 없으면 panic
  * @retval 슬롯 ID. 슬롯이 없으면 WDG_INVALID_ID
  */
WdgId Wdg_Register(const char *name, uint32_t deadlineMs);

/** @brief 체크인("나 살아있다"). 핫패스 — 32비트 단일 저장이라 락이 필요 없다. */
void  Wdg_CheckIn(WdgId id);

/** @brief 정당한 장시간 블록 동안 감시에서 일시 제외 / 복귀. */
void  Wdg_Suspend(WdgId id);
void  Wdg_Resume(WdgId id);

/**
  * @brief  치명적 장애 선언. 이후 감시 태스크는 **영구히** pet을 중단한다(래칭).
  * @note   실패 경로(다운로드 실패, 스택 오버플로, 힙 부족, Error_Handler)에서 호출.
  *         IWDG가 물어 리셋 → 부트로더가 Factory 롤백. LD3(빨강) 점등으로 표시.
  */
void  Wdg_Panic(const char *reason);

/** @brief panic 래치 여부. */
bool  Wdg_IsPanicked(void);

/** @brief 감시 태스크 본체(osThreadNew에 넘긴다). 100ms 주기로 판정 후 pet. */
void  Wdg_MonitorTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* WDG_MONITOR_H */
