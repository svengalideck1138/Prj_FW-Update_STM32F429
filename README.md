# STM32F429 무선 펌웨어 업데이트 시스템 (OTA / IAP)

NUCLEO-F429ZI 보드를 대상으로, **ST-Link 없이** 디버깅용 **UART**(향후 Ethernet) 케이블과 **C# UI(UI_Monitor)** 만으로 펌웨어를 교체하는 프로젝트입니다. STM32 내부에 커스텀 부트로더를 두고, **애플리케이션이 스스로 새 펌웨어를 내려받아 재부팅하면 부트로더가 적용**하는 방식(OTA)을 구현합니다.

> **목표 시나리오**: 제품 출하 후 현장에서 ST-Link 없이, 케이블만 연결해 C# UI로 `.hex` 펌웨어를 무선 업데이트한다.

> **현재 상태**: 핵심 파이프라인(UART) **동작 완료** — UI에서 `.hex` 전송 → 앱이 Staging에 저장 → 재부팅 → 부트로더가 실행영역에 복사 → 새 펌웨어 실행.

---

## 1. 아키텍처: "앱이 다운로드, 부트로더가 적용"

일반적인 "부트로더가 다운로드" 방식과 달리, **다운로드는 앱이** 수행하고 **부트로더는 적용(복사)만** 하는 구조입니다.

```
┌─ APP (평소 실행) ─────────────────────────────────────────┐
│  · UART로 "FWUPDATE" 명령 수신 → 다운로드 모드 진입        │
│  · 새 펌웨어를 Flash의 [Staging] 영역에 수신·저장          │
│  · 다 받으면 [Metadata]에 '업데이트 대기' 플래그 기록      │
│  · NVIC_SystemReset() 으로 재부팅                          │
└──────────────────────────┬────────────────────────────────┘
                           │ 재부팅
                           ▼
┌─ Bootloader (부팅 시) ────────────────────────────────────┐
│  · [Metadata] 플래그 확인                                  │
│  · 있으면 [Staging] → [App 실행영역] 복사(적용) → 플래그 클리어 │
│  · 앱으로 점프                                             │
└───────────────────────────────────────────────────────────┘
```

**장점**: 다운로드 중에도 앱이 정상 동작 / 실행 중 코드를 덮어쓰지 않음(안전) / 부트로더가 단순.

구성요소:
- **FW_BOOT**: 부트로더. 업데이트 적용(복사) + 앱 점프.
- **FW_APP**: 애플리케이션. 평소 동작 + UART로 펌웨어 수신 → Staging 저장 → 재부팅.
- **UI_Monitor**: C# WinForms. `.hex` 파싱 + 프로토콜 전송 + 진행률.

---

## 2. 메모리 맵 (STM32F429ZI Flash 2MB)

`FW_BOOT/Core/Inc/flash_if.h` 에 단일 정의(양쪽 프로젝트 공용).

```
┌──────────────────────────────────────────────────────────┐
│ Bootloader    0x0800_0000 ~ 0x0800_FFFF   64KB  (섹터 0~3)│  복사+점프
├──────────────────────────────────────────────────────────┤
│ Metadata      0x0801_0000 ~ 0x0801_FFFF   64KB  (섹터 4)  │  업데이트 플래그/크기
├──────────────────────────────────────────────────────────┤
│ App 실행영역   0x0802_0000 ~ 0x080F_FFFF  896KB (섹터 5~11)│  현재 펌웨어 실행
├──────────────────────────────────────────────────────────┤
│ Staging       0x0810_0000 ~ 0x081D_FFFF  896KB (섹터12~22)│  새 펌웨어 임시 저장
├──────────────────────────────────────────────────────────┤
│ 예비           0x081E_0000 ~ 0x081F_FFFF  128KB (섹터 23) │
└──────────────────────────────────────────────────────────┘
```

- **Staging = 실행영역과 동일 크기(896KB)** → 어떤 앱이든 그대로 수용
- **XIP**: STM32는 코드를 Flash에서 직접 실행. RAM엔 변수/스택 + 수신 버퍼만.

![Memory Map](docs/Memory%20Map.png)

---

## 3. 전송 프로토콜 (UART, 115200 8N1)

