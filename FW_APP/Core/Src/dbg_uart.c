/**
  ******************************************************************************
  * @file    dbg_uart.c
  * @brief   USART3(115200) 최소 디버그 출력 구현. 자세한 주의사항은 dbg_uart.h 참고.
  ******************************************************************************
  */
#include "dbg_uart.h"
#include "main.h"
#include <string.h>

/* USART3 핸들은 main.c가 소유한다. */
extern UART_HandleTypeDef huart3;

/* 폴링 송신 타임아웃(ms). HAL_GetTick()은 TIM6 기반이라
 * taskDISABLE_INTERRUPTS()(BASEPRI=5) 상태에서도 계속 진행되므로 훅 안에서도 안전하다. */
#define DBG_TX_TIMEOUT_MS   1000U

void Dbg_Puts(const char *s)
{
  if (s == NULL) return;

  size_t len = strlen(s);
  if (len == 0U) return;

  (void)HAL_UART_Transmit(&huart3, (uint8_t *)s, (uint16_t)len, DBG_TX_TIMEOUT_MS);
}

void Dbg_PutHex32(uint32_t v)
{
  static const char hex[] = "0123456789ABCDEF";
  char b[11];

  b[0] = '0';
  b[1] = 'x';
  for (int i = 0; i < 8; i++)
  {
    b[2 + i] = hex[(v >> (28 - 4 * i)) & 0xFU];
  }
  b[10] = '\0';

  Dbg_Puts(b);
}

void Dbg_PutU32(uint32_t v)
{
  char b[11];
  int  i = 0;

  if (v == 0U)
  {
    b[i++] = '0';
  }
  else
  {
    char tmp[10];
    int  n = 0;
    while ((v > 0U) && (n < 10))
    {
      tmp[n++] = (char)('0' + (v % 10U));
      v /= 10U;
    }
    while (n > 0)
    {
      b[i++] = tmp[--n];
    }
  }
  b[i] = '\0';

  Dbg_Puts(b);
}
