# STM32F429 펌웨어 업데이트 시스템 (Custom Bootloader / IAP)

NUCLEO-F429ZI 보드를 대상으로, **ST-Link 없이** 디버깅용 **UART** 또는 **Ethernet** 케이블과 **C# UI(UI_Monitor)** 만으로 펌웨어를 교체할 수 있게 하는 프로젝트입니다. STM32 내부에 상주하는 **커스텀 부트로더(IAP, In-Application Programming)** 를 직접 구현합니다.

> **목표 시나리오**: 제품 출하 후 현장에서, ST-Link를 꺼내지 않고 케이블만 연결해 C# UI로 `.hex` 펌웨어를 내려받아 업데이트한다.

---

## 1. 전체 아키텍처

```
[PC: C# UI Monitor]  --(UART or Ethernet)-->  [STM32 Bootloader]  --write-->  [STM32 Flash: App 영역]
      .hex 파싱               프로토콜               Flash 쓰기            점프 후 실행
```

STM32 Flash를 **부트로더 영역**과 **애플리케이션 영역**으로 분리합니다.

- **Bootloader (FW_BOOT)**: 리셋 시 항상 먼저 실행. 업데이트 요청이 없으면 앱으로 점프하고, 있으면 UART/Ethernet으로 새 펌웨어를 받아 Flash에 굽는다.
- **Application (FW_APP)**: 실제 제품 동작 펌웨어. 현재는 LED 데모.
- **UI_Monitor**: C# WinForms 프로그램. RS-232 / Ethernet 전송 모드 지원.

---

## 2. 메모리 구조

### 2.1 STM32F429ZI 전체 메모리 맵

![Memory Map](docs/Memory%20Map.png)

복잡해 보이지만 실제로 쓰는 영역은 3개뿐입니다.

| 주소 | 이름 | 용도 |
|---|---|---|
| `0x0800_0000 ~ 0x081F_FFFF` | **Flash 2MB** | 코드 저장 (부트로더 + 앱) |
| `0x2000_0000 ~ 0x2002_FFFF` | **SRAM 192KB** | 실행 중 변수/스택 |
| `0x0000_0000 ~` | Aliased 영역 | BOOT0 핀에 따라 Flash가 여기 매핑 → 리셋 시 실행 |

> STM32(Cortex-M)는 코드를 RAM에 올리지 않고 **Flash에서 직접 실행(XIP)** 한다. RAM에는 변수/스택만 올라간다.

### 2.2 Flash 섹터 맵과 영역 분할

앱 영역은 반드시 **섹터 경계**에서 시작해야 한다(앱을 지울 때 부트로더가 함께 지워지지 않도록).

| 섹터 | 주소 | 크기 | 용도 |
|---|---|---|---|
| Sector 0~3 | `0x0800_0000` | 16KB × 4 = **64KB** | **Bootloader (FW_BOOT)** |
| Sector 4 | `0x0801_0000` | 64KB | **Application (FW_APP)** 시작 |
| Sector 5~11 | `0x0802_0000` ~ | 128KB × 7 | Application |
| (Bank2) 12~23 | `0x0810_0000` ~ | ~2MB까지 | Application |

```
┌─────────────────────────────────────────────┐
│ Bootloader  0x0800_0000 ~ 0x0800_FFFF (64KB) │  ← FW_BOOT (섹터 0~3)
├─────────────────────────────────────────────┤
│ Application 0x0801_0000 ~ 0x081F_FFFF (1984KB)│  ← FW_APP  (섹터 4~)
└─────────────────────────────────────────────┘
```

---

## 3. 프로젝트 구조

```
Prj_FW-Update_STM32F429/
├── FW_BOOT/          # 부트로더 프로젝트 (@ 0x0800_0000, 64KB)
│   ├── Core/Src/main.c
│   └── STM32F429ZITX_FLASH.ld
├── FW_APP/           # 애플리케이션 프로젝트 (@ 0x0801_0000)
│   ├── Core/Src/main.c
│   └── STM32F429ZITX_FLASH.ld
├── UI_Monitor/       # C# UI (RS-232 / Ethernet 뷰어)
├── docs/             # 데이터시트, 메모리맵 이미지
└── README.md
```

> **부트로더와 앱은 각자 다른 주소로 링크된 별개의 바이너리**여야 한다. 하나의 바이너리로 합치면, 앱 업데이트 중 부트로더까지 지워져 전원이 끊기면 **벽돌(brick)** 이 된다.

---

## 4. 링커 스크립트 수정

두 프로젝트의 `STM32F429ZITX_FLASH.ld` 에서 `MEMORY` 블록의 `FLASH` 항목만 수정했다.

