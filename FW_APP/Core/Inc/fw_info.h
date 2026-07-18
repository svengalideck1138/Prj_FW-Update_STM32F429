/**
  ******************************************************************************
  * @file    fw_info.h
  * @brief   펌웨어 빌드 변형(버전 + LED 패턴) 정의와, 다른 슬롯의 버전을 읽어오는 기능.
  *
  *  왜 필요한가:
  *   롤백이 '실제로' 일어났는지 확인하려면 지금 돌고 있는 이미지가 무엇인지 구분되어야 한다.
  *   그래서 (1) 빌드마다 LED 패턴을 다르게 하고, (2) 버전 문자열을 UART로 주기 출력하며,
  *   (3) Factory 슬롯에 들어있는 이미지의 버전까지 함께 보여준다.
  *
  *  Factory 버전을 어떻게 아는가:
  *   Factory(0x080A_0000)에는 펌웨어 이미지가 통째로 들어있다. 그 안에 '매직 8바이트 +
  *   버전 문자열' 구조체를 심어두고, 실행 중인 앱이 Factory 영역을 4바이트 간격으로
  *   훑어 그 매직을 찾는다. 링커 스크립트를 수정하지 않아도 되고(고정 오프셋 불필요),
  *   빌드 배치가 바뀌어도 그대로 동작한다.
  ******************************************************************************
  */
#ifndef FW_INFO_H
#define FW_INFO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 빌드 변형 선택 =====================================================
 *  ★ 빌드할 때 아래 FW_VARIANT 한 줄만 바꾼 뒤 .hex를 따로 저장하면 된다.
 *
 *    FW_VARIANT_APP01     : 파란색(LD2) 점멸        → FW_App01.hex
 *    FW_VARIANT_APP02     : 초록색(LD1) 점멸        → FW_App02.hex
 *    FW_VARIANT_FACTORY   : LED 3개 좌우 순차 이동  → FW_FACTORY.hex
 *    FW_VARIANT_APP02_BAD : 자가확인 직전에 고의로 멈춤 → FW_App02_BAD.hex
 *                           (롤백 시험 전용. 아래 설명 참고)
 * ========================================================================= */
#define FW_VARIANT_APP01      1
#define FW_VARIANT_APP02      2
#define FW_VARIANT_FACTORY    3
#define FW_VARIANT_APP02_BAD  4

#define FW_VARIANT           FW_VARIANT_APP02     /* ← 여기만 바꾼다 */

/* 변형에 따라 버전 문자열이 자동으로 정해진다(오타/불일치 방지) */
#if   (FW_VARIANT == FW_VARIANT_APP01)
  #define FW_VERSION_STRING  "FW_App01"
#elif (FW_VARIANT == FW_VARIANT_APP02)
  #define FW_VERSION_STRING  "FW_App02"
#elif (FW_VARIANT == FW_VARIANT_FACTORY)
  #define FW_VERSION_STRING  "FW_FACTORY"
#elif (FW_VARIANT == FW_VARIANT_APP02_BAD)
  #define FW_VERSION_STRING  "FW_App02_BAD"
#else
  #error "FW_VARIANT 값이 올바르지 않습니다"
#endif

/* ===== 롤백 시험용 '고장난 앱' =====================================================
 *  FW_VARIANT_APP02_BAD 로 빌드하면, 앱이 TRIAL 부팅에서 '자가확인(CONFIRMED 기록)'을
 *  하기 직전에 일부러 무한 루프에 빠진다. 그러면 다음 사슬이 그대로 돌아간다:
 *
 *    OTA 업로드 → DONE → 재부팅
 *      → 부트로더: PENDING 감지 → Staging→App 복사 → state=TRIAL, 워치독(약 4초) 무장 → 점프
 *      → 앱이 멈춤 → CONFIRMED 기록 실패 → 아무도 워치독을 갱신하지 않음
 *      → 약 4초 뒤 워치독 리셋
 *      → 부트로더: TRIAL + 시도횟수 초과 감지 → Factory→App 복사(롤백) → 메타 클리어
 *      → Factory 이미지로 부팅 (LED 패턴이 바뀌는 것으로 복원 확인)
 *
 *  ⚠️ 안전하다: 고장난 이미지는 App 슬롯에만 들어가고 Factory는 건드리지 않으므로,
 *     롤백이 정상 펌웨어로 자동 복구한다. 벽돌이 되지 않는다.
 * ================================================================================= */

/* ===== 이미지에 심는 표식 ===== */

/* 8바이트 매직. 우연히 같은 값이 데이터에 나올 확률을 없애기 위해 두 워드를 쓴다. */
#define FW_INFO_MAGIC0   0x4E495746U   /* 'F','W','I','N' */
#define FW_INFO_MAGIC1   0xA5A5F00DU

#define FW_INFO_VER_LEN  24U

typedef struct {
  uint32_t magic0;
  uint32_t magic1;
  char     version[FW_INFO_VER_LEN];   /* 널 종료 문자열 */
} FwInfo;

/** 이 이미지 자신의 정보(상수). 링커가 .rodata에 넣고, 다른 슬롯에서는 스캔으로 찾는다. */
extern const FwInfo g_fwInfo;

/**
  * @brief  주어진 플래시 영역에서 FwInfo 표식을 찾는다.
  * @param  base  슬롯 시작 주소 (예: FACTORY_ADDRESS)
  * @param  size  슬롯 크기      (예: FACTORY_SIZE)
  * @retval 찾으면 해당 구조체 포인터, 없으면 NULL.
  */
const FwInfo *FwInfo_FindIn(uint32_t base, uint32_t size);

/**
  * @brief  슬롯의 버전 문자열을 반환한다. 표식이 없으면 "(none)".
  * @note   반환 문자열은 플래시 안의 상수이거나 정적 리터럴이므로 그대로 출력해도 된다.
  */
const char *FwInfo_VersionOf(uint32_t base, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* FW_INFO_H */
