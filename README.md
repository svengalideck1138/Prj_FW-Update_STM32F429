# STM32F429 펌웨어 업데이트 시스템

<p align="center">
  <img src="docs/NUCLEO-F429ZI%20Top%20View.png" alt="NUCLEO-F429ZI Top View" width="480">
</p>

### 동작 모습

<p align="center">
  <img src="docs/UART%20Download.gif" alt="UART(RS-232)로 펌웨어 다운로드" width="720">
  <br><em><strong>UART(RS-232) 다운로드</strong> — <code>FWUPDATE</code> → Staging 수신 → CRC 검증 → 재부팅 후 적용</em>
</p>

<p align="center">
  <img src="docs/Ethernet%20Download.gif" alt="Ethernet(TCP)으로 펌웨어 다운로드" width="720">
  <br><em><strong>Ethernet(TCP) 다운로드</strong> — 같은 프로토콜을 전송 계층만 바꿔 그대로 사용</em>
</p>

NUCLEO-F429ZI 보드를 대상으로, **ST-Link 없이** **UART 또는 Ethernet** 케이블과 **C# UI(UI_Monitor)** 만으로 펌웨어를 교체하는 프로젝트입니다. STM32 내부에 커스텀 부트로더를 두고, **애플리케이션이 스스로 새 펌웨어를 내려받아 재부팅하면 부트로더가 검증·적용**하는 방식(OTA)을 구현합니다.

> **목표 시나리오**: 제품 출하 후 현장에서 ST-Link 없이, 케이블만 연결해 C# UI로 `.hex` 펌웨어를 업데이트한다.

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
│  · 최초 부팅: App → Factory 복사(공장초기 이미지 확보)         │
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

## 2. 메모리 맵 (STM32F429ZI Flash 2MB)
![Memory Map](docs/Memory%20Map.png)
`FW_BOOT/Core/Inc/flash_if.h` 에 단일 정의(양쪽 프로젝트 공용, 각 슬롯 512KB 대칭).
```
┌──────────────────────────────────────────────────────────┐
│ Bootloader   0x0800_0000 ~ 0x0800_FFFF   64KB  (섹터 0~3) │  검증/적용/롤백/점프
├──────────────────────────────────────────────────────────┤
│ Metadata     0x0801_0000 ~ 0x0801_FFFF   64KB  (섹터 4)   │  상태/크기/CRC/시도횟수
├──────────────────────────────────────────────────────────┤
│ App 실행영역  0x0802_0000 ~ 0x0809_FFFF  512KB (섹터 5~8) │  현재 펌웨어 실행
├──────────────────────────────────────────────────────────┤
│ Factory      0x080A_0000 ~ 0x0811_FFFF  512KB (섹터 9~16) │  공장초기 이미지(롤백 복귀처, 불변)
├──────────────────────────────────────────────────────────┤
│ Staging      0x0812_0000 ~ 0x0819_FFFF  512KB (섹터17~20) │  새 펌웨어 임시 저장
├──────────────────────────────────────────────────────────┤
│ 예비          0x081A_0000 ~ 0x081F_FFFF  384KB (섹터21~23) │
└──────────────────────────────────────────────────────────┘
```

- **App / Factory / Staging = 각 512KB 동일** → 어떤 앱이든 세 슬롯에 그대로 수용
- **Factory**: 첫 ST-Link 펌웨어가 최초 부팅 시 자동 복사되어 롤백 복귀처가 됨(이후 불변)
- **XIP**: STM32는 코드를 Flash에서 직접 실행. RAM엔 변수/스택 + 수신 버퍼만.

> **⚠️ 주소 배치 = 링커 스크립트 + `SCB->VTOR` (둘 다 필수)**
> 각 프로젝트를 지정 슬롯에 놓으려면 **소스 수정만으로는 부족**하고, 링커 스크립트 `STM32F429ZITX_FLASH.ld`의 `FLASH` 영역도 함께 맞춰야 한다.
>
> | 프로젝트 | `.ld` FLASH 영역 | 추가 |
> |---|---|---|
> | **FW_BOOT** | `ORIGIN = 0x08000000, LENGTH = 64K` | — |
> | **FW_APP** | `ORIGIN = 0x08020000, LENGTH = 512K` | 앱 `main()`에서 `SCB->VTOR = 0x08020000` |
>
> - 링커 **`ORIGIN`** = 벡터테이블·코드·상수가 **빌드 시 링크되는 절대주소**, **`SCB->VTOR`** = **런타임에 CPU가 벡터테이블을 찾는 위치** → 둘이 어긋나면 점프 후 크래시.
> - 링커 **`LENGTH`** = 슬롯 크기 제한(App이 Factory/Staging를 침범하지 못하게).
> - (FW_APP `.ld`에 관련 주석 있음. CubeMX 재생성 시 `.ld`의 `ORIGIN/LENGTH`가 기본값으로 되돌아가지 않았는지 확인할 것.)