### 4.1 FW_BOOT — Flash를 앞쪽 64KB로 제한

```ld
/* 기존 */
FLASH (rx) : ORIGIN = 0x8000000, LENGTH = 2048K
/* 수정 후 */
FLASH (rx) : ORIGIN = 0x8000000, LENGTH = 64K     /* 부트로더 = 섹터 0~3 */
```

### 4.2 FW_APP — Flash 시작 주소를 0x0801_0000으로 이동

```ld
/* 기존 */
FLASH (rx) : ORIGIN = 0x8000000, LENGTH = 2048K
/* 수정 후 */
FLASH (rx) : ORIGIN = 0x8010000, LENGTH = 1984K   /* 앱 = 섹터 4~, 2048K-64K */
```

> ORIGIN을 바꾸면 앱의 벡터 테이블도 `0x0801_0000`으로 이동한다. 따라서 앱 실행 시 **`SCB->VTOR` 재배치가 필수**다(아래 5.1).

---

## 5. 핵심 소스 코드

### 5.1 FW_APP — 벡터 테이블 재배치 (`main.c`)

앱은 `0x0801_0000`에 있으므로, 인터럽트 벡터 테이블 위치를 CPU에 알려야 한다. 이 줄이 없으면 인터럽트가 발생할 때 CPU가 부트로더(`0x0800_0000`)의 핸들러로 점프해 오동작한다.

```c
/* USER CODE BEGIN 1 */
SCB->VTOR = 0x08010000U;   /* ★필수: 앱의 벡터 테이블 위치 지정 */
/* USER CODE END 1 */
```

또한 앱은 LED만 필요하므로, 복사되어 있던 불필요한 주변장치 초기화를 막아 초기화 단계에서 멈추지 않게 했다.

```c
MX_GPIO_Init();
// MX_ETH_Init();          // 부트로더 몫이라 앱에서는 비활성화
// MX_USART3_UART_Init();
// MX_USB_OTG_FS_PCD_Init();
```

### 5.2 FW_BOOT — 앱 주소 정의 (`main.c`)

```c
/* USER CODE BEGIN PD */
#define APP_ADDRESS   0x08010000U     /* FW_APP 링커 ORIGIN과 일치 */
#define RAM_START     0x20000000U
#define RAM_END       0x20030000U     /* 0x20000000 + 192KB */
/* USER CODE END PD */
```

### 5.3 FW_BOOT — 앱으로 점프 (`main.c`)

리셋 시 하드웨어가 하는 동작(SP 로드 → Reset_Handler 점프)을 소프트웨어로 재현한다. 유효한 앱이 없으면 리턴하여 부트로더에 머문다.

```c
static void BL_JumpToApplication(void)
{
  /* 벡터 테이블 앞 2워드 = [초기 스택 포인터][Reset_Handler 주소] */
  uint32_t appStack = *(volatile uint32_t *)(APP_ADDRESS);
  uint32_t appEntry = *(volatile uint32_t *)(APP_ADDRESS + 4U);

  /* ① 유효성 검사: 앱이 없으면 Flash가 지워진 상태(0xFFFFFFFF)라
   *    초기 스택 포인터가 RAM 범위를 벗어난다 */
  if (appStack < RAM_START || appStack > RAM_END)
  {
    return;                 /* 유효 앱 없음 → 부트로더에 머무름 */
  }

  /* ② 인계 전 정리: 켜둔 주변장치/인터럽트/SysTick을 끈다 */
  HAL_RCC_DeInit();
  HAL_DeInit();
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;
  __disable_irq();

  /* ③ 앱의 벡터 테이블로 전환 */
  SCB->VTOR = APP_ADDRESS;

  /* ④ 스택 포인터 교체 */
  __set_MSP(appStack);

  /* ⑤ 인터럽트 재활성화 (★정상 리셋 상태 재현)
   *    안 켜면 앱의 SysTick 인터럽트가 안 울려 HAL_Delay()가 영원히 멈춘다 */
  __enable_irq();

  /* ⑥ 앱의 Reset_Handler로 점프 (돌아오지 않음) */
  ((void (*)(void))appEntry)();
}
```

### 5.4 FW_BOOT — 점프 호출과 부트로더 모드 표시 (`main.c`)

```c
/* USER CODE BEGIN 2 */
/* [진단용] 부트로더 실행 확인 — 점프 전에 LD3를 3회 깜빡 */
for (int i = 0; i < 6; i++) { HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin); HAL_Delay(100); }

/* 유효한 앱이 있으면 점프, 없으면 리턴하여 아래 while로 떨어짐 */
BL_JumpToApplication();
/* USER CODE END 2 */

while (1)
{
  /* USER CODE BEGIN 3 */
  /* 여기 도달 = 유효 앱 없음 → LD3(빨강)를 천천히 깜빡여 '부트로더 모드' 표시 */
  HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
  HAL_Delay(500);
  /* USER CODE END 3 */
}
```