| 순서 | 방향 | 내용 |
|---|---|---|
| 1 | PC→MCU | `"FWUPDATE"` (다운로드 모드 진입 명령) |
| 2 | MCU→PC | `"READY\r\n"` |
| 3 | PC→MCU | 전체 크기 4바이트 (little-endian) |
| 4 | MCU→PC | Staging erase 후 `ACK(0x79)` / `NACK(0x1F)` |
| 5 | PC→MCU | 256B 청크 |
| 6 | MCU→PC | 청크마다 `ACK(0x79)` (흐름 제어) |
| 7 | (5~6 반복) | |
| 8 | MCU→PC | `"DONE\r\n"` → 이후 앱이 플래그 기록 + 재부팅 |

- `.hex` 파싱은 **C#(PC)에서** 수행 → MCU엔 `크기 + 연속 바이너리`만 전송(MCU 단순화)
- 빈 공간 0xFF 패딩, 4바이트 정렬

---

## 4. 프로젝트 구조

```
Prj_FW-Update_STM32F429/
├── FW_BOOT/                  # 부트로더 (@ 0x0800_0000)
│   ├── Core/Src/main.c       #   BL_ApplyPendingUpdate / BL_JumpToApplication
│   ├── Core/Src/flash_if.c   #   Flash erase/write/meta
│   ├── Core/Inc/flash_if.h   #   메모리 맵 + FwMeta 정의 (공용 원본)
│   └── STM32F429ZITX_FLASH.ld#   FLASH 64K
├── FW_APP/                   # 애플리케이션 (@ 0x0802_0000)
│   ├── Core/Src/main.c       #   App_EnterDownloadMode (Staging 수신)
│   ├── Core/Src/flash_if.c   #   FW_BOOT와 동일하게 유지
│   ├── Core/Inc/flash_if.h   #
│   └── STM32F429ZITX_FLASH.ld#   FLASH ORIGIN=0x8020000, 896K
├── UI_Monitor/               # C# UI (.hex 파서 + 전송)
├── docs/                     # 데이터시트, 메모리맵 이미지
└── README.md
```

> **flash_if.c/.h 는 두 프로젝트에 복제**되어 있다. 한쪽 수정 시 다른 쪽도 동일하게 유지할 것.

---

## 5. 핵심 소스

### FW_APP — 벡터 재배치 + 다운로드 수신 (`main.c`)
```c
SCB->VTOR = 0x08020000U;                 // 실행영역 벡터테이블 (★필수)
```
```c
// 평소: 초록 하트비트 + "FWUPDATE" 감시 → 수신 시 App_EnterDownloadMode()
// 다운로드: [크기 수신] → Staging erase → [청크 수신·기록 + ACK] → "DONE"
//           → FlashIf_WriteMeta({magic, size}) → NVIC_SystemReset()
```

### FW_BOOT — 업데이트 적용 (`main.c`)
```c
static void BL_ApplyPendingUpdate(void)
{
  const FwMeta *m = (const FwMeta *)METADATA_ADDRESS;
  if (m->magic != FW_UPDATE_MAGIC) return;              // 대기 없음 → 정상 부팅
  FlashIf_EraseRange(APP_ADDRESS, APP_ADDRESS + m->size - 1);   // 실행영역 지우기
  FlashIf_Write(APP_ADDRESS, (const uint8_t*)STAGING_ADDRESS,   // Staging → 실행영역
                (m->size + 3) & ~3U);
  FlashIf_ClearMeta();                                  // 플래그 클리어(1회 적용)
}
```

### FW_BOOT — 앱 점프 (`main.c`)
```c
// 유효성 검사(SP가 RAM 범위) → HAL_DeInit/SysTick off → SCB->VTOR
// → __set_MSP → __enable_irq(★) → 앱 Reset_Handler로 점프
```

### UI_Monitor — Intel HEX 파서 + 전송 (`Form1.cs`)
```
ParseIntelHex(path, 0x08020000) → 연속 바이너리
SendFirmwareSequence(): FWUPDATE → READY → 크기 → ACK → 청크(ACK) → DONE
```

---

## 6. 빌드 & 사용법

