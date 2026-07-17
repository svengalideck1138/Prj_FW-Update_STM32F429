# STM32F429 무선 펌웨어 업데이트 시스템 (OTA / IAP)

NUCLEO-F429ZI 보드를 대상으로, **ST-Link 없이** 디버깅용 **UART**(향후 Ethernet) 케이블과 **C# UI(UI_Monitor)** 만으로 펌웨어를 교체하는 프로젝트입니다. STM32 내부에 커스텀 부트로더를 두고, **애플리케이션이 스스로 새 펌웨어를 내려받아 재부팅하면 부트로더가 검증·적용**하는 방식(OTA)을 구현합니다.

> **목표 시나리오**: 제품 출하 후 현장에서 ST-Link 없이, 케이블만 연결해 C# UI로 `.hex` 펌웨어를 무선 업데이트한다.

> **현재 상태**: **UART OTA 파이프라인 + 무결성 검증(CRC) + 자동 롤백까지 완성·실기 검증 완료.** 전송이 깨지거나 새 펌웨어가 고장 나도 안전하게 복구된다. (Ethernet 경로는 예정)

---

## 1. 아키텍처: "앱이 다운로드, 부트로더가 검증·적용"

일반적인 "부트로더가 다운로드" 방식과 달리, **다운로드는 앱이** 수행하고 **부트로더는 검증·적용(복사)·롤백만** 하는 구조입니다.

```
┌─ APP (평소 실행) ─────────────────────────────────────────┐
│  · UART로 "FWUPDATE" 명령 수신 → 다운로드 모드 진입        │
│  · 새 펌웨어를 Flash의 [Staging]에 수신·저장 (청크 CRC 검증)│
│  · 전체 CRC32 검증 통과 → [Metadata] state=PENDING 기록    │
│  · NVIC_SystemReset() 으로 재부팅                          │
│  · (시험 부팅이면) 자가진단 통과 → state=CONFIRMED + 워치독 갱신 │
└──────────────────────────┬────────────────────────────────┘
                           │ 재부팅
                           ▼
┌─ Bootloader (부팅 시) ────────────────────────────────────┐
│  · 최초 부팅: App → Factory 복사(골든 이미지 확보)         │
│  · [Metadata] 상태머신 처리:                              │
│     PENDING  → CRC 검증 → Staging→App 복사 → TRIAL(워치독 켜고 시험) │
│     TRIAL    → (확인 실패로 재리셋됨) → Factory→App 롤백   │
│     CONFIRMED→ 메타 클리어(정상 확정)                      │
│  · 앱으로 점프                                             │
└───────────────────────────────────────────────────────────┘
```

**장점**: 다운로드 중에도 앱이 정상 동작 / 실행 중 코드를 덮어쓰지 않음 / 깨진 전송·고장 펌웨어·전원차단에 안전.

구성요소:
- **FW_BOOT**: 부트로더. 검증·적용·롤백·워치독·앱 점프.
- **FW_APP**: 애플리케이션. 평소 동작 + UART로 펌웨어 수신 → Staging 저장 → 재부팅 + 자가확인.
- **UI_Monitor**: C# WinForms. `.hex` 파싱 + 프로토콜 전송 + 실시간 진행률/속도.

---

## 2. 메모리 맵 (STM32F429ZI Flash 2MB)

`FW_BOOT/Core/Inc/flash_if.h` 에 단일 정의(양쪽 프로젝트 공용, 각 슬롯 512KB 대칭).

```
┌──────────────────────────────────────────────────────────┐
│ Bootloader   0x0800_0000 ~ 0x0800_FFFF   64KB  (섹터 0~3) │  검증/적용/롤백/점프
├──────────────────────────────────────────────────────────┤
│ Metadata     0x0801_0000 ~ 0x0801_FFFF   64KB  (섹터 4)   │  상태/크기/CRC/시도횟수
├──────────────────────────────────────────────────────────┤
│ App 실행영역  0x0802_0000 ~ 0x0809_FFFF  512KB (섹터 5~8) │  현재 펌웨어 실행
├──────────────────────────────────────────────────────────┤
│ Factory      0x080A_0000 ~ 0x0811_FFFF  512KB (섹터 9~16) │  골든 이미지(롤백 복귀처, 불변)
├──────────────────────────────────────────────────────────┤
│ Staging      0x0812_0000 ~ 0x0819_FFFF  512KB (섹터17~20) │  새 펌웨어 임시 저장
├──────────────────────────────────────────────────────────┤
│ 예비          0x081A_0000 ~ 0x081F_FFFF  384KB (섹터21~23) │
└──────────────────────────────────────────────────────────┘
```