---

## 6. 현재 상태 (진행 완료 마일스톤)

✅ **마일스톤 0 — 부트로더/앱 분리 부팅 완성**

- [x] Flash를 부트로더(64KB) / 앱 영역으로 분리 (링커)
- [x] 부트로더 → 앱 점프 (유효성 검사 + 정리 + VTOR + MSP + IRQ 재활성화)
- [x] 앱 벡터 테이블 재배치(`SCB->VTOR`)
- [x] 실기 검증 완료 (아래 LED 동작으로 확인)

### LED로 보는 동작 구분

| LED 동작 | 의미 |
|---|---|
| 🔴 **적색 3회 빠른 깜빡** | 부트로더 실행됨 (점프 시도 직전) |
| 🔴 적색 **계속 천천히 깜빡** | 유효 앱 없음 → **부트로더 모드** 유지 |
| 🟢🔵🔴 **LD1↔LD2↔LD3 흐르는 패턴** | 앱으로 점프 성공 → **앱 실행 중** |

---

## 7. 빌드 & 테스트 절차 (STM32CubeIDE)

1. **FW_BOOT** 빌드 → 플래시 (`0x0800_0000`)
   - 앱이 아직 없으면 적색 LED가 계속 깜빡 = 부트로더 모드
2. **FW_APP** 빌드 → 플래시 (`0x0801_0000`)
3. 보드의 **검은 RESET 버튼**을 누른다
   - `적색 3회 깜빡 → LED 흐르는 패턴` 이 보이면 점프 성공

> ⚠️ CubeIDE에서 앱을 "Run/Debug"로 직접 띄우면 디버거가 부트로더를 건너뛴다. **실제 점프 검증은 둘 다 플래시 후 물리 RESET 버튼**으로 한다.
> ⚠️ ST-Link 플래시 옵션이 "Full chip erase"면 서로를 지운다. 기본값인 **섹터 단위 erase**를 사용한다.

---

## 8. 트러블슈팅 — "부트로더는 되는데 앱이 이상해요"의 3대 함정

실제로 이 프로젝트에서 겪고 해결한 문제들.

| 증상 | 원인 | 해결 |
|---|---|---|
| 앱으로 점프 후 **멈춤/크래시** | 앱이 벡터 테이블을 부트로더 것으로 봄 | 앱에서 `SCB->VTOR = 0x08010000` |
| 특정 LED가 **켜진 채 멈춤** (HAL_Delay 무한대기) | 부트로더가 `__disable_irq()` 후 안 켜줌 → SysTick 인터럽트 안 울림 | 점프 직전 `__enable_irq()` |
| 앱 **초기화 단계에서 멈춤** | 앱이 ETH 등 주변장치를 재초기화하다 실패 | 앱에서 불필요한 주변장치 초기화 제거 |

---

## 9. 앞으로의 로드맵

```
✅ 1. 부트로더/앱 메모리 분리 + 점프          ← 현재 완료
⬜ 2. Flash erase/write 함수
⬜ 3. UART 프로토콜 (패킷/CRC/ACK) + C# 송신 + .hex 파싱
⬜ 4. 업데이트 진입 트리거 (앱→플래그→리셋→부트로더 잔류)
⬜ 5. Ethernet(lwIP) 방식 추가
⬜ 6. 안정성/롤백 (Factory 슬롯 + 워치독 trial/confirm)
```

### (예정) 자동 롤백 개념
- **Factory 슬롯**: 출하 시 굽는 초기 펌웨어. 업데이트로 절대 안 건드림 → 항상 안전한 복귀처.
- **CRC 검증**: 전송 중 깨진 펌웨어 차단.
- **워치독 + trial/confirm**: 새 펌웨어가 부팅 후 스스로 "정상" 도장을 못 찍으면 리셋되어 Factory로 롤백.

---

## 10. 참고 자료 (docs)

- `docs/Memory Map.png` — STM32F429 메모리 맵
- `docs/NUCLEO-F429ZI Datasheet.pdf` — 보드 데이터시트
- `docs/STM32F427ZI Datasheet.pdf` — MCU 데이터시트 (F427/F429 공용)

---

## 개발 환경

- **보드**: NUCLEO-F429ZI (STM32F429ZI, Flash 2MB / SRAM 256KB[192KB+64KB CCM])
- **IDE**: STM32CubeIDE
- **UI**: C# WinForms (.NET Framework)
