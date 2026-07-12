/**
  ******************************************************************************
  * @file    flash_if.c
  * @brief   부트로더용 Flash 인터페이스 (erase / write)
  ******************************************************************************
  */
#include "flash_if.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/**
  * @brief  Flash 주소 -> 섹터 번호 변환 (STM32F429ZI 2MB, dual bank).
  * @note   F429의 섹터는 크기가 불균일하다:
  *         Bank1: 0~3(16KB) / 4(64KB) / 5~11(128KB)
  *         Bank2: 12~15(16KB) / 16(64KB) / 17~23(128KB)
  */
static uint32_t FlashIf_GetSector(uint32_t addr)
{
  /* --- Bank 1 --- */
  if      (addr < 0x08004000U) return FLASH_SECTOR_0;
  else if (addr < 0x08008000U) return FLASH_SECTOR_1;
  else if (addr < 0x0800C000U) return FLASH_SECTOR_2;
  else if (addr < 0x08010000U) return FLASH_SECTOR_3;
  else if (addr < 0x08020000U) return FLASH_SECTOR_4;
  else if (addr < 0x08040000U) return FLASH_SECTOR_5;
  else if (addr < 0x08060000U) return FLASH_SECTOR_6;
  else if (addr < 0x08080000U) return FLASH_SECTOR_7;
  else if (addr < 0x080A0000U) return FLASH_SECTOR_8;
  else if (addr < 0x080C0000U) return FLASH_SECTOR_9;
  else if (addr < 0x080E0000U) return FLASH_SECTOR_10;
  else if (addr < 0x08100000U) return FLASH_SECTOR_11;
  /* --- Bank 2 --- */
  else if (addr < 0x08104000U) return FLASH_SECTOR_12;
  else if (addr < 0x08108000U) return FLASH_SECTOR_13;
  else if (addr < 0x0810C000U) return FLASH_SECTOR_14;
  else if (addr < 0x08110000U) return FLASH_SECTOR_15;
  else if (addr < 0x08120000U) return FLASH_SECTOR_16;
  else if (addr < 0x08140000U) return FLASH_SECTOR_17;
  else if (addr < 0x08160000U) return FLASH_SECTOR_18;
  else if (addr < 0x08180000U) return FLASH_SECTOR_19;
  else if (addr < 0x081A0000U) return FLASH_SECTOR_20;
  else if (addr < 0x081C0000U) return FLASH_SECTOR_21;
  else if (addr < 0x081E0000U) return FLASH_SECTOR_22;
  else                         return FLASH_SECTOR_23;
}

FlashIf_Status FlashIf_EraseRange(uint32_t startAddr, uint32_t endAddr)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t sectorError = 0U;
  uint32_t first = FlashIf_GetSector(startAddr);
  uint32_t last  = FlashIf_GetSector(endAddr);

  erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
  erase.Sector       = first;
  erase.NbSectors    = (last - first) + 1U;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;   /* 2.7~3.6V: word(32bit) 프로그래밍 */

  HAL_FLASH_Unlock();
  HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&erase, &sectorError);
  HAL_FLASH_Lock();

  return (st == HAL_OK) ? FLASH_IF_OK : FLASH_IF_ERROR_ERASE;
}

FlashIf_Status FlashIf_EraseApp(void)
{
  /* 실행 영역(APP_ADDRESS ~ APP_END_ADDRESS)만 지운다. Staging/Metadata는 건드리지 않음. */
  return FlashIf_EraseRange(APP_ADDRESS, APP_END_ADDRESS);
}

FlashIf_Status FlashIf_Write(uint32_t address, const uint8_t *data, uint32_t length)
{
  /* Flash는 word(4바이트) 단위로만 쓸 수 있다 */
  if ((length & 0x3U) != 0U)
  {
    return FLASH_IF_ERROR_ALIGN;
  }

  HAL_FLASH_Unlock();
  for (uint32_t i = 0U; i < length; i += 4U)
  {
    uint32_t word;
    memcpy(&word, &data[i], 4U);   /* data가 비정렬일 수 있어 memcpy로 안전하게 */

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i, word) != HAL_OK)
    {
      HAL_FLASH_Lock();
      return FLASH_IF_ERROR_WRITE;
    }
  }
  HAL_FLASH_Lock();

  return FLASH_IF_OK;
}

FlashIf_Status FlashIf_WriteMeta(const FwMeta *meta)
{
  /* Metadata 섹터를 지우고 구조체를 기록한다 */
  if (FlashIf_EraseRange(METADATA_ADDRESS, METADATA_ADDRESS) != FLASH_IF_OK)
  {
    return FLASH_IF_ERROR_ERASE;
  }
  return FlashIf_Write(METADATA_ADDRESS, (const uint8_t *)meta, sizeof(FwMeta));
}

FlashIf_Status FlashIf_ClearMeta(void)
{
  return FlashIf_EraseRange(METADATA_ADDRESS, METADATA_ADDRESS);
}
