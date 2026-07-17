/**
  ******************************************************************************
  * @file    net_link.c
  * @brief   lwIP RAW TCP(NO_SYS) 기반 블로킹 전송 링크 + RX 링버퍼.
  *
  *  구조:
  *   - TCP 서버로 NETLINK_PORT를 리슨, 한 번에 한 클라이언트만 수용.
  *   - recv 콜백이 도착 payload를 RX 링버퍼에 적재(전량 안 들어가면 ERR_MEM로 백프레셔).
  *   - NetLink_Recv/Send는 링버퍼/tcp_write 위에서 "정확 길이" 블로킹 I/O를 제공하며,
  *     대기 중 lwIP를 계속 펌핑하고 워치독을 갱신한다(플래시/전송 중 리셋 방지).
  *
  *  주의(NO_SYS): recv 콜백과 소비(Recv/ReadAvailable)는 모두 MX_LWIP_Process()
  *  컨텍스트(메인 루프 단일 스레드)에서만 실행된다. 인터럽트에서 링버퍼를 만지지
  *  않으므로 별도 락/volatile이 필요 없다.
  ******************************************************************************
  */
#include "net_link.h"
#include "main.h"          /* HAL, IWDG, HAL_GetTick */
#include "lwip.h"          /* MX_LWIP_Process */
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

/* ===== RX 링버퍼 (크기는 반드시 2의 거듭제곱: 인덱스 마스킹 사용) ===== */
#define RX_RING_SIZE   4096U          /* TCP 윈도우(기본 ~2KB) 이상 → 한 윈도우 전량 흡수 */
#if (RX_RING_SIZE & (RX_RING_SIZE - 1U)) != 0U
#error "RX_RING_SIZE must be a power of two"
#endif

static uint8_t  s_rx[RX_RING_SIZE];
static uint32_t s_head;                /* 누적 write 인덱스 (recv 콜백) */
static uint32_t s_tail;                /* 누적 read 인덱스 (소비) */

static struct tcp_pcb *s_client;       /* 현재 연결 pcb */
static struct tcp_pcb *s_listen;       /* 리슨 pcb */
static bool            s_connected;

/* --- 링버퍼 헬퍼 (32bit 언랩 뺄셈: count는 항상 <= RX_RING_SIZE) --- */
static uint32_t ring_count(void) { return s_head - s_tail; }
static uint32_t ring_free(void)  { return RX_RING_SIZE - ring_count(); }
static void     ring_reset(void) { s_head = 0U; s_tail = 0U; }

static void ring_write(const uint8_t *d, uint32_t n)
{
  for (uint32_t i = 0U; i < n; i++)
  {
    s_rx[s_head & (RX_RING_SIZE - 1U)] = d[i];
    s_head++;
  }
}

static uint32_t ring_read(uint8_t *d, uint32_t max)
{
  uint32_t n = ring_count();
  if (n > max) n = max;
  for (uint32_t i = 0U; i < n; i++)
  {
    d[i] = s_rx[s_tail & (RX_RING_SIZE - 1U)];
    s_tail++;
  }
  return n;
}

/* --- 워치독 갱신(펫). IWDG 미가동 시엔 무해한 no-op. --- */
static inline void netlink_pet(void) { IWDG->KR = 0x0000AAAAU; }

static void netlink_reset_conn(void)
{
  s_client    = NULL;
  s_connected = false;
  ring_reset();
}

/* ===== lwIP RAW 콜백 ===== */

static err_t netlink_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  (void)arg;

  if (err != ERR_OK)
  {
    if (p != NULL) pbuf_free(p);
    return err;
  }

  if (p == NULL)                 /* 원격이 연결을 정상 종료 */
  {
    tcp_recv(tpcb, NULL);
    tcp_err(tpcb, NULL);
    tcp_close(tpcb);
    netlink_reset_conn();
    return ERR_OK;
  }

  /* 링버퍼에 '전량' 들어갈 때만 수용. 부족하면 ERR_MEM → lwIP가 pbuf를 보관했다
   * 나중에 재전달(백프레셔). RX_RING_SIZE >= TCP 윈도우이므로 소비만 되면 곧 수용됨. */
  if (ring_free() < p->tot_len)
  {
    return ERR_MEM;
  }

  for (struct pbuf *q = p; q != NULL; q = q->next)
  {
    ring_write((const uint8_t *)q->payload, q->len);
  }
  tcp_recved(tpcb, p->tot_len);      /* 수신 윈도우 전진 */
  pbuf_free(p);
  return ERR_OK;
}