<p align="center">
  <img src="docs/Linker%20script%20%EC%84%A4%EC%A0%95.png" alt="FW_APP 링커 스크립트 FLASH 영역 설정" width="560">
  <br><em>FW_APP <code>STM32F429ZITX_FLASH.ld</code> — FLASH 영역을 App 슬롯(0x0802_0000 / 512K)으로 직접 설정</em>
</p>

## 3. 전송 프로토콜 (UART / Ethernet)

> 아래 표는 전송 계층(UART/TCP)에 무관하게 동일하다. MCU는 `FwTransport` 추상화로, PC는 `IFwLink` 추상화로 같은 바이트 시퀀스를 주고받는다.

### 명령 목록 (모두 8바이트 ASCII)
| 명령 | 전송 계층 | 동작 |
|---|---|---|
| `FWUPDATE` | UART / TCP | Staging에 기록 → 부트로더가 App에 적용 |
| `FWFACTRY` | UART / TCP | Factory 슬롯에 **직접** 기록(공장초기 이미지 교체) |
| `FWROLLBK` | UART / TCP | Factory→App 복원 요청 (데이터 없음, `DONE`/`FAILED` 응답) |
| `FWINFO??` | **TCP 전용** | 상태 한 줄 조회 (아래 참고) |
| `FWSYS???` | UART / TCP | 보드/장치 정보 + 메모리 사용량 조회 (아래 참고) |

### 다운로드 시퀀스 (`FWUPDATE` / `FWFACTRY` 공통)
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

| 위협 | 방어 |
|---|---|
| 전송 중 비트 오류 | 청크 CRC16 → 해당 청크만 재전송 |
| 보관/조립/기록 손상 | 전체 CRC32 (앱 수신 후 + 부트로더 복사 전·후 검증) |
| 복사 중 전원차단 | 검증된 복사 성공 후에만 플래그 클리어 → 재부팅 시 재복사 |
| **새 펌웨어 기능 고장** | **워치독(IWDG) + Factory 롤백 (자동 복구)** |
| 부트로더 자기 리셋 | 부트로더 긴 Flash 작업 중엔 워치독 미가동 |

> **워치독 원칙**: IWDG는 앱을 감시할 뿐, **부트로더의 긴 Flash 작업(캡처/적용/롤백)은 감시하지 않는다.** 그래서 부트로더는 IWDG를 자동 시작하지 않고 **TRIAL 앱 점프 직전에만** 켠다(`BL_StartWatchdog`). CubeMX가 생성한 `MX_IWDG_Init()` 자동 호출은 FW_BOOT에서 주석 처리되어 있으며, **재생성 시 다시 주석 처리해야 함.**
>
> ⚠️ **단, 앱은 자기 워치독을 스스로 켠다**: `FW_APP`의 `main()`은 `MX_IWDG_Init()`을 호출하므로(주석 처리하지 않음) **앱이 실행되는 동안 IWDG는 항상 돌고 있다.** 부트로더의 TRIAL 무장과는 별개다. 즉 "평상시 부팅에서는 워치독이 꺼져 있다"는 말은 **부트로더 기준**이며, 앱 안에서는 언제나 갱신(pet)이 필요하다.

### 워치독 갱신 방식 — 태스크 체크인 (FreeRTOS 전환 후)

 **체크인 방식**(`FW_APP/Core/Src/wdg_monitor.c`)을 쓴다:

