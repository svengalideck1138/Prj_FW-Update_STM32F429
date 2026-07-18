/**
  ******************************************************************************
  * @file    fw_info.c
  * @brief   이미지 버전 표식 정의 + 다른 슬롯에서 표식 찾기. 설계 배경은 fw_info.h 참고.
  ******************************************************************************
  */
#include "fw_info.h"
#include <string.h>

/* 이 이미지 자신의 정보.
 * used 속성: 코드에서 직접 참조하지 않아도 링커가 버리지 않도록 한다
 *            (Factory 슬롯에 들어간 이미지는 '스캔'으로만 읽히기 때문). */
__attribute__((used))
const FwInfo g_fwInfo = {
  .magic0  = FW_INFO_MAGIC0,
  .magic1  = FW_INFO_MAGIC1,
  .version = FW_VERSION_STRING
};

const FwInfo *FwInfo_FindIn(uint32_t base, uint32_t size)
{
  /* 4바이트 정렬로 훑는다. 구조체가 통째로 들어갈 수 있는 범위까지만 본다. */
  if (size < sizeof(FwInfo)) return NULL;

  const uint32_t last = base + size - sizeof(FwInfo);

  for (uint32_t a = base; a <= last; a += 4U)
  {
    const FwInfo *p = (const FwInfo *)a;

    /* 첫 워드부터 비교 → 대부분 여기서 걸러지므로 스캔이 빠르다 */
    if ((p->magic0 == FW_INFO_MAGIC0) && (p->magic1 == FW_INFO_MAGIC1))
    {
      /* 버전 문자열이 '실제로 쓸 수 있는 값'인지까지 확인한다.
       * 매직만 보고 통과시키면, 복사가 도중에 끊긴 슬롯(예: 워치독 리셋으로 중단된
       * Factory 캡처)에서 매직만 옮겨지고 문자열은 0xFF/쓰레기인 상태를 그대로 출력해
       * 터미널에 '????' 같은 값이 찍힌다. 다음 조건을 모두 만족해야 유효로 본다:
       *   · 1글자 이상
       *   · 널 종료가 필드 안에 존재
       *   · 종료 전 문자가 전부 출력 가능한 ASCII(0x20~0x7E) */
      bool ok  = false;
      for (uint32_t i = 0U; i < FW_INFO_VER_LEN; i++)
      {
        char c = p->version[i];

        if (c == '\0')
        {
          ok = (i > 0U);      /* 빈 문자열은 무효 */
          break;
        }
        if ((c < 0x20) || (c > 0x7E))
        {
          break;              /* 출력 불가 문자 → 손상된 것으로 판단 */
        }
      }
      if (ok) return p;
      /* 유효하지 않으면 우연한 일치이거나 손상된 것이므로 계속 찾는다 */
    }
  }
  return NULL;
}

const char *FwInfo_VersionOf(uint32_t base, uint32_t size)
{
  const FwInfo *p = FwInfo_FindIn(base, size);
  return (p != NULL) ? p->version : "(none)";
}
