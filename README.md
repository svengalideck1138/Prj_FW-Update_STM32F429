# STM32F429 펌웨어 업데이트 시스템

<p align="center">
  <img src="docs/NUCLEO-F429ZI%20Top%20View.png" alt="NUCLEO-F429ZI Top View" width="480">
</p>

NUCLEO-F429ZI 보드를 대상으로, **ST-Link 없이** **UART 또는 Ethernet** 케이블과 **C# UI(UI_Monitor)** 만으로 펌웨어를 교체하는 프로젝트입니다. STM32 내부에 커스텀 부트로더를 두고, **애플리케이션이 스스로 새 펌웨어를 내려받아 재부팅하면 부트로더가 검증·적용**하는 방식(OTA)을 구현합니다.

> **목표 시나리오**: 제품 출하 후 현장에서 ST-Link 없이, 케이블만 연결해 C# UI로 `.hex` 펌웨어를 무선 업데이트한다.

> **현재 상태**: **UART + Ethernet(TCP) OTA + 무결성 검증(CRC) + 자동 롤백 + FreeRTOS 전환까지 완성.** UI에서 RS-232/Ethernet 경로를 선택해 업데이트할 수 있고, 전송이 깨지거나 새 펌웨어가 고장 나도 안전하게 복구된다. 앱은 FreeRTOS 태스크로 동작하며 **체크인 방식 워치독**이 자동 롤백을 보장한다.
>
> **실기 검증 완료**: UART OTA / Ethernet OTA / Factory 직접 쓰기(**UART·Ethernet 양쪽**) / 자동 롤백(BAD 빌드→워치독→복원) / 강제 롤백(`FWROLLBK`) / 청크 재전송 / TCP 재접속 반복 / Ethernet 상태 표출(`FWINFO??`).
>
> **미해결**: 없음.
>
> 오래 남아 있던 "Ethernet 전송 후 이따금 ping 불통"은 ST 생성 `ethernetif.c`의 **RX 데드락**으로 원인이 확정되어 수정했다(§10 트러블슈팅). 다만 확률적 레이스였으므로 재발 여부는 계속 지켜볼 것.
> - `ledTask` 스택 1024B 상향 후 자원 재측정 (§8 자원 실측 — 값이 부팅마다 크게 흔들리니 여러 번 볼 것)

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

---

## 3. 전송 프로토콜 (UART 115200 8N1 / Ethernet TCP:7) — 2겹 무결성 검증

> 아래 표는 전송 계층(UART/TCP)에 무관하게 동일하다. MCU는 `FwTransport` 추상화로, PC는 `IFwLink` 추상화로 같은 바이트 시퀀스를 주고받는다.

### 명령 목록 (모두 8바이트 ASCII)

| 명령 | 전송 계층 | 동작 | 재부팅 |
|---|---|---|---|
| `FWUPDATE` | UART / TCP | Staging에 기록 → 부트로더가 App에 적용 | O |
| `FWFACTRY` | UART / TCP | Factory 슬롯에 **직접** 기록(골든 이미지 교체) | X |
| `FWROLLBK` | UART / TCP | Factory→App 복원 요청 (데이터 없음, `DONE`/`FAILED` 응답) | O |
| `FWINFO??` | **TCP 전용** | 상태 한 줄 조회 (아래 참고) | X |

명령은 8바이트 슬라이딩 윈도로 매칭하므로, 뒤따르는 헤더/청크는 스트림에 그대로 남아 다음 단계가 이어서 읽는다.

부팅 배너와 TCP 접속 시 `[FW] cmds=...`가 출력되므로 **보드 펌웨어가 어떤 명령을 아는지** 눈으로 확인할 수 있다.

### `FWINFO??` — 상태 조회 (TCP 전용)

```
PC→MCU : "FWINFO??"
MCU→PC : "[FW] app=FW_App02  factory=FW_FACTORY  up=26s\r\n"
```