- 각 태스크가 자기 슬롯에 주기적으로 `Wdg_CheckIn()`
- `wdgTask`(100ms 주기)는 **등록된 모든 슬롯이 기한 내 체크인했을 때만** IWDG를 갱신
- 하나라도 늦으면 **panic을 래치**(회복해도 다시 pet하지 않음) → IWDG 리셋 → Factory 롤백
- 실패 경로(다운로드 실패, 스택 오버플로, 힙 부족, `Error_Handler`)는 전부 `Wdg_Panic()`으로 연결

## 5. 프로젝트 구조

```
Prj_FW-Update_STM32F429/
├── FW_BOOT/                  # 부트로더 (@ 0x0800_0000)
│   ├── Core/Src/main.c       #   BL_HandleUpdate(상태머신) / BL_ApplyStagingToApp
│   │                         #   BL_Rollback / BL_StartWatchdog / BL_EnsureFactory / BL_JumpToApplication
│   ├── Core/Src/flash_if.c   #   Flash erase/write/meta + CRC32
│   └── Core/Inc/flash_if.h   #   메모리 맵 + FwMeta/FwState 정의 (공용 원본)
├── FW_APP/                   # 애플리케이션 (@ 0x0802_0000, 512K) — FreeRTOS
│   ├── Core/Src/main.c       #   태스크(led/uart/net) + FwTransport(uart/tcp) + 명령 매처
│   │                         #   자가확인 / 상태 한 줄(App_BuildStatusLine)
│   ├── Core/Src/ota.c        #   OTA 프로토콜 — 수신·소거·기록·검증·마무리 (main.c에서 분리)
│   ├── Core/Src/sys_info.c   #   보드/장치 식별 + 메모리 사용량 보고 (FWSYS???)
│   ├── Core/Src/wdg_monitor.c#   태스크 체크인 워치독 감시 (자동 롤백의 핵심)
│   ├── Core/Src/uart_link.c  #   USART3 순환 DMA + IDLE → StreamBuffer
│   ├── Core/Src/net_link.c   #   TCP 링크 (BSD 소켓)
│   ├── Core/Src/fw_info.c    #   빌드 변형/버전 표식 + 다른 슬롯 버전 스캔
│   ├── Core/Src/dbg_uart.c   #   UART 디버그 출력(부팅 배너·장애 보고)
│   ├── LWIP/                 #   CubeMX 생성 lwIP(RTOS 모드) + ethernetif + lwipopts
│   │                         #   ethernetif.c에 RX 데드락 + PHY 링크 복구
│   ├── Core/Src/flash_if.c   #   소거가 RAM 상주(.RamFunc)
│   └── Core/Inc/flash_if.h   #
├── UI_Monitor/               # C# UI (.hex 파서 + 프로토콜 전송 + 진행률)
│   ├── Form1.cs              #   UI/프로토콜(SendFirmwareSequence) — RS-232/Ethernet 선택
│   │                         #   태그 로그(LogCh/LogKind) + 상태 이벤트 + 모드 전환 시 자동 Close
│   └── FwLink.cs             #   IFwLink + SerialLink / TcpLink (전송 추상화)
├── docs/                     # 데이터시트, 메모리맵 이미지
└── README.md
```

> ** flash_if.c 는 더 이상 동일하지 않다**
> `FW_APP` 쪽만 `FlashIf_EraseRange()`가 **RAM 상주 루틴**(`FlashIf_EraseSectorFromRam`, `.RamFunc`)으로 섹터를 하나씩 지우며 소거 중 IWDG를 갱신한다.

## 6. 핵심 소스

### FW_APP (`main.c`)

CubeMX 생성 코드 + 태스크 생성 + 명령 매처가 있다. OTA 프로토콜과 시스템 정보는 각각
`ota.c` / `sys_info.c`로 분리했다.

`main()` 맨 앞에서 **벡터 테이블을 실행영역으로 재배치한다(필수)**:

```c
SCB->VTOR = 0x08020000U;   // 없으면 앱이 부트로더 벡터를 보고 점프 직후 크래시
```

- **부팅 시** — 메타가 `TRIAL`이면 자가진단을 통과시켜 `CONFIRMED`를 기록한다.
  못 하면 워치독이 리셋 → 부트로더가 Factory로 롤백한다(§4).
- **태스크** — `uartTask`/`netTask`가 각자 경로에서 명령을 감시하고,
  `ledTask`가 하트비트 겸 CPU 기아 감지, `wdgTask`가 체크인을 취합해 IWDG를 pet 한다(§4).