- **App / Factory / Staging = 각 512KB 동일** → 어떤 앱이든 세 슬롯에 그대로 수용
- **Factory**: 첫 ST-Link 펌웨어가 최초 부팅 시 자동 복사되어 롤백 복귀처가 됨(이후 불변)
- **XIP**: STM32는 코드를 Flash에서 직접 실행. RAM엔 변수/스택 + 수신 버퍼만.

![Memory Map](docs/Memory%20Map.png)

---

## 3. 전송 프로토콜 (UART, 115200 8N1) — 2겹 무결성 검증

| 순서 | 방향 | 내용 |
|---|---|---|
| 1 | PC→MCU | `"FWUPDATE"` (다운로드 모드 진입 명령) |
| 2 | MCU→PC | `"READY\r\n"` |
| 3 | PC→MCU | **헤더** `[전체크기 4B][CRC32 4B]` (little-endian) |
| 4 | MCU→PC | Staging erase 후 `ACK(0x79)` / `NACK(0x1F)` |
| 5 | PC→MCU | **청크** `[데이터 256B][CRC16 2B]` |
| 6 | MCU→PC | 청크 CRC16 검증 → OK `ACK` / 손상 `NACK`(→ 같은 청크 재전송) |
| 7 | (5~6 반복) | |
| 8 | MCU→PC | 전체 Staging CRC32 검증 → `"DONE\r\n"` / 불일치 시 `"CRCERR\r\n"` |

- **청크 CRC16**(CCITT, poly 0x1021): 깨진 청크 즉시 감지 → **그 청크만 재전송**(자동 복구)
- **전체 CRC32**(STM32 하드웨어 CRC, poly 0x04C11DB7): 조립·Flash 기록 결과 최종 검증
- `.hex` 파싱은 **C#(PC)에서** 수행 → MCU엔 `크기 + 연속 바이너리`만 전송(MCU 단순화). 빈 공간 0xFF 패딩, 4바이트 정렬.

---

## 4. 상태 머신 & 자동 롤백

`Metadata`(섹터4)에 저장되는 `FwMeta.state`로 부트로더와 앱이 재부팅을 건너 상태를 주고받는다.

```
              앱 다운로드+CRC 통과
  NONE ───────────────────────────▶ PENDING
                                      │ 부트로더: Staging CRC OK → App 복사 → App CRC OK
                                      ▼
                                    TRIAL ── 앱 자가진단 통과 ──▶ CONFIRMED ──▶ 정상
           (워치독 4초 켜고 시험 부팅)  │                          (다음 부팅에 메타 클리어)
                                      │ 앱이 멈춤 → 워치독 갱신 실패 → 강제 리셋
                                      ▼
                       부트로더: TRIAL+시도초과 감지 → Factory→App 복사(롤백) → NONE
```

**4겹 방어 체계**

| 위협 | 방어 |
|---|---|
| 전송 중 비트 오류 | 청크 CRC16 → 해당 청크만 재전송 |
| 보관/조립/기록 손상 | 전체 CRC32 (앱 수신 후 + 부트로더 복사 전·후 검증) |
| 복사 중 전원차단 | 검증된 복사 성공 후에만 플래그 클리어 → 재부팅 시 재복사(멱등) |
| **새 펌웨어 기능 고장** | **워치독(IWDG) + Factory 롤백 (자동 복구)** |
| 부트로더 자기 리셋 | 부트로더 긴 Flash 작업 중엔 워치독 미가동 |

> **워치독 원칙**: IWDG는 앱을 감시할 뿐, **부트로더의 긴 Flash 작업(캡처/적용/롤백)은 감시하지 않는다.** 그래서 부트로더는 IWDG를 자동 시작하지 않고 **TRIAL 앱 점프 직전에만** 켠다(`BL_StartWatchdog`). CubeMX가 생성한 `MX_IWDG_Init()` 자동 호출은 FW_BOOT에서 주석 처리되어 있으며, **재생성 시 다시 주석 처리해야 함.**

---

## 5. 프로젝트 구조

