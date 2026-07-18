/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "dbg_uart.h"
#include "wdg_monitor.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 4 */

/* 마지막 장애 정보 — 디버거로 확인용 */
volatile const char *g_faultTaskName = NULL;
volatile uint32_t    g_faultHeapFree = 0;

/**
  * @brief  스택 오버플로 감지 훅 (configCHECK_FOR_STACK_OVERFLOW == 2)
  * @note   훅이 비어 있으면 오버플로가 '조용히' 지나가 나중에 엉뚱한 하드폴트로 나타난다.
  *         Wdg_Panic()으로 pet을 영구 중단시켜 IWDG 리셋 → Factory 롤백까지 이어지게 한다.
  *         (여기서 무한루프만 돌면 다른 태스크가 대신 pet해 롤백이 무력화될 수 있다)
  */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
  (void)xTask;
  g_faultTaskName = (const char *)pcTaskName;

  Dbg_Puts("\r\n*** FreeRTOS STACK OVERFLOW in task: ");
  Dbg_Puts((const char *)pcTaskName);
  Dbg_Puts("\r\n");

  Wdg_Panic("stack overflow");

  /* 인터럽트를 끄지 않는다: IWDG가 물어 리셋시켜야 롤백으로 이어진다. */
  for (;;) { }
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
/**
  * @brief  FreeRTOS 힙 부족 훅 (configUSE_MALLOC_FAILED_HOOK == 1)
  * @note   태스크/큐/세마포어 생성이 configTOTAL_HEAP_SIZE 부족으로 실패하면 호출된다.
  */
void vApplicationMallocFailedHook(void)
{
  g_faultHeapFree = (uint32_t)xPortGetFreeHeapSize();

  Dbg_Puts("\r\n*** FreeRTOS MALLOC FAILED, free heap = ");
  Dbg_PutU32(g_faultHeapFree);
  Dbg_Puts("\r\n");

  Wdg_Panic("malloc failed");

  for (;;) { }
}
/* USER CODE END 5 */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