**왜 TCP에만 있나.** RS-232는 보드가 1초마다 배너를 스스로 뿜고 그게 뷰어에 그대로 쌓인다. TCP에서 같은 짓을 하면 OTA 청크/ACK 스트림 사이에 로그가 끼어 프로토콜이 깨진다. 그래서 **보드는 요청받았을 때만 응답**하고 폴링 주기는 PC가 쥔다(UI 타이머 1초). 화면상 결과는 RS-232와 동일하다.

- 응답 문자열은 UART 주기 로그와 **같은 함수**(`App_BuildStatusLine()`)가 만든다 → 두 뷰어의 글자가 어긋나지 않는다.
- UI는 전송 중 타이머를 멈춘다(`SetTransferUi`). 폴링과 OTA가 같은 소켓을 쓰므로 겹치면 전송이 깨진다.

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
>
> ⚠️ **단, 앱은 자기 워치독을 스스로 켠다**: `FW_APP`의 `main()`은 `MX_IWDG_Init()`을 호출하므로(주석 처리하지 않음) **앱이 실행되는 동안 IWDG는 항상 돌고 있다.** 부트로더의 TRIAL 무장과는 별개다. 즉 "평상시 부팅에서는 워치독이 꺼져 있다"는 말은 **부트로더 기준**이며, 앱 안에서는 언제나 갱신(pet)이 필요하다.

### 워치독 갱신 방식 — 태스크 체크인 (FreeRTOS 전환 후)

베어메탈에서는 단일 메인 루프가 pet했기 때문에 **"루프가 멈춘다 = pet이 끊긴다 = 리셋 = 롤백"** 이 자동으로 성립했다. 이것이 자동 롤백의 전제다.

RTOS에서 pet 전용 태스크를 순진하게 두면 이 전제가 깨진다 — 통신/OTA 태스크가 죽어도 그 태스크는 멀쩡히 계속 pet 하므로 **"멈췄는데 리셋이 안 되는"** 상태가 되어 안전장치가 조용히 무력화된다.

그래서 **체크인 방식**(`FW_APP/Core/Src/wdg_monitor.c`)을 쓴다:

- 각 태스크가 자기 슬롯에 주기적으로 `Wdg_CheckIn()`
- `wdgTask`(100ms 주기)는 **등록된 모든 슬롯이 기한 내 체크인했을 때만** IWDG를 갱신
- 하나라도 늦으면 **panic을 래치**(회복해도 다시 pet하지 않음) → IWDG 리셋 → Factory 롤백
- 실패 경로(다운로드 실패, 스택 오버플로, 힙 부족, `Error_Handler`)는 전부 `Wdg_Panic()`으로 연결

`ledTask`를 **최저 우선순위**로 두는 것도 의도적이다: 상위 태스크가 CPU를 독점하면 이 태스크가 가장 먼저 굶으므로, 하트비트 LED와 CPU 기아 감지가 같은 메커니즘이 된다.

---

## 5. 프로젝트 구조

```
Prj_FW-Update_STM32F429/
├── FW_BOOT/                  # 부트로더 (@ 0x0800_0000)
│   ├── Core/Src/main.c       #   BL_HandleUpdate(상태머신) / BL_ApplyStagingToApp
│   │                         #   BL_Rollback / BL_StartWatchdog / BL_EnsureFactory / BL_JumpToApplication
│   ├── Core/Src/flash_if.c   #   Flash erase/write/meta + CRC32
│   └── Core/Inc/flash_if.h   #   메모리 맵 + FwMeta/FwState 정의 (공용 원본)
├── FW_APP/                   # 애플리케이션 (@ 0x0802_0000, 512K) — FreeRTOS
│   ├── Core/Src/main.c       #   태스크(led/uart/net) + FwTransport(uart/tcp)
│   │                         #   App_EnterDownloadMode(t, target) / App_Crc16 / 자가확인
│   ├── Core/Src/wdg_monitor.c#   태스크 체크인 워치독 감시 (자동 롤백의 핵심)
│   ├── Core/Src/uart_link.c  #   USART3 순환 DMA + IDLE → StreamBuffer
│   ├── Core/Src/net_link.c   #   TCP 링크 (BSD 소켓)
│   ├── Core/Src/fw_info.c    #   빌드 변형/버전 표식 + 다른 슬롯 버전 스캔
│   ├── Core/Src/dbg_uart.c   #   UART 디버그 출력(부팅 배너·장애 보고)
│   ├── LWIP/                 #   CubeMX 생성 lwIP(RTOS 모드) + ethernetif + lwipopts
│   ├── Core/Src/flash_if.c   #   FW_BOOT와 동일하게 유지
│   └── Core/Inc/flash_if.h   #
├── UI_Monitor/               # C# UI (.hex 파서 + 프로토콜 전송 + 진행률)
│   ├── Form1.cs              #   UI/프로토콜(SendFirmwareSequence) — RS-232/Ethernet 선택
│   └── FwLink.cs             #   IFwLink + SerialLink / TcpLink (전송 추상화)
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
[FACTORY WORK] RunSlotSwitch("FWROLLBK") → Factory→App 복원 (확인 대화상자 후 전송)
```