```
Prj_FW-Update_STM32F429/
├── FW_BOOT/                  # 부트로더 (@ 0x0800_0000)
│   ├── Core/Src/main.c       #   BL_HandleUpdate(상태머신) / BL_ApplyStagingToApp
│   │                         #   BL_Rollback / BL_StartWatchdog / BL_EnsureFactory / BL_JumpToApplication
│   ├── Core/Src/flash_if.c   #   Flash erase/write/meta + CRC32
│   └── Core/Inc/flash_if.h   #   메모리 맵 + FwMeta/FwState 정의 (공용 원본)
├── FW_APP/                   # 애플리케이션 (@ 0x0802_0000, 512K)
│   ├── Core/Src/main.c       #   App_EnterDownloadMode(수신+CRC) / App_Crc16 / 자가확인+워치독 pet
│   ├── Core/Src/flash_if.c   #   FW_BOOT와 동일하게 유지
│   └── Core/Inc/flash_if.h   #
├── UI_Monitor/               # C# UI (.hex 파서 + 프로토콜 전송 + 진행률)
├── docs/                     # 데이터시트, 메모리맵 이미지
└── README.md
```

> **flash_if.c/.h 는 두 프로젝트에 복제**되어 있다. 한쪽 수정 시 다른 쪽도 동일하게 유지할 것.

---

## 6. 핵심 소스

### FW_APP (`main.c`)
```c
SCB->VTOR = 0x08020000U;                       // 실행영역 벡터테이블 재배치 (★필수)

// 부팅 시: TRIAL이면 자가진단 통과 → state=CONFIRMED 기록 (못 하면 워치독이 리셋→롤백)
// 메인 루프: IWDG->KR = 0xAAAA (워치독 갱신) + "FWUPDATE" 감시 + 하트비트
// 다운로드: [크기+CRC32 수신] → Staging erase → [청크: 데이터+CRC16 → 검증 → 기록 → ACK]
//           → 전체 CRC32 검증 → Metadata{state=PENDING,size,crc} 기록 → NVIC_SystemReset()
```

### FW_BOOT (`main.c`) — 상태 머신
```c
static void BL_HandleUpdate(void) {
  const FwMeta *m = (const FwMeta *)METADATA_ADDRESS;
  if (m->magic != FW_UPDATE_MAGIC) return;
  switch (m->state) {
    case FW_STATE_PENDING:                       // 새 펌웨어 적용
      if (BL_ApplyStagingToApp(m->size, m->crc)){// CRC검증+erase+copy+CRC재검증
        meta.state = FW_STATE_TRIAL; meta.attempts = 1; FlashIf_WriteMeta(&meta);
        BL_StartWatchdog();                      // 4초 워치독 켜고 시험 부팅
      } break;
    case FW_STATE_TRIAL:                          // 시험 실패로 재리셋됨
      if (attempts >= FW_MAX_TRIALS) { BL_Rollback(); FlashIf_ClearMeta(); }  // Factory 복귀
      else { attempts++; FlashIf_WriteMeta(&meta); BL_StartWatchdog(); }      // 재시도
      break;
    case FW_STATE_CONFIRMED: FlashIf_ClearMeta(); break;   // 정상 확정
  }
}
```

### FW_BOOT — 앱 점프
```c
// 유효성 검사(SP가 RAM 범위) → HAL_DeInit/SysTick off → SCB->VTOR
// → __set_MSP → __enable_irq(★) → 앱 Reset_Handler로 점프
```

### UI_Monitor (`Form1.cs`)
```
[open]        ParseIntelHex(path, 0x08020000) → 연속 바이너리, 경로·크기 표시
[F/W Download] SendFirmwareSequence():
              FWUPDATE → READY → [크기+CRC32] → ACK → [청크+CRC16, NACK시 재전송] → DONE
              (상태바에 실시간 %/바이트/속도(KB/s), 뷰어에 10%마다 로그)
```

---

## 7. 빌드 & 사용법

### 최초 1회 (ST-Link)
1. STM32CubeProgrammer로 **Full chip erase** (깨끗한 시작)
2. **FW_BOOT** 빌드 → 플래시 (`0x0800_0000`)
3. **FW_APP** 빌드 → 플래시 (`0x0802_0000`) — 이 앱이 OTA 수행 + 최초 부팅 시 Factory로 자동 복사됨
   - `.hex` 출력: Project Properties → C/C++ Build → Settings → **MCU Post build outputs → Convert to Intel Hex file**

### 이후 무선 업데이트 (ST-Link 불필요)
1. 새 FW_APP을 빌드해 `FW_APP.hex` 생성 (**플래시하지 않음**)
2. 보드는 현재 앱 실행 중(하트비트)
3. UI_Monitor: COM 포트 **115200** OPEN → **[open]** 으로 `.hex` 선택(경로·크기 표시) → **[F/W Download]**
4. 진행률 100% → `DONE` → 자동 재부팅 → 부트로더 검증·적용 → 새 펌웨어 시험 부팅 → 자가확인 → 정상