### 최초 1회 (ST-Link)
1. **FW_BOOT** 빌드 → 플래시 (`0x0800_0000`)
2. **FW_APP** 빌드 → 플래시 (`0x0802_0000`) — 이후 이 앱이 OTA를 수행
   - CubeIDE에서 `.hex` 출력: Properties → C/C++ Build → Settings → **MCU Post build outputs → Convert to Intel Hex file**

### 이후 무선 업데이트 (ST-Link 불필요)
1. 새 FW_APP을 빌드해 `FW_APP.hex` 생성 (**플래시하지 않음**)
2. 보드는 현재 앱 실행 중(하트비트)
3. UI_Monitor: COM 포트 **115200** 로 OPEN → **Send Firmware (.hex)** → `FW_APP.hex` 선택
4. 진행률 100% → `DONE` → 보드 자동 재부팅 → 부트로더가 적용 → **새 펌웨어 실행**

> ⚠️ 현재 실행 중인 앱에 **다운로드 수신 코드가 있어야** OTA가 된다(그래서 최초 1회는 ST-Link).

### LED 상태
| LED | 의미 |
|---|---|
| 🔴 적색 3회 빠른 깜빡 | 부트로더 실행(점프 직전) |
| 🔴 적색 계속 깜빡 | 유효 앱 없음(부트로더 모드) |
| 🟢/🔵 하트비트 | 앱 정상 실행 (패턴은 앱 버전에 따름) |
| 🔵 파랑 켜짐 | 다운로드 모드 / 업데이트 적용 중 |

---

## 7. 로드맵

```
✅ 1. 부트로더/앱 분리 + 점프 (VTOR / MSP / IRQ 재활성화)
✅ 2. Flash erase/write 드라이버 (flash_if)
✅ 3. 메모리 맵 재설계 (Bootloader / Metadata / App / Staging)
✅ 4. 앱: UART "FWUPDATE" 인지 → Staging에 hex 수신
✅ 5. 앱: 수신 완료 → Metadata 플래그 + 재부팅
✅ 6. 부트로더: 플래그 확인 → Staging→실행영역 복사(적용)
─────────── 여기까지 UART OTA 파이프라인 완성 ───────────
⬜ 7. CRC 검증 + 자동 롤백 (전송 손상/전원차단 대비)
⬜ 8. UI 다듬기 (에러 처리, 버전 표시)
⬜ 9. RTOS(FreeRTOS)로 통신 태스크화 + Ethernet(lwIP) 방식 추가
```

### 다음(7단계) 롤백 구상
- **CRC 검증**: 전송/복사 전후 무결성 확인 (Metadata의 `crc` 필드 활용)
- **워치독 + trial/confirm**: 새 펌웨어가 부팅 후 스스로 "정상" 확인을 못 하면 리셋 → 이전(또는 Factory)로 롤백

---

## 8. 트러블슈팅 (겪고 해결한 것)

| 증상 | 원인 | 해결 |
|---|---|---|
| 점프 후 멈춤/크래시 | 앱이 부트로더 벡터테이블을 봄 | 앱에서 `SCB->VTOR = 0x08020000` |
| LED 켜진 채 멈춤 (HAL_Delay 무한대기) | 부트로더가 `__disable_irq()` 후 안 켜줌 | 점프 직전 `__enable_irq()` |
| 앱 초기화 중 멈춤 | 앱이 ETH 재초기화 실패 | 앱에서 불필요한 주변장치 초기화 제거 |
| 커밋 시 `.vsidx` Permission denied | VS 캐시가 git 추적됨 | `.gitignore` + `git rm --cached` |

---

## 9. 참고 자료 (docs)

- `docs/Memory Map.png` — STM32F429 메모리 맵
- `docs/NUCLEO-F429ZI Datasheet.pdf` — 보드 데이터시트
- `docs/STM32F427ZI Datasheet.pdf` — MCU 데이터시트 (F427/F429 공용)

## 개발 환경
- **보드**: NUCLEO-F429ZI (STM32F429ZI, Flash 2MB / SRAM 256KB[192KB+64KB CCM])
- **IDE**: STM32CubeIDE / **UI**: C# WinForms (.NET Framework, System.IO.Ports)
