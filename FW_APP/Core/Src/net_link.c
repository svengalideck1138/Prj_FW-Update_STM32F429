/**
  ******************************************************************************
  * @file    net_link.c
  * @brief   TCP 링크 (BSD 소켓). 설계 배경은 net_link.h 참고.
  *
  *  주의: lwipopts에서 LWIP_PROVIDE_ERRNO가 켜져 있어 lwIP가 errno와 E* 상수를 직접
  *        제공한다. 따라서 <errno.h>를 include하면 안 된다(중복 정의 충돌).
  ******************************************************************************
  */
#include "net_link.h"
#include "main.h"

#include "lwip/opt.h"
#include "lwip/sockets.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/* 송신이 이 시간 안에 진행되지 않으면 실패로 본다(상대가 죽은 경우 무한 대기 방지). */
#define NETLINK_TX_TIMEOUT_MS   5000U

/* 무한 대기(timeoutMs==0)일 때 recv를 끊어 기다리는 단위.
 * 한 번에 영원히 블록하지 않고 주기적으로 깨어나 연결 상태를 재확인하기 위함. */
#define NETLINK_WAIT_SLICE_MS   1000U

static int s_listenFd = -1;
static int s_clientFd = -1;

/* lwIP는 LWIP_SO_SNDRCVTIMEO_NONSTANDARD=0(기본)에서 struct timeval을 받는다. */
static void netlink_set_timeout(int fd, int optname, uint32_t ms)
{
  struct timeval tv;
  tv.tv_sec  = (long)(ms / 1000U);
  tv.tv_usec = (long)((ms % 1000U) * 1000U);
  (void)lwip_setsockopt(fd, SOL_SOCKET, optname, &tv, sizeof(tv));
}

/* recv/send가 '타임아웃'으로 실패했는지(=재시도 가능) 판별 */
static bool netlink_would_block(void)
{
  return (errno == EWOULDBLOCK) || (errno == EAGAIN);
}

void NetLink_Init(void)
{
  s_listenFd = -1;
  s_clientFd = -1;
}