static void netlink_err_cb(void *arg, err_t err)
{
  (void)arg;
  (void)err;
  /* 이 콜백 시점에 pcb는 이미 lwIP가 해제함 → close/abort 호출 금지. 상태만 정리. */
  netlink_reset_conn();
}

static err_t netlink_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  (void)arg;
  if (err != ERR_OK || newpcb == NULL) return ERR_VAL;

  if (s_connected)                   /* 이미 세션 있음 → 새 연결 거부 */
  {
    return ERR_MEM;
  }

  s_client    = newpcb;
  s_connected = true;
  ring_reset();

  tcp_nagle_disable(newpcb);         /* 1바이트 ACK가 지연되지 않도록(저지연) */
  tcp_recv(newpcb, netlink_recv_cb);
  tcp_err(newpcb,  netlink_err_cb);
  return ERR_OK;
}

/* ===== 공개 API ===== */

void NetLink_Init(void)
{
  netlink_reset_conn();

  struct tcp_pcb *pcb = tcp_new();
  if (pcb == NULL) return;

  if (tcp_bind(pcb, IP_ADDR_ANY, NETLINK_PORT) != ERR_OK)
  {
    tcp_close(pcb);
    return;
  }

  s_listen = tcp_listen(pcb);        /* 성공 시 작은 리슨 pcb 반환 + 기존 pcb 해제 */
  if (s_listen == NULL)
  {
    tcp_close(pcb);
    return;
  }
  tcp_accept(s_listen, netlink_accept_cb);
}

void NetLink_Poll(void)
{
  MX_LWIP_Process();
}

bool     NetLink_Connected(void)                          { return s_connected; }
uint32_t NetLink_Available(void)                          { return ring_count(); }
uint32_t NetLink_ReadAvailable(uint8_t *buf, uint32_t max){ return ring_read(buf, max); }

bool NetLink_Recv(uint8_t *buf, uint32_t len, uint32_t timeoutMs)
{
  uint32_t got   = 0U;
  uint32_t start = HAL_GetTick();

  while (got < len)
  {
    got += ring_read(buf + got, len - got);
    if (got >= len) break;

    if (!s_connected && (ring_count() == 0U))     /* 끊겼고 버퍼도 비었음 */
    {
      return false;
    }

    netlink_pet();
    NetLink_Poll();                               /* 새 패킷 수신 시도 */

    if ((timeoutMs != 0U) && ((HAL_GetTick() - start) >= timeoutMs))
    {
      return false;
    }
  }
  return true;
}

bool NetLink_Send(const uint8_t *buf, uint32_t len)
{
  const uint32_t TX_TIMEOUT_MS = 5000U;
  uint32_t off   = 0U;
  uint32_t start = HAL_GetTick();

  while (off < len)
  {
    if (!s_connected || (s_client == NULL)) return false;

    uint32_t sndbuf = tcp_sndbuf(s_client);
    if (sndbuf == 0U)                              /* 송신 버퍼 없음 → ACK 대기 */
    {
      netlink_pet();
      NetLink_Poll();
      if ((HAL_GetTick() - start) >= TX_TIMEOUT_MS) return false;
      continue;
    }

    uint32_t chunk = (len - off < sndbuf) ? (len - off) : sndbuf;
    err_t e = tcp_write(s_client, buf + off, (u16_t)chunk, TCP_WRITE_FLAG_COPY);
    if (e == ERR_MEM)                              /* 힙 부족 → 잠시 펌핑 후 재시도 */
    {
      netlink_pet();
      NetLink_Poll();
      if ((HAL_GetTick() - start) >= TX_TIMEOUT_MS) return false;
      continue;
    }
    if (e != ERR_OK) return false;

    off += chunk;
    tcp_output(s_client);                          /* 즉시 flush */
    start = HAL_GetTick();                          /* 진행 있었으니 타임아웃 리셋 */
  }
  return true;
}

void NetLink_Close(void)
{
  if (s_client != NULL)
  {
    tcp_recv(s_client, NULL);
    tcp_err(s_client, NULL);
    if (tcp_close(s_client) != ERR_OK)
    {
      tcp_abort(s_client);
    }
  }
  netlink_reset_conn();
}