**뷰어에 보드 상태가 뜨는 경로가 전송 모드마다 다르다:**

| 모드 | 방식 |
|---|---|
| RS-232 | 보드가 1초마다 배너를 스스로 송신 → `SerialPort.DataReceived`가 받아 뷰어에 출력 |
| Ethernet | `_infoTimer`(1초)가 `FWINFO??`를 폴링 → 응답 한 줄을 뷰어에 출력 |

`_infoTimer`의 Tick은 UI 스레드에서 돌고 전송은 `Task.Run`이므로, `SetTransferUi(true/false)`가 타이머를 멈추고 재개하는 것만으로 폴링과 OTA가 겹치지 않는다.

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
3. UI_Monitor에서 전송 경로 선택:
   - **RS-232**: COM 포트 **115200** OPEN
   - **Ethernet**: Transfer Mode `Ethernet` 선택 → IP `192.168.172.128` / Port `7` → **Connect**
     - PC NIC는 **같은 `192.168.172.0/24` 서브넷의 고정 IP**여야 한다(보드는 DHCP를 쓰지 않는다). 개발 PC는 현재 `192.168.172.150`.
     - 접속되면 뷰어에 상태 줄이 **1초마다** 갱신된다(`FWINFO??` 폴링). 안 뜨면 아래 순서로 확인:
       `ping 192.168.172.128` → 실패 시 `arp -a 192.168.172.128`으로 **ARP 응답 유무** 확인 → ARP도 없으면 §8 "알려진 한계"의 RX 데드락 항목 참고(보드 리셋으로 복구).
