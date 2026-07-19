/**
  ******************************************************************************
  * @file    flash_if.c
  * @brief   Flash 인터페이스 (erase / write) — FW_BOOT / FW_APP 공용
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

uint32_t FlashIf_NextSectorAddr(uint32_t addr)
{
  /* 섹터 경계표 (FlashIf_GetSector 의 경계와 동일하게 유지할 것).
   * 각 값은 '그 섹터의 끝 다음 주소' = 다음 섹터의 시작이다. */
  static const uint32_t bounds[] = {
    /* Bank1 */
    0x08004000U, 0x08008000U, 0x0800C000U, 0x08010000U,   /* S0~S3  16K */
    0x08020000U,                                          /* S4     64K */
    0x08040000U, 0x08060000U, 0x08080000U, 0x080A0000U,
    0x080C0000U, 0x080E0000U, 0x08100000U,                /* S5~S11 128K */
    /* Bank2 */
    0x08104000U, 0x08108000U, 0x0810C000U, 0x08110000U,   /* S12~S15 16K */
    0x08120000U,                                          /* S16     64K */
    0x08140000U, 0x08160000U, 0x08180000U, 0x081A0000U,
    0x081C0000U, 0x081E0000U, 0x08200000U                 /* S17~S23 128K */
  };

  for (uint32_t i = 0U; i < (sizeof(bounds) / sizeof(bounds[0])); i++)
  {
    if (addr < bounds[i])
    {
      return bounds[i];
    }
  }
  return 0x08200000U;   /* 플래시 끝 */
}

/* Flash 상태 레지스터의 에러 플래그 모음 */
#define FLASH_IF_SR_ERRORS  (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | \
                             FLASH_SR_PGSERR | FLASH_SR_OPERR)
#define FLASH_IF_SR_ALL     (FLASH_IF_SR_ERRORS | FLASH_SR_EOP)

/**
  * @brief  단일 섹터 소거 — 이 함수는 **RAM에서 실행된다**.
  *
  * @note   ★ 왜 RAM이어야 하는가.
  *         섹터 소거 중에는 해당 뱅크의 Flash 배열을 읽을 수 없다. 소거 대상이 Bank1이면
  *         앱 코드 자체가 Bank1에 있으므로(App 슬롯 = 섹터 5~8) 명령어 인출이 멈춰
  *         **CPU가 완전히 정지**한다. 그동안에는 어떤 태스크도, 어떤 ISR도 돌지 못한다.
  *
  *         그런데 IWDG는 LSI로 독립 구동되므로 CPU가 멈춰 있든 말든 계속 카운트한다.
  *         설정값(Prescaler=256, Reload=500)의 타임아웃은 128000/f_LSI 인데, LSI 규격이
  *         17/32/47 kHz(min/typ/max)라 **보장 가능한 하한은 2.72초**(f_LSI가 상한일 때)다.
  *         반면 128KB 섹터 하나의 소거 시간은 typ 1초 / **max 4초**다. 즉 소거를 시작하기
  *         직전에 워치독을 갱신해도 마진이 음수이고, 소거 중에는 갱신할 방법이 없다.
  *         → LSI가 빠른 쪽으로 치우친 개체나 마모된 섹터에서 **소거 도중 자기 리셋**이
  *           나고, Factory 슬롯이 반쯤 지워진 채로 재부팅된다.
  *
  *         이 함수를 RAM에 두면 소거 중에도 코드가 계속 돌므로, BSY 폴링 루프 안에서
  *         IWDG를 계속 갱신할 수 있다. 소거 시간이 얼마가 걸리든 워치독은 물지 않는다.
  *
  * @note   HAL을 쓰지 않고 레지스터를 직접 다루는 이유도 같다. HAL 함수는 Flash에 있어
  *         호출하는 순간 인출이 멈춘다. 여기서 부르는 것은 전부 CMSIS 인라인 함수뿐이다.
  *
  * @note   Bank1을 지울 때만 인터럽트를 막는다. 인터럽트가 걸리면 벡터 테이블을 Flash에서
  *         읽어야 하는데 그 인출이 소거 완료까지 멈추므로, 그동안 이 폴링 루프가 돌지 못해
  *         RAM에 둔 의미가 없어진다. (어차피 CPU가 정지하는 구간이라 새로 잃는 것은 없다.)
  *         Bank2(Staging) 소거 중에는 Bank1 코드가 정상 실행되므로 인터럽트를 살려 둔다 —
  *         여기서까지 막으면 Ethernet/lwIP가 수 초간 멎어 오히려 퇴보다.
  *
  * @param  snb    FLASH_CR.SNB에 넣을 값 (섹터 12~23은 +4 보정된 값)
  * @param  bank1  대상이 Bank1(섹터 0~11)이면 1
  */