- **명령 매처** — 8바이트 슬라이딩 윈도. `FWUPDATE` / `FWFACTRY` / `FWROLLBK` / `FWSYS???`
  (+ TCP 전용 `FWINFO??`). 매칭되면 `ota.c` 또는 `sys_info.c`로 넘긴다(§3).
- **상태 한 줄**(`App_BuildStatusLine`) — UART 주기 로그와 `FWINFO??` 응답이 이 함수를 공유한다.

### FW_APP (`ota.c`) — 다운로드 프로토콜

`main.c`가 1,474줄까지 커져 CubeMX 생성 코드와 OTA 프로토콜이 뒤섞여 있었다. 프로토콜을 통째로 분리했고(순수 이동), 이어서 252줄짜리 단일 함수를 단계별로 쪼갰다.

```c
Ota_EnterDownloadMode(t, target)   // 세션 초기화 + LED, 실패 시 ota_report_failure
└─ ota_run(s)                      // ★ 여기만 보면 전체 흐름이 읽힌다
   ├─ send "READY"
   ├─ ota_recv_header(s)           // [크기 4B][CRC32 4B] 수신·검증 (크기 이상 → NACK)
   ├─ ota_erase(s)                 // 섹터 단위 소거 (IWDG 직접 pet + osDelay 양보) → ACK
   ├─ ota_recv_chunks(s)           // buf[256], 청크마다 기록 후 ACK
   │  └─ ota_recv_one_chunk(s,..)  // CRC16 검증, 최대 5회 재수신
   ├─ ota_verify(s)                // 전체 CRC32 → "DONE" / "CRCERR"
   └─ ota_finish(s)                // FACTORY는 복귀 / STAGING은 메타 기록 후 재부팅
```

- 각 단계는 `bool`을 반환하고, 실패 상태(어디까지 기록했는지 등)는 `OtaSession`에 남는다.
  분리 전에는 `goto stop` 12곳이 한 곳으로 모이는 구조였다.
- **`Ota_RequestRollback()`도 같은 파일에 있다**(`FWROLLBK`).
- ⚠️ 실패 진단이 `UartLink_*` 카운터를 찍는다 — **TCP 세션이 실패해도 UART 카운터가 나온다.**
  전송 계층에 무관해야 할 코드가 UART에만 있는 값을 참조하는 것으로, 분리하면서 드러났다.
  동작을 바꾸지 않으려고 그대로 두었다.

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

### FW_BOOT — 앱 점프 (`BL_JumpToApplication`)

```c
uint32_t appStack = *(volatile uint32_t *)(APP_ADDRESS);      // 벡터[0] = 초기 SP
uint32_t appEntry = *(volatile uint32_t *)(APP_ADDRESS + 4U); // 벡터[1] = Reset_Handler

// ① 유효성 검사 — 앱이 안 구워졌으면 Flash가 0xFFFFFFFF라 SP가 RAM 밖이다
if (appStack < RAM_START || appStack > RAM_END) return;       // 앱 없음 → 부트로더에 머무름

// ② 인계 전 정리 — 부트로더가 켜둔 것이 남으면 앱 초기화 중 인터럽트가 튄다
HAL_RCC_DeInit();
HAL_DeInit();
SysTick->CTRL = 0; SysTick->LOAD = 0; SysTick->VAL = 0;
__disable_irq();

// ③ 앱의 벡터 테이블로 전환 (앱도 자기 main()에서 다시 설정하지만 여기서도 맞춘다)
SCB->VTOR = APP_ADDRESS;

// ④ 스택 포인터를 앱 것으로 교체
__set_MSP(appStack);

// ⑤ ★ 인터럽트 재개 — 정상 리셋 상태를 재현한다
//    빠뜨리면 앱의 SysTick이 안 울려 HAL_Delay()가 영원히 멈춘다
__enable_irq();

((void (*)(void))appEntry)();   // 앱 시작. 돌아오지 않는다
```

| 단계 | 빠뜨리면 |
|---|---|
| ① 유효성 검사 | 빈 Flash로 점프 → 하드폴트 |
| ② `HAL_DeInit` / SysTick off | 앱 초기화 중 남은 인터럽트가 튐 |
| ③ `SCB->VTOR` | 앱이 부트로더 벡터를 봄 → 점프 후 크래시 |
| ④ `__set_MSP` | 부트로더 스택을 그대로 씀 |
| ⑤ `__enable_irq` | **`HAL_Delay()`가 무한 대기** (LED 켜진 채 멈춤) |

