/**
  ******************************************************************************
  * @file    flash_if.h
  * @brief   Flash 인터페이스 (erase / write) — STM32F429ZI (2MB, dual bank)
  * @note    FW_BOOT / FW_APP 공용. 두 프로젝트의 내용은 동일하게 유지할 것.
  ******************************************************************************
  */
#ifndef FLASH_IF_H
#define FLASH_IF_H

#include <stdint.h>

/* ============================================================
 *  메모리 맵 (STM32F429ZI Flash 2MB, dual bank)
 * ------------------------------------------------------------
 *  Bootloader    0x0800_0000  64KB    (섹터 0~3)
 *  Metadata      0x0801_0000  64KB    (섹터 4)     플래그/CRC/크기
 *  App 실행영역   0x0802_0000  896KB   (섹터 5~11)  현재 펌웨어 실행
 *  Staging       0x0810_0000  896KB   (섹터 12~22) 새 펌웨어 임시 저장
 *  예비           0x081E_0000  128KB   (섹터 23)
 * ============================================================ */

#define BOOT_ADDRESS         0x08000000U
#define BOOT_SIZE            (64U * 1024U)

#define METADATA_ADDRESS     0x08010000U
#define METADATA_SIZE        (64U * 1024U)

#define APP_ADDRESS          0x08020000U                          /* 앱 실행영역 시작 (FW_APP 링커 ORIGIN과 일치) */
#define APP_SIZE             (896U * 1024U)
#define APP_END_ADDRESS      (APP_ADDRESS + APP_SIZE - 1U)        /* 0x080F_FFFF */

#define STAGING_ADDRESS      0x08100000U                          /* 새 펌웨어 임시 저장 슬롯 */
#define STAGING_SIZE         (896U * 1024U)
#define STAGING_END_ADDRESS  (STAGING_ADDRESS + STAGING_SIZE - 1U)/* 0x081D_FFFF */

#define FLASH_END_ADDRESS    0x081FFFFFU                          /* Flash 2MB 끝 */

/* === 업데이트 메타데이터 (Metadata 섹터 0x0801_0000에 저장) === */
#define FW_UPDATE_MAGIC      0xB00710ADU   /* 이 값이면 'Staging에 새 펌웨어 대기 중' */

typedef struct
{
  uint32_t magic;      /* FW_UPDATE_MAGIC이면 업데이트 대기 */
  uint32_t size;       /* Staging에 있는 펌웨어 크기(바이트) */
  uint32_t crc;        /* (7단계 예정) 무결성 검증용 */
  uint32_t reserved;
} FwMeta;

/* 반환 상태 */
typedef enum
{
  FLASH_IF_OK = 0,
  FLASH_IF_ERROR_ERASE,   /* 지우기 실패 */
  FLASH_IF_ERROR_WRITE,   /* 쓰기 실패 */
  FLASH_IF_ERROR_ALIGN    /* 길이가 4의 배수가 아님 */
} FlashIf_Status;

/** @brief  startAddr~endAddr 를 포함하는 섹터들을 지운다 (섹터 단위). */
FlashIf_Status FlashIf_EraseRange(uint32_t startAddr, uint32_t endAddr);

/** @brief  애플리케이션 실행 영역(APP_ADDRESS~APP_END_ADDRESS)을 지운다. */
FlashIf_Status FlashIf_EraseApp(void);

/**
  * @brief  address 에 data 를 length 바이트 쓴다.
  * @note   length는 4의 배수여야 하고, 대상 영역은 미리 지워져 있어야 한다.
  */
FlashIf_Status FlashIf_Write(uint32_t address, const uint8_t *data, uint32_t length);

/** @brief  업데이트 메타데이터를 Metadata 섹터에 기록한다 (섹터 지우고 씀). */
FlashIf_Status FlashIf_WriteMeta(const FwMeta *meta);

/** @brief  Metadata 섹터를 지운다 (업데이트 플래그 클리어). */
FlashIf_Status FlashIf_ClearMeta(void);

#endif /* FLASH_IF_H */