4. **[open]** 으로 `.hex` 선택(경로·크기 표시) → **[F/W Download]**
5. 진행률 100% → `DONE` → 자동 재부팅 → 부트로더 검증·적용 → 새 펌웨어 시험 부팅 → 자가확인 → 정상

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
✅ 7b. Factory 슬롯 + 워치독(IWDG) + trial/confirm 자동 롤백
✅ 8. UI: Open/Download 분리 + 실시간 진행률/속도 표출
─────────── 여기까지 UART OTA + 무결성 + 자동 롤백 완성 ───────────
✅ 9. Ethernet(lwIP/TCP) 업데이트 경로 (UART와 병존, 전송계층만 교체)
✅ 10. FreeRTOS 전환 (태스크 분리 + 체크인 워치독 + UART DMA + 소켓 API)
✅ 10a. Factory 슬롯 직접 쓰기("FWFACTRY") — 롤백 검증용
✅ 10b. 강제 롤백("FWROLLBK") — UI "FACTORY WORK" 버튼
✅ 10c. 상태 조회("FWINFO??") — Ethernet 뷰어에도 버전 표출
```

### FreeRTOS 전환 요약 (항목 10)

`FW_APP`만 FreeRTOS로 전환했다. **`FW_BOOT`는 베어메탈 유지** — 검증·적용·롤백·점프만 하므로 RTOS는 위험만 늘린다.

| 태스크 | 우선순위 | 스택 | 역할 |
|---|---|---|---|
| `EthIf` | Realtime(48) | 1024B | ETH RX → lwIP (CubeMX 생성) |
| `tcpip_thread` | High(40) | 2048B | lwIP 코어 (lwIP 생성) |
| `wdgTask` | AboveNormal(32) | 512B | 체크인 취합 → IWDG pet |
| `uartTask` | Normal(24) | 2048B | UART "FWUPDATE"/"FWFACTRY" + OTA |
| `netTask` | Normal(24) | 2048B | TCP(포트 7) 동일 |
| `ledTask` | **Low(8)** | 1024B | 하트비트 **겸 CPU 기아 감지** |

주요 변경:
- **UART 수신**: 10ms 블로킹 폴링 → **순환 DMA + IDLE → StreamBuffer**
- **TCP**: RAW API + 4KB 링버퍼 + 수동 펌핑 → **BSD 소켓**(`net_link.c` 250줄→204줄). `tcpip_thread`가 상시 돌므로 수동 펌핑이 불필요해졌다
- **전송 추상화**: `FwTransport{recv,send,pet}` vtable — 프로토콜은 UART/TCP에 무관
- **OTA 상호배제**: `otaMutex` **try-lock**(블로킹 금지 — 대기하면 앞 세션 종료 후 두 번째 다운로드가 시작돼 Staging이 깨진다)

### 자원 실측 (`[RES]` 로그, OTA 수행 후 최저 수위)

`osThreadGetStackSpace()`는 **그 태스크가 지금까지 남긴 최소 여유(바이트)** 다.

> ⚠️ **이 값은 "부팅 이후" 최저 수위라 리셋할 때마다 초기화된다.** 그 부팅에서 실제로 탄 코드 경로만 반영하므로, OTA·Factory 쓰기처럼 깊게 들어가는 작업을 **한 번 거친 뒤에** 읽어야 최악값에 가깝다.
>
> 실측 예: `netTask`는 부팅에 따라 **636B ~ 1180B**로 갈렸다(다운로드 경로를 탔는지 여부). 반면 `uartTask`·`wdgTask`·`ledTask`는 부팅 간 편차가 10B 미만으로 안정적이었다.

**현재 배정** (관측된 최대 사용량 기준):

| 태스크 | 스택 | 최대 사용 | 여유 |
|---|---|---|---|
| `uartTask` | 2048B | 264B | 87% |
| `wdgTask` | 512B | 152B | 70% |
| `ledTask` | 1024B | 328B | 69% |
| `netTask` | 2048B | 1180B | 42% |

힙: `now=12040B` / `min=7832B`.

> `ledTask`는 512B에서 시작했고 실측 사용량은 320~328B(여유 36~38%)로 **안정적이었다.** 오버플로 위험이 확인된 것은 아니지만 다른 태스크(42~87%)에 비해 혼자 얇아 **1024B로 올렸다**(힙 여유가 12KB라 비용이 작다). 768B로도 충분하다.
>
> 사용량에는 `App_PrintStatusLine()`의 상태 문자열 조립 버퍼(`STATUS_LINE_MAX`=96B)가 포함되어 있다.

> 📌 **측정값 해석 시 주의.** 스택은 FreeRTOS 힙에서 할당되므로, `stack_size`를 바꿨는데 `[RES]`의 `heap now=`가 그만큼 변하지 않았다면 **새 펌웨어가 보드에 올라가지 않은 것**이다. 실제로 이 프로젝트에서 그 상태의 측정값을 "사용량이 급증했다"고 잘못 읽은 적이 있다. 여유값(`led=`)만 보면 크기 변경 여부를 알 수 없다 — `heap now=`를 함께 볼 것.

### Ethernet(TCP) 경로 요약 (항목 9)

동일한 프로토콜/CRC/상태머신/Flash 계층을 **그대로 재사용**하고 **전송 계층만 UART↔TCP로 교체**했다. UART 경로는 유지되어 UI에서 **RS-232/Ethernet 중 선택** 가능.

| 구성 | 내용 |
|---|---|
| MCU 스택 | **lwIP RTOS 모드** (`NO_SYS=0`, 자체 `tcpip_thread`) — FreeRTOS 전환(항목 10) 때 재작성됨 |
| 보드 IP/포트 | **고정 `192.168.172.128 : 7`** (보드=TCP 서버, PC=클라이언트) |
| 전송 추상화 | `FwTransport{recv,send,pet}` vtable → `uartTransport`/`tcpTransport` (`FW_APP/Core/Src/main.c`) |
| TCP 링크 | `FW_APP/Core/Src/net_link.c` — **BSD 소켓 API**. `SO_RCVTIMEO`로 '정확히 n바이트 블로킹 수신'을 직접 얻는다 |
| 트리거 | `uartTask`/`netTask`가 각자 자기 경로를 감시 → 명령이 온 경로로 다운로드. `s_otaMutex` try-lock으로 동시 세션 차단 |
| C# UI | `IFwLink` 인터페이스 + `SerialLink`/`TcpLink`(`UI_Monitor/FwLink.cs`). Transfer Mode 라디오로 선택, Socket Connect/Disconnect 배선 |

> **[R4] RAW API → 소켓으로 다시 쓴 이유.** 베어메탈(NO_SYS) 시절에는 RAW API + 4KB 링버퍼 + 수동 펌핑(`MX_LWIP_Process`)으로 '블로킹 수신'을 흉내 내야 했다. FreeRTOS로 오면서 lwIP가 자체 `tcpip_thread`를 갖게 되어 그 구조는 (a) 더 이상 필요 없고 (b) RAW API를 tcpip 스레드 밖에서 부르는 것이라 **스레드 안전하지도 않다**. 소켓 API가 블로킹 수신을 그대로 제공하므로 링버퍼·백프레셔·펌핑 코드가 통째로 사라졌다.

> ⚠️ **소켓은 `netTask`만 만진다.** `lwipopts.h`가 `LWIP_NETCONN_FULLDUPLEX`를 켜지 않으므로(opt.h 기본값 0) 하나의 netconn은 한 번에 한 스레드만 쓸 수 있다. 다른 태스크에서 같은 fd에 `lwip_send`를 하면 lwIP가 연결을 abort한다. 자세한 건 §10 트러블슈팅 참고.

> CubeMX에서 **LWIP 미들웨어(Static IP, LAN8742)** 를 활성화한 뒤 Generate Code → `net_link` + 프로토콜 리팩터를 얹었다. FW_BOOT는 다운로드를 하지 않으므로 **LWIP/ETH/USB 모두 꺼져 있어야 한다**(§9 함정 목록 3번).

### 플래시 특성 — 반드시 알아야 할 두 가지

**① 듀얼 뱅크와 RWW(Read-While-Write)**

F429ZI 2MB는 듀얼 뱅크다: **Bank1 = 0x0800_0000~0x080F_FFFF, Bank2 = 0x0810_0000~0x081F_FFFF.**
한 뱅크를 지우는 동안 다른 뱅크에서 **코드 실행(읽기)** 이 가능하다.

| 대상 | 뱅크 | 앱 실행(0x0802_0000, Bank1)에 미치는 영향 |
|---|---|---|
| **Staging** (0x0812_0000) | Bank2 | 명령어 인출 계속 가능 |
| **Metadata** (0x0801_0000) | Bank1 | **CPU 전면 스톨** — 어떤 태스크도 못 돔 |
| **Factory** (0x080A_0000~0x0811_FFFF) | **양쪽에 걸침** | Bank1 부분 소거 시 **전면 스톨** |

> ⚠️ **RWW는 "CPU를 놓아준다"는 뜻이 아니다.** HAL(`HAL_FLASHEx_Erase`)은 BSY 플래그를 **busy-wait 폴링**하므로 호출한 태스크가 CPU를 계속 쥔다. 즉 RWW가 되더라도 **우선순위가 낮은 태스크는 그동안 전혀 스케줄되지 않는다.**
> → 대책: 섹터 사이마다 `osDelay(1)`로 양보하고, 감시 기한을 '섹터 1개 소거 시간'(128KB 기준 최악 4초)보다 크게 잡는다.
> → Bank1을 지우는 구간(Metadata / Factory)은 아예 태스크가 못 도므로 **체크인이 아니라 직접 `IWDG->KR` 갱신**이 필요하다.

**② 섹터 크기가 불균일**
16KB×4 / 64KB×1 / 128KB×7 이 뱅크마다 반복된다. Staging은 전부 128KB라 단순 덧셈으로 전진해도 되지만, **Factory는 128K×3 + 16K×4 + 64K×1** 이 섞여 있어 `FlashIf_NextSectorAddr()`로 경계를 따라가야 한다.

### 알려진 한계 / 다음 보강 후보
- ~~단일 Staging erase가 워치독 타임아웃을 넘는 문제~~ → **해소됨**: 섹터 단위로 쪼개 지우며 매 섹터 체크인 + 양보.
- 롤백 목적지는 **Factory** — "직전 버전"이 아니라 "그 슬롯에 넣어둔 이미지"로 복귀.
- OTA 실패 시 해당 태스크는 `Wdg_Panic()` 후 정지한다(워치독 리셋으로 회복). 세션만 정리하고 계속 서비스하는 방식은 미구현.
- `FwInfo` 버전 표식은 슬롯 전체를 스캔해 찾는다(고정 오프셋 아님). 512KB 스캔이라 1초 주기 로그에서는 무시할 만하지만, 더 자주 호출하려면 캐싱이 필요하다. **`FWINFO??` 폴링도 같은 1초 주기**이므로 현재는 문제없다 — 주기를 줄이려면 캐싱을 먼저 넣을 것.
- **미검증 항목**: `ledTask` 스택 1024B 상향 후 재측정(여러 부팅에서 볼 것).
- `[RES]` 자원 로그는 **UART에만** 나간다. TCP는 `FWINFO??`로 상태 한 줄만 응답한다(진단 콘솔은 UART가 맡는 구조). Ethernet만 연결한 채 자원을 봐야 한다면 `FWRES???` 같은 조회 명령을 추가하면 된다.
- ~~**Ethernet 전송 후 이따금 ping 불통**~~ → **원인 확정·수정 완료**(§10 트러블슈팅). ST 생성 `ethernetif.c`의 RX 데드락이었다. 수정 후 반복 전송에서 재현되지 않음.
  - 다만 **확률적 레이스**였으므로 "안 나온다"가 곧 증명은 아니다. 재발하면 §9 함정 목록 5번(재생성으로 되돌아갔는지)부터 확인할 것.

---

## 9. ⚠️ CubeMX 재생성 시 반드시 되돌릴 것

`.ioc`를 열어 Generate Code를 하면 아래가 **조용히 기본값으로 되돌아간다.** 되돌아가도 빌드는 통과하고, 증상은 한참 뒤에 엉뚱한 모습으로 나타난다. 재생성 직후 이 표를 처음부터 훑을 것.

| # | 대상 | 되돌아가면 생기는 일 | 확인/조치 |
|---|---|---|---|
| 1 | **FW_BOOT의 `MX_IWDG_Init()` 자동 호출** | 부트로더가 시작하자마자 IWDG를 켠다 → Factory 캡처/적용/롤백 같은 긴 Flash 작업 중 자기 리셋 → **부팅 루프** | `main()`의 호출을 다시 주석 처리. IWDG는 `BL_StartWatchdog()`에서 TRIAL 점프 직전에만 켠다 |
| 2 | **링커 `.ld`의 FLASH ORIGIN/LENGTH** | 부트로더/앱이 서로의 영역을 침범 | FW_BOOT `0x0800_0000`/64K, FW_APP `0x0802_0000`/512K |
| 3 | **FW_BOOT `.ioc`의 LWIP/ETH/USB** | 부트로더가 64KB를 넘겨 `region FLASH overflowed` (약 76KB) | 전부 해제. 부트로더는 `RCC/SYS/GPIO/IWDG/USART3`만 필요 (현재 12.95KB) |
| 4 | **`lwipopts.h`** | 튜닝값이 기본값으로 (특히 `MEM_SIZE`) | USER CODE 블록 밖의 수정은 매번 재확인 |
| 5 | **`ethernetif.c`의 RX 데드락 수정 3곳** | `portMAX_DELAY` 복귀 → **Ethernet 수신이 이따금 영구 정지**(링크는 Up인데 ARP 무응답, 리셋으로만 복구). 증상이 한참 뒤에 나타나 원인 추적이 어렵다 | ① `TIME_WAITING_FOR_INPUT`이 `pdMS_TO_TICKS(100)`인지 ② `ethernetif_input`이 세마포어 반환값과 무관하게 매번 수신 재시도 + `RxAllocStatus` 해제를 하는지 ③ `RxAllocStatus`가 `volatile`인지. 셋 다 USER CODE 블록 **밖**이다 (바로 위 `INTERFACE_THREAD_STACK_SIZE`는 안이라 유지된다) |

> **왜 이 목록이 따로 있나.** 위 항목들은 전부 "CubeMX가 관리하는 영역"에 있어 USER CODE 주석으로 보호되지 않는다. 1번은 이 프로젝트에서 **실제로 두 번** 당했다.

> `LWIP_NETCONN_FULLDUPLEX`를 켜서 해결하고 싶은 문제가 생기면, 그 수정이 4번에 해당한다는 점을 먼저 고려할 것 — 재생성 한 번에 버그가 조용히 부활한다.

---

## 10. 트러블슈팅 (겪고 해결한 것)

| 증상 | 원인 | 해결 |
|---|---|---|
| 점프 후 멈춤/크래시 | 앱이 부트로더 벡터테이블을 봄 | 앱에서 `SCB->VTOR = 0x08020000` |
| LED 켜진 채 멈춤 (HAL_Delay 무한대기) | 부트로더가 `__disable_irq()` 후 안 켜줌 | 점프 직전 `__enable_irq()` |
| 앱 초기화 중 멈춤 | 앱이 ETH 재초기화 실패 | 앱에서 불필요한 주변장치 초기화 제거 |
| **부팅 무한 루프** | CubeMX가 IWDG를 부트로더 시작 시 자동 켜서 긴 Flash 작업 중 자기 리셋 | `MX_IWDG_Init()` 자동 호출 주석 처리, `BL_StartWatchdog()`에서만 시작 |
| 정상 전송인데 `CRCERR` | MCU HW CRC와 PC 소프트 CRC 파라미터 불일치 | 동일 파라미터(poly/init/반사없음/최종XOR없음/LE word) |
| **FW_BOOT 빌드 시 `region FLASH overflowed`** | 부트로더 `.ioc`에 **LWIP/ETH/USB가 켜져 있어** 64KB 초과(약 76KB) | CubeMX에서 LWIP·ETH·USB_OTG_FS 해제 → **12.95KB**. 부트로더는 `RCC/SYS/GPIO/IWDG/USART3`만 있으면 된다 |
| OTA 중 `erase ACK 실패 (0x0D)` | ACK 자리에 읽힌 `0x0D`는 **panic 메시지의 첫 `\r`**. 실제 원인은 erase busy-wait가 낮은 우선순위 `ledTask`를 굶겨 체크인 기한 초과 | 섹터 사이 `osDelay(1)` 양보 + 감시 기한을 섹터 1개 소거 시간보다 크게 |
| **특정 오프셋에서 청크 CRC 실패, 재전송 5회 전부 실패** | DMA 콜백에서 HAL의 `Size` 인자를 위치로 오해. HT/TC는 **고정값**(256/512)이고 IDLE만 실제 위치인데, 두 IRQ 우선순위가 같아 순서가 뒤바뀌면 랩어라운드로 오인해 버퍼 한 바퀴를 잘못 주입 → 스트림이 영구히 어긋남 | `Size`를 쓰지 말고 `__HAL_DMA_GET_COUNTER()`로 **실제 위치를 직접 계산** |
| Factory 버전이 `????`로 표시 | 워치독 리셋으로 **App→Factory 자동 캡처가 중단**되어 매직만 복사되고 문자열은 쓰레기 | 버전 문자열이 출력 가능한 ASCII인지까지 검증. Factory는 **CRC 검증되는 `FWFACTRY` 경로**로 채우는 편이 확실 |
| 주기 로그가 UART OTA를 깨뜨림 | USART3는 OTA 채널이기도 하다 | 송신권 뮤텍스로 배타 처리 — OTA 세션 중에는 로그를 자동으로 건너뜀 |
| **펌웨어 전송 후 이따금 ping 불통** (링크 Up, **ARP조차 무응답**, 시리얼 로그는 정상, 보드 리셋으로만 복구) | ST 생성 `ethernetif.c`의 RX 데드락. ① 대량 수신으로 RX pbuf 풀 고갈 → `RxAllocStatus = RX_ALLOC_ERROR` ② `low_level_input()`은 `RX_ALLOC_OK`가 아니면 즉시 NULL → `do/while` 탈출 ③ `portMAX_DELAY`로 무한 대기 ④ 깨울 수 있는 건 ETH RX 인터럽트(디스크립터가 없어 안 옴)와 `pbuf_free_custom()`뿐인데, 플래그가 세워진 시점에 모든 pbuf가 이미 반납된 뒤면 후자도 불리지 않음 → **영구 정지** | `ethernetif.c` 3곳 수정: ① `TIME_WAITING_FOR_INPUT` → `pdMS_TO_TICKS(100)` ② `ethernetif_input`이 세마포어 반환값과 무관하게 매번 수신 재시도하고, 진입 시 `RX_ALLOC_ERROR`를 해제 ③ `RxAllocStatus`에 `volatile`. **타임아웃만 유한값으로 바꾸는 건 효과가 없다** — 타임아웃 시 `osOK`가 아니라 수신 시도를 건너뛰고, 들어가도 플래그가 ERROR인 채라 또 NULL이 나온다 |
| **Ethernet 접속 직후 `연결이 강제로 끊겼습니다`** (보드는 리셋 안 됨, `up=`이 계속 증가) | `ledTask`가 `netTask`와 **같은 소켓에** `lwip_send`를 했다. `LWIP_NETCONN_FULLDUPLEX=0`(기본값)에서 netconn은 `current_msg`/`op_completed` 세마포어가 하나뿐이라, `lwip_recv`로 블록 중인 소켓에 다른 스레드가 쓰면 상태가 깨져 lwIP가 연결을 abort한다 | **소켓은 `netTask`만 만진다.** 상태 표출은 보드가 먼저 보내는 대신 `FWINFO??` 요청-응답으로 전환 — 요청받은 태스크가 그 자리에서 답하므로 충돌 조건 자체가 없어진다. `lwipopts.h`에 `LWIP_NETCONN_FULLDUPLEX=1`을 켜는 방법도 있으나 **CubeMX 재생성 시 되돌아가** 버그가 조용히 부활한다 |

---

## 11. 참고 자료 (docs)

- `docs/Memory Map.png` — STM32F429 메모리 맵
- `docs/NUCLEO-F429ZI Datasheet.pdf` — 보드 데이터시트
- `docs/STM32F427ZI Datasheet.pdf` — MCU 데이터시트 (F427/F429 공용, Flash 프로그래밍 Table 48 등)

## 개발 환경
- **보드**: NUCLEO-F429ZI (STM32F429ZI, Flash 2MB / SRAM 256KB[192KB+64KB CCM])
- **IDE**: STM32CubeIDE / **UI**: C# WinForms (.NET Framework, System.IO.Ports)