## 7. 빌드 & 사용법

### 최초 1회 (ST-Link)
1. STM32CubeProgrammer로 **Full chip erase** (깨끗한 시작)
2. **FW_BOOT** 빌드 → 플래시 (`0x0800_0000`)
3. **FW_APP** 빌드 → 플래시 (`0x0802_0000`) — 이 앱이 OTA 수행 + 최초 부팅 시 Factory로 자동 복사됨
   - `.hex` 출력: Project Properties → C/C++ Build → Settings → **MCU Post build outputs → Convert to Intel Hex file**

### 이후 업데이트 (ST-Link 불필요)
1. 새 FW_APP을 빌드해 `FW_APP.hex` 생성 (**플래시하지 않음**)
2. **[open]** 으로 `.hex` 선택(경로·크기 표시) → **[F/W Download]**
5. 진행률 100% → `DONE` → 자동 재부팅 → 부트로더 검증·적용 → 새 펌웨어 시험 부팅 → 자가확인 → 정상

### LED 상태
| LED | 의미 |
|---|---|
| 🔴 적색 3회 빠른 깜빡 | 부트로더 실행(점프 직전) |
| 🔴 적색 계속 깜빡 | 유효 앱 없음(부트로더 모드) |
| 🔴 적색 켜짐 | 롤백 중(Factory→App 복사) |
| 🔵 파랑 켜짐 | 다운로드 모드 / 업데이트 적용 중 |
| 🟢+🔵 잠깐 | 최초 부팅 Factory 캡처 중(1회성) |
| 🟢/🔵 하트비트 | 앱 정상 실행 (패턴은 앱 버전에 따름) |

## 8. 로드맵

```
✅ 1. 부트로더/앱 분리 + 점프 (VTOR / MSP / IRQ 재활성화)
✅ 2. Flash erase/write 드라이버 (flash_if)
✅ 3. 메모리 맵 재설계 (Bootloader / Metadata / App / Factory / Staging)
✅ 4. 앱: UART "FWUPDATE" 인지 → Staging에 hex 수신
✅ 5. 앱: 수신 완료 → Metadata 플래그 + 재부팅
✅ 6. 부트로더: 플래그 확인 → Staging→실행영역 복사(적용)
✅ 7a. CRC 검증 (전체 CRC32 + 청크 CRC16 자동 재전송)
✅ 7b. Factory 슬롯 + 워치독(IWDG) + trial/confirm 자동 롤백
✅ 8. UI: Open/Download 분리 + 실시간 진행률/속도 표출
─────────── 여기까지 UART OTA + 무결성 + 자동 롤백 완성 ───────────
✅ 9. Ethernet(lwIP/TCP) 업데이트 경로 (UART와 병존, 전송계층만 교체)
✅ 10. FreeRTOS 전환 (태스크 분리 + 체크인 워치독 + UART DMA + 소켓 API)
✅ 10a. Factory 슬롯 직접 쓰기("FWFACTRY") — 롤백 검증용
✅ 10b. 강제 롤백("FWROLLBK") — UI "FACTORY WORK" 버튼
─────────── 여기까지 기능 완성. 이하는 안정성 보강 ───────────
✅ 11a. UI: Intel HEX 파서 검증 강화 (체크섬/슬롯 경계/시작주소/초기 SP)
✅ 11b. UART DMA 콜백 원자성 (IDLE/HT·TC 선점 경합 제거)
✅ 11c. Flash 소거를 RAM 상주 루틴으로 (소거 중 IWDG 갱신)
✅ 11d. Ethernet PHY 링크 복구 (오토네고 실패 자가 회복)
```

### FreeRTOS 전환 요약

`FW_APP`만 FreeRTOS로 전환했다. **`FW_BOOT`는 베어메탈 유지** — 검증·적용·롤백·점프만 하므로 RTOS는 위험만 늘린다.