bool NetLink_ServerOpen(void)
{
  struct sockaddr_in addr;

  s_listenFd = lwip_socket(AF_INET, SOCK_STREAM, 0);
  if (s_listenFd < 0) return false;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_port        = lwip_htons((uint16_t)NETLINK_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (lwip_bind(s_listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
  {
    (void)lwip_close(s_listenFd);
    s_listenFd = -1;
    return false;
  }

  /* 백로그 1: 한 번에 한 세션만 다룬다(OTA는 동시에 두 개가 돌면 안 된다). */
  if (lwip_listen(s_listenFd, 1) < 0)
  {
    (void)lwip_close(s_listenFd);
    s_listenFd = -1;
    return false;
  }

  return true;
}

bool NetLink_Accept(uint32_t timeoutMs)
{
  if (s_listenFd < 0) return false;
  if (s_clientFd >= 0) return true;      /* 이미 세션 있음 */

  /* accept도 SO_RCVTIMEO를 따른다 → 주기적으로 빠져나와 워치독 체크인을 할 수 있다. */
  netlink_set_timeout(s_listenFd, SO_RCVTIMEO, timeoutMs);

  struct sockaddr_in cli;
  socklen_t          cliLen = sizeof(cli);

  int fd = lwip_accept(s_listenFd, (struct sockaddr *)&cli, &cliLen);
  if (fd < 0) return false;              /* 타임아웃이거나 오류 */

  /* Nagle 비활성 (자세한 이유는 헤더 주석 참고) */
  int one = 1;
  (void)lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

  /* 송신이 영원히 막히지 않도록 상한을 둔다 */
  netlink_set_timeout(fd, SO_SNDTIMEO, NETLINK_TX_TIMEOUT_MS);

  s_clientFd = fd;
  return true;
}

bool NetLink_Connected(void)
{
  return (s_clientFd >= 0);
}

void NetLink_CloseClient(void)
{
  if (s_clientFd >= 0)
  {
    (void)lwip_close(s_clientFd);
    s_clientFd = -1;
  }
}

int NetLink_RecvSome(uint8_t *buf, uint32_t maxLen, uint32_t timeoutMs)
{
  if ((s_clientFd < 0) || (buf == NULL) || (maxLen == 0U)) return -1;

  netlink_set_timeout(s_clientFd, SO_RCVTIMEO,
                      (timeoutMs == 0U) ? NETLINK_WAIT_SLICE_MS : timeoutMs);

  int r = lwip_recv(s_clientFd, buf, (size_t)maxLen, 0);
  if (r > 0) return r;

  if (r == 0)                                     /* 상대가 정상 종료 */
  {
    NetLink_CloseClient();
    return -1;
  }
  if (netlink_would_block()) return 0;            /* 단순 타임아웃 */

  NetLink_CloseClient();                          /* 실제 오류 */
  return -1;
}

bool NetLink_Recv(uint8_t *buf, uint32_t len, uint32_t timeoutMs)
{
  if ((s_clientFd < 0) || (buf == NULL)) return false;
  if (len == 0U) return true;

  uint32_t   got   = 0U;
  TickType_t start = xTaskGetTickCount();
  TickType_t total = pdMS_TO_TICKS(timeoutMs);

  while (got < len)
  {
    uint32_t sliceMs;

    if (timeoutMs == 0U)
    {
      sliceMs = NETLINK_WAIT_SLICE_MS;            /* 무한 대기: 잘게 끊어 계속 기다린다 */
    }
    else
    {
      /* SO_RCVTIMEO는 '호출당' 타임아웃이지만 이 함수의 계약은 '총 데드라인'이다.
       * 남은 시간을 매번 계산해 옵션을 줄여가며 맞춘다. */
      TickType_t elapsed = xTaskGetTickCount() - start;   /* 언랩 안전 */
      if (elapsed >= total) return false;
      sliceMs = (uint32_t)((total - elapsed) * portTICK_PERIOD_MS);
      if (sliceMs == 0U) sliceMs = 1U;
    }

    netlink_set_timeout(s_clientFd, SO_RCVTIMEO, sliceMs);

    int r = lwip_recv(s_clientFd, buf + got, (size_t)(len - got), 0);
    if (r > 0)
    {
      got += (uint32_t)r;
      continue;
    }

    /* 연결이 끝났거나 오류면 소켓을 닫아 NetLink_Connected()가 실제 상태를 반영하게 한다.
     * 닫지 않으면 호출자가 '연결됨'으로 오인해 실패한 recv를 무한 반복한다. */
    if (r == 0)
    {
      NetLink_CloseClient();                      /* 상대가 닫음 */
      return false;
    }
    if (!netlink_would_block())
    {
      NetLink_CloseClient();                      /* 실제 오류 */
      return false;
    }
    /* 타임아웃 → 루프 상단에서 데드라인을 다시 판정한다 */
  }

  return true;
}

bool NetLink_Send(const uint8_t *buf, uint32_t len)
{
  if ((s_clientFd < 0) || (buf == NULL)) return false;
  if (len == 0U) return true;

  uint32_t   off   = 0U;
  TickType_t start = xTaskGetTickCount();

  while (off < len)
  {
    int r = lwip_send(s_clientFd, buf + off, (size_t)(len - off), 0);
    if (r > 0)
    {
      off  += (uint32_t)r;
      start = xTaskGetTickCount();                /* 진행이 있었으니 타임아웃 리셋 */
      continue;
    }
    if ((r < 0) && netlink_would_block())
    {
      /* 송신 버퍼가 찼다가 SO_SNDTIMEO가 만료된 경우 — 총 상한 안에서 재시도 */
      if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(NETLINK_TX_TIMEOUT_MS)) return false;
      continue;
    }

    NetLink_CloseClient();     /* 상대가 닫혔거나 오류 — 상태를 실제와 맞춘다 */
    return false;
  }

  return true;
}