__attribute__((section(".RamFunc"), noinline))
static FlashIf_Status FlashIf_EraseSectorFromRam(uint32_t snb, uint32_t bank1)
{
  FlashIf_Status result  = FLASH_IF_OK;
  uint32_t       primask = __get_PRIMASK();

  if (bank1 != 0U)
  {
    __disable_irq();
  }

  /* 앞선 연산이 남아 있으면 끝날 때까지 (여기서도 워치독을 갱신한다) */
  while ((FLASH->SR & FLASH_SR_BSY) != 0U)
  {
    IWDG->KR = 0x0000AAAAU;
  }

  if ((FLASH->CR & FLASH_CR_LOCK) != 0U)
  {
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
  }

  /* 잔류 에러 플래그를 지우고 시작한다. 이전 실패가 남긴 플래그를 그대로 두면
   * 이번 연산이 멀쩡히 끝났는데도 실패로 판정된다. */
  FLASH->SR = FLASH_IF_SR_ALL;

  FLASH->CR &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
  FLASH->CR |= FLASH_PSIZE_WORD;                              /* 2.7~3.6V */
  FLASH->CR |= FLASH_CR_SER | (snb << FLASH_CR_SNB_Pos);
  FLASH->CR |= FLASH_CR_STRT;
  __DSB();                                                    /* STRT가 실제로 전달된 뒤 폴링 */

  /* ★ 핵심: 소거가 끝날 때까지 폴링하면서 매 반복 워치독을 갱신한다.
   * 이 루프가 RAM에 있으므로 Bank1 소거로 Flash가 멈춰 있어도 계속 돈다. */
  while ((FLASH->SR & FLASH_SR_BSY) != 0U)
  {
    IWDG->KR = 0x0000AAAAU;
  }

  if ((FLASH->SR & FLASH_IF_SR_ERRORS) != 0U)
  {
    result = FLASH_IF_ERROR_ERASE;
  }
  FLASH->SR = FLASH_IF_SR_ALL;                                /* 다음 연산을 위해 정리 */

  FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
  FLASH->CR |= FLASH_CR_LOCK;

  if ((bank1 != 0U) && (primask == 0U))
  {
    __enable_irq();
  }

  return result;
}

FlashIf_Status FlashIf_EraseRange(uint32_t startAddr, uint32_t endAddr)
{
  uint32_t first = FlashIf_GetSector(startAddr);
  uint32_t last  = FlashIf_GetSector(endAddr);

  /* endAddr < startAddr이면 아래 루프가 성립하지 않는다. 그대로 두면 섹터 번호가
   * 끝없이 증가하며 엉뚱한 섹터를 지운다. */
  if (last < first)
  {
    return FLASH_IF_ERROR_ERASE;
  }

  for (uint32_t sector = first; sector <= last; sector++)
  {
    /* 섹터 12~23은 SNB 값이 4만큼 밀려 있다(F429 dual bank). HAL의 FLASH_Erase_Sector와
     * 같은 보정이다. */
    uint32_t snb   = (sector > FLASH_SECTOR_11) ? (sector + 4U) : sector;
    uint32_t bank1 = (sector <= FLASH_SECTOR_11) ? 1U : 0U;

    FlashIf_Status st = FlashIf_EraseSectorFromRam(snb, bank1);
    if (st != FLASH_IF_OK)
    {
      return st;
    }
  }

  return FLASH_IF_OK;
}

FlashIf_Status FlashIf_EraseApp(void)
{
  /* 실행 영역만 지운다. Staging/Metadata는 건드리지 않음. */
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

uint32_t FlashIf_Crc32(const uint8_t *data, uint32_t length)
{
  /* STM32 하드웨어 CRC 유닛:
   *   DR에 32bit word를 쓸 때마다  crc ^= word; 이후 32회 { crc = (crc<<1) ^ (MSB? poly:0) }
   *   CR의 RESET 비트로 init=0xFFFFFFFF */
  __HAL_RCC_CRC_CLK_ENABLE();
  CRC->CR = CRC_CR_RESET;

  for (uint32_t i = 0U; (i + 4U) <= length; i += 4U)
  {
    uint32_t word;
    memcpy(&word, &data[i], 4U);   /* 비정렬 접근 회피 (little-endian으로 word 구성) */
    CRC->DR = word;
  }

  return CRC->DR;
}