| 태스크 | 우선순위 | 스택 | 역할 |
|---|---|---|---|
| `EthIf` | Realtime(48) | 1024B | ETH RX → lwIP (CubeMX 생성) |
| `tcpip_thread` | High(40) | 2048B | lwIP 코어 (lwIP 생성) |
| `wdgTask` | AboveNormal(32) | 512B | 체크인 취합 → IWDG pet |
| `uartTask` | Normal(24) | 2560B | UART 명령 감시 + OTA |
| `netTask` | Normal(24) | 2560B | TCP(포트 7) 동일 |
| `ledTask` | **Low(8)** | 1024B | 하트비트 **겸 CPU 기아 감지** |

주요 변경:
- **UART 수신**: 10ms 블로킹 폴링 → **순환 DMA + IDLE → StreamBuffer**
- **TCP**: RAW API + 4KB 링버퍼 + 수동 펌핑 → **BSD 소켓**(`net_link.c` 250줄→204줄). `tcpip_thread`가 상시 돌므로 수동 펌핑이 불필요해졌다
- **전송 추상화**: `FwTransport{recv,send,pet,quiesce}` vtable — 프로토콜은 UART/TCP에 무관
- **OTA 상호배제**: `otaMutex` **try-lock**(블로킹 금지 — 대기하면 앞 세션 종료 후 두 번째 다운로드가 시작돼 Staging이 깨진다)

### 안정성 보강 요약

| # | 대상 | 한 줄 요약 |
|---|---|---|
| 11a | `UI_Monitor/Form1.cs` | HEX 레코드 체크섬·슬롯 경계·시작 주소·초기 SP를 **보드에 보내기 전에** 검증. 슬롯 밖(옵션바이트 등) 레코드는 경고 후 제외 |
| 11b | `FW_APP/uart_link.c` | `HAL_UARTEx_RxEventCallback`의 `lastPos` 갱신 구간을 `taskENTER_CRITICAL_FROM_ISR()`로 보호 |
| 11c | `FW_APP/flash_if.c` | 단일 섹터 소거를 `.RamFunc`에 두고, BSY 폴링 루프에서 IWDG를 계속 갱신 |
| 11d | `FW_APP/ethernetif.c`, `ota.c` | PHY 오토네고 실패에서 자가 회복 + 리셋 전 소켓 정상 종료 |

## 9. ⚠️ CubeMX 재생성 시 반드시 되돌릴 것

`.ioc`를 열어 Generate Code를 하면 아래가 **조용히 기본값으로 되돌아간다.** 되돌아가도 빌드는 통과하고, 증상은 한참 뒤에 엉뚱한 모습으로 나타난다. 재생성 직후 이 표를 처음부터 훑을 것.

| # | 대상 | 되돌아가면 생기는 일 | 확인/조치 |
|---|---|---|---|
| 1 | **FW_BOOT의 `MX_IWDG_Init()` 자동 호출** | 부트로더가 시작하자마자 IWDG를 켠다 → Factory 캡처/적용/롤백 같은 긴 Flash 작업 중 자기 리셋 → **부팅 루프** | `main()`의 호출을 다시 주석 처리. IWDG는 `BL_StartWatchdog()`에서 TRIAL 점프 직전에만 켠다 |
| 2 | **링커 `.ld`의 FLASH ORIGIN/LENGTH** | 부트로더/앱이 서로의 영역을 침범 | FW_BOOT `0x0800_0000`/64K, FW_APP `0x0802_0000`/512K |
| 3 | **FW_BOOT `.ioc`의 LWIP/ETH/USB** | 부트로더가 64KB를 넘겨 `region FLASH overflowed` (약 76KB) | 전부 해제. 부트로더는 `RCC/SYS/GPIO/IWDG/USART3`만 필요 (현재 12.95KB) |
| 4 | **`lwipopts.h`** | 튜닝값이 기본값으로 (특히 `MEM_SIZE`) | USER CODE 블록 밖의 수정은 매번 재확인. **파일 끝의 `USER CODE BEGIN 1`은 생성된 `#define`들보다 뒤이므로**, 앞으로 값을 덮을 일이 있으면 거기에 `#undef`+`#define`으로 넣으면 재생성에도 살아남는다 |
| 4b | **`stm32f4xx_hal_conf.h`의 `PHY_READ_TO` / `PHY_WRITE_TO`** | 기본값 `0x0000FFFF` = **65.5초**로 되돌아간다. MDIO가 한 번 물리면 `EthLink` 스레드가 1분 넘게 블로킹되는데, **EthLink는 워치독 등록 대상이 아니라**(`Wdg_Register`는 led/uart/net 3개뿐) 아무도 감지하지 못한다 → 링크 관리와 PHY 재협상 복구가 조용히 멎는다 | 둘 다 `100U`인지 확인. MDC ~1.8MHz에서 정상 MDIO 트랜잭션은 40us 미만이라 마진이 2000배가 넘는다. (FW_BOOT 쪽 같은 정의는 `HAL_ETH_MODULE_ENABLED`가 꺼져 있어 죽은 값이므로 무시해도 된다) |
| 5 | **`ethernetif.c`의 수정 6곳** (Ethernet 수신·링크 관련) | 되돌아가면 **Ethernet 수신이 이따금 죽는다** — 링크는 Up인데 ARP조차 무응답, 리셋으로만 복구. e·f가 되돌아가면 **TCP OTA 후 링크 LED가 안 켜지는 증상이 부활한다** | 아래 6개가 모두 살아 있는지 확인. **전부 USER CODE 블록 밖이다** (바로 위 `INTERFACE_THREAD_STACK_SIZE`와 `EthDbg_Link`/`EthPhy_Recover`는 안이라 유지된다) |