> ⚠️ 현재 실행 중인 앱에 **다운로드 수신 코드가 있어야** OTA가 된다(그래서 최초 1회는 ST-Link).

### LED 상태
| LED | 의미 |
|---|---|
| 🔴 적색 3회 빠른 깜빡 | 부트로더 실행(점프 직전) |
| 🔴 적색 계속 깜빡 | 유효 앱 없음(부트로더 모드) |
| 🔴 적색 켜짐 | 롤백 중(Factory→App 복사) |
| 🔵 파랑 켜짐 | 다운로드 모드 / 업데이트 적용 중 |
| 🟢+🔵 잠깐 | 최초 부팅 Factory 캡처 중(1회성) |
| 🟢/🔵 하트비트 | 앱 정상 실행 (패턴은 앱 버전에 따름) |

---

## 8. 로드맵

```
✅ 1. 부트로더/앱 분리 + 점프 (VTOR / MSP / IRQ 재활성화)
✅ 2. Flash erase/write 드라이버 (flash_if)
✅ 3. 메모리 맵 재설계 (Bootloader / Metadata / App / Factory / Staging)
✅ 4. 앱: UART "FWUPDATE" 인지 → Staging에 hex 수신
✅ 5. 앱: 수신 완료 → Metadata 플래그 + 재부팅
✅ 6. 부트로더: 플래그 확인 → Staging→실행영역 복사(적용)
✅ 7a. CRC 검증 (전체 CRC32 + 청크 CRC16 자동 재전송)
✅ 7b. Factory 슬롯 + 워치독(IWDG) + trial/confirm 자동 롤백  ← 실기 검증 완료
✅ 8. UI: Open/Download 분리 + 실시간 진행률/속도 표출
─────────── 여기까지 UART OTA + 무결성 + 자동 롤백 완성 ───────────
⬜ 9. Ethernet(lwIP/TCP) 업데이트 경로 + (선택) FreeRTOS 통신 태스크화
```

### 알려진 한계 / 다음 보강 후보
- 앱 다운로드 시 **단일 Staging erase 호출이 워치독 타임아웃(4초)을 넘는 대형 펌웨어(>~200KB)** 는 섹터 단위로 쪼개 지우며 워치독을 갱신하도록 개선 필요(현재 소형 앱은 문제 없음).
- 롤백 목적지는 **Factory(최초 펌웨어)** — "직전 버전"이 아니라 "초기 펌웨어"로 복귀.

---

## 9. 트러블슈팅 (겪고 해결한 것)

| 증상 | 원인 | 해결 |
|---|---|---|
| 점프 후 멈춤/크래시 | 앱이 부트로더 벡터테이블을 봄 | 앱에서 `SCB->VTOR = 0x08020000` |
| LED 켜진 채 멈춤 (HAL_Delay 무한대기) | 부트로더가 `__disable_irq()` 후 안 켜줌 | 점프 직전 `__enable_irq()` |
| 앱 초기화 중 멈춤 | 앱이 ETH 재초기화 실패 | 앱에서 불필요한 주변장치 초기화 제거 |
| **부팅 무한 루프** | CubeMX가 IWDG를 부트로더 시작 시 자동 켜서 긴 Flash 작업 중 자기 리셋 | `MX_IWDG_Init()` 자동 호출 주석 처리, `BL_StartWatchdog()`에서만 시작 |
| 정상 전송인데 `CRCERR` | MCU HW CRC와 PC 소프트 CRC 파라미터 불일치 | 동일 파라미터(poly/init/반사없음/최종XOR없음/LE word) |
| 커밋 시 `.vsidx` Permission denied | VS 캐시가 git 추적됨 | `.gitignore` + `git rm --cached` |

---

## 10. 참고 자료 (docs)

- `docs/Memory Map.png` — STM32F429 메모리 맵
- `docs/NUCLEO-F429ZI Datasheet.pdf` — 보드 데이터시트
- `docs/STM32F427ZI Datasheet.pdf` — MCU 데이터시트 (F427/F429 공용, Flash 프로그래밍 Table 48 등)

## 개발 환경
- **보드**: NUCLEO-F429ZI (STM32F429ZI, Flash 2MB / SRAM 256KB[192KB+64KB CCM])
- **IDE**: STM32CubeIDE / **UI**: C# WinForms (.NET Framework, System.IO.Ports)