## 10. 트러블슈팅 (겪고 해결한 것)

| 증상 | 원인 | 해결 |
|---|---|---|
| 점프 후 멈춤/크래시 | 앱이 부트로더 벡터테이블을 봄 | 앱에서 `SCB->VTOR = 0x08020000` |
| LED 켜진 채 멈춤 (HAL_Delay 무한대기) | 부트로더가 `__disable_irq()` 후 안 켜줌 | 점프 직전 `__enable_irq()` |
| 앱 초기화 중 멈춤 | 앱이 ETH 재초기화 실패 | 앱에서 불필요한 주변장치 초기화 제거 |
| **부팅 무한 루프** | CubeMX가 IWDG를 부트로더 시작 시 자동 켜서 긴 Flash 작업 중 자기 리셋 | `MX_IWDG_Init()` 자동 호출 주석 처리, `BL_StartWatchdog()`에서만 시작 |
| **FW_BOOT 빌드 시 `region FLASH overflowed`** | 부트로더 `.ioc`에 **LWIP/ETH/USB가 켜져 있어** 64KB 초과(약 76KB) | CubeMX에서 LWIP·ETH·USB_OTG_FS 해제 → **12.95KB**. 부트로더는 `RCC/SYS/GPIO/IWDG/USART3`만 있으면 된다 |
| **TCP OTA/롤백 후 RJ45 초록 링크 LED가 안 켜짐** (UART는 무사) | 소켓을 연 채 `NVIC_SystemReset()` → 상대 PC는 연결이 살아 있다고 믿고 재전송을 계속 쏜다. 그런데 **`SB177`(기본 ON)로 PHY의 nRST가 MCU NRST에 직결**(UM1974 p.32)이라 PHY도 같이 하드웨어 리셋된다. 리셋에서 나온 PHY가 오토네고를 시작하려는데 매체가 시끄러워 협상이 깨진다 → **parallel detect로 폴백**(로그의 `100M-HALF`/`10M-HALF` — half duplex가 그 signature) 또는 아예 링크 실패. 게다가 이 펌웨어는 원래 **PHY 레지스터에 쓰기를 한 번도 하지 않아** 회복 수단이 없었다 | ① 리셋 전 소켓 정상 종료(`FwTransport.quiesce`) ② `low_level_init`에서 링크를 래치하지 않음 ③ 링크 스레드에 속도 재평가 분기 + 장기 down 시 오토네고 재시작(`EthPhy_Recover`) |

## 11. 참고 자료 (docs)

- `docs/Memory Map.png` — STM32F429 메모리 맵
- `docs/NUCLEO-F429ZI Datasheet.pdf` — STM32 Nucleo-144 보드 사용자 매뉴얼.
- `docs/STM32F427ZI Datasheet.pdf` — MCU 데이터시트

## 개발 환경
- **보드**: NUCLEO-F429ZI (STM32F429ZI, Flash 2MB / SRAM 256KB[192KB+64KB CCM])
- **IDE**: STM32CubeIDE / **UI**: C# WinForms (.NET Framework, System.IO.Ports)
