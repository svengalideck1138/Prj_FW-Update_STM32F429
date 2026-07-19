/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : ethernetif.c
  * Description        : This file provides code for the configuration
  *                      of the ethernetif.c MiddleWare.
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
#include "main.h"
#include "lwip/opt.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "netif/etharp.h"
#include "lwip/ethip6.h"
#include "ethernetif.h"
#include "lan8742.h"
#include <string.h>
#include "cmsis_os.h"
#include "lwip/tcpip.h"

/* Within 'USER CODE' section, code will be kept by default at each generation */
/* USER CODE BEGIN 0 */
#include "dbg_uart.h"

/* [진단] PHY 링크 상태와 MAC에 실제로 적용한 속도를 시리얼로 찍는다.
 * "PHY와 MAC이 어긋났다"는 가설을 눈으로 확인하기 위한 것 —
 * PC의 Get-NetAdapter 값과 대조하면 양쪽이 일치하는지 바로 드러난다. */
static void EthDbg_Link(const char *where, int32_t state, uint32_t applied)
{
  Dbg_Puts("[ETH] ");
  Dbg_Puts(where);
  Dbg_Puts(" phy=");
  switch (state)
  {
    case LAN8742_STATUS_LINK_DOWN:             Dbg_Puts("LINK_DOWN");        break;
    case LAN8742_STATUS_100MBITS_FULLDUPLEX:   Dbg_Puts("100M-FULL");        break;
    case LAN8742_STATUS_100MBITS_HALFDUPLEX:   Dbg_Puts("100M-HALF");        break;
    case LAN8742_STATUS_10MBITS_FULLDUPLEX:    Dbg_Puts("10M-FULL");         break;
    case LAN8742_STATUS_10MBITS_HALFDUPLEX:    Dbg_Puts("10M-HALF");         break;
    case LAN8742_STATUS_AUTONEGO_NOTDONE:      Dbg_Puts("AUTONEG-NOTDONE");  break;
    default:                                   Dbg_Puts("err(");
                                               Dbg_PutU32((uint32_t)state);
                                               Dbg_Puts(")");                break;
  }
  if (applied) { Dbg_Puts("  -> MAC applied"); }
  else         { Dbg_Puts("  -> (no MAC change)"); }
  Dbg_Puts("\r\n");
}

/* 아래에서 정의되는 PHY 객체 (여기서 쓰기 위해 앞당겨 선언) */
extern lan8742_Object_t LAN8742;

/* 링크 스레드는 100ms 주기다. 오토네고는 보통 2~3초에 끝나므로 5초를 기다린 뒤
 * 재협상을 건다(정상 협상 중에 끼어들어 오히려 깨뜨리지 않도록 넉넉히 잡았다). */
#define ETH_RENEGO_AFTER_POLLS   50U   /* 50 × 100ms = 5초 */
#define ETH_RENEGO_MAX_TRIES      3U   /* 케이블이 빠진 경우 무한 재시도/로그 방지 */

/**
  * @brief  PHY를 '알려진 좋은 상태'로 되돌리고 오토네고를 다시 시작시킨다.
  *
  * @note   ★ 왜 필요한가 (실측 증상 기반).
  *         TCP로 OTA/롤백을 하면 재부팅 후 RJ45 링크 LED가 안 켜지는 일이 잦았다.
  *         UART로 같은 일을 하면 발생하지 않는다. 로그를 보면 실패 구간에서 PHY가
  *         `100M-HALF` / `10M-HALF`로 붙는다 — **half duplex는 오토네고가 실패해
  *         parallel detect로 폴백했다는 signature다**(parallel detect는 속도만 알 수
  *         있어 duplex를 항상 half로 둔다). 더 나쁘면 링크 자체가 안 맺힌다.
  *
  *         원인: 보드의 SB177로 PHY의 nRST가 MCU NRST에 직결되어 있어(UM1974 p.32)
  *         NVIC_SystemReset()이 PHY도 하드웨어 리셋한다. 그런데 TCP 경로에서는 소켓을
  *         닫지 않고 리셋하므로 상대 PC가 재전송을 계속 쏘고 있고, 그 와중에 리셋에서
  *         나온 PHY가 오토네고(FLP 버스트)를 시작하려다 협상이 깨진다.
  *
  *         그리고 이 펌웨어는 원래 **PHY 레지스터에 쓰기를 단 한 번도 하지 않았다.**
  *         (LAN8742_Init()은 주소 스캔만 하고 소프트 리셋을 하지 않는다 — 아래
  *          low_level_init의 옛 주석이 이 점을 잘못 적고 있었다.) 그래서 한 번 잘못
  *         협상되면 스스로 빠져나올 방법이 없었다.
  *
  * @note   POWER_DOWN / ISOLATE / LOOPBACK을 명시적으로 해제한다. 리셋 펄스가 짧아
  *         PHY가 MODE 스트랩을 잘못 래치하면 파워다운으로 기동할 수 있는데, 그 경우
  *         링크도 LED도 영영 안 살아난다. 여기서 걷어내면 소프트웨어만으로 회복된다.
  *
  * @note   LAN8742_StartAutoNego()를 쓰지 않는 이유: 그 함수는 AUTONEGO_EN만 세우고
  *         RESTART_AUTONEGO를 세우지 않아 '이미 실패한 협상'을 다시 시작시키지 못한다.
  */
static void EthPhy_Recover(const char *why)
{
  uint32_t bcr = 0U;

  if (LAN8742.IO.ReadReg(LAN8742.DevAddr, LAN8742_BCR, &bcr) < 0)
  {
    Dbg_Puts("[ETH] phy recover FAILED (MDIO read)\r\n");
    return;
  }

  bcr &= ~((uint32_t)(LAN8742_BCR_POWER_DOWN | LAN8742_BCR_ISOLATE | LAN8742_BCR_LOOPBACK));
  bcr |=  (uint32_t)(LAN8742_BCR_AUTONEGO_EN | LAN8742_BCR_RESTART_AUTONEGO);

  if (LAN8742.IO.WriteReg(LAN8742.DevAddr, LAN8742_BCR, bcr) < 0)
  {
    Dbg_Puts("[ETH] phy recover FAILED (MDIO write)\r\n");
    return;
  }

  Dbg_Puts("[ETH] phy autoneg restarted - ");
  Dbg_Puts(why);
  Dbg_Puts("\r\n");
}
/* USER CODE END 0 */

/* Private define ------------------------------------------------------------*/
/* The time to block waiting for input. */
/* [수정] ST 기본값은 portMAX_DELAY(무한 대기)다. 그러면 RX 풀 고갈로 막혔을 때
 * 아무도 깨워주지 않아 수신이 영구 정지한다(상세는 ethernetif_input 주석 참고).
 * 100ms마다 깨어나 재시도하도록 유한값으로 바꾼다 — 최악의 경우에도 100ms 안에 자가 복구.
 * 평상시에는 수신 인터럽트가 세마포어를 즉시 올리므로 이 타임아웃은 거의 쓰이지 않는다.
 * ⚠️ USER CODE 블록 밖이라 CubeMX 재생성 시 되돌아간다(README §9 함정 목록 5번). */
#define TIME_WAITING_FOR_INPUT ( pdMS_TO_TICKS(100) )
/* Time to block waiting for transmissions to finish */
#define ETHIF_TX_TIMEOUT (2000U)
/* USER CODE BEGIN OS_THREAD_STACK_SIZE_WITH_RTOS */
/* Stack size of the interface thread (EthIf).
 * [R1] ST 기본값 350바이트는 지나치게 빠듯하다. 이 스레드는 low_level_input →
 * HAL_ETH_ReadData → zero-copy 콜백 → netif->input(tcpip_input) 경로를 타므로
 * 350B에서는 스택 오버플로가 나기 쉽다. 1024B로 올린다.
 * (이 블록은 USER CODE라 CubeMX 재생성에도 유지된다) */
#define INTERFACE_THREAD_STACK_SIZE ( 1024 )
/* USER CODE END OS_THREAD_STACK_SIZE_WITH_RTOS */
/* Network interface name */
#define IFNAME0 's'
#define IFNAME1 't'

/* ETH Setting  */
#define ETH_DMA_TRANSMIT_TIMEOUT               ( 20U )
#define ETH_TX_BUFFER_MAX             ((ETH_TX_DESC_CNT) * 2U)

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/* Private variables ---------------------------------------------------------*/
/*
@Note: This interface is implemented to operate in zero-copy mode only:
        - Rx Buffers will be allocated from LwIP stack Rx memory pool,
          then passed to ETH HAL driver.
        - Tx Buffers will be allocated from LwIP stack memory heap,
          then passed to ETH HAL driver.

@Notes:
  1.a. ETH DMA Rx descriptors must be contiguous, the default count is 4,
       to customize it please redefine ETH_RX_DESC_CNT in ETH GUI (Rx Descriptor Length)
       so that updated value will be generated in stm32xxxx_hal_conf.h
  1.b. ETH DMA Tx descriptors must be contiguous, the default count is 4,
       to customize it please redefine ETH_TX_DESC_CNT in ETH GUI (Tx Descriptor Length)
       so that updated value will be generated in stm32xxxx_hal_conf.h

  2.a. Rx Buffers number must be between ETH_RX_DESC_CNT and 2*ETH_RX_DESC_CNT
  2.b. Rx Buffers must have the same size: ETH_RX_BUF_SIZE, this value must
       passed to ETH DMA in the init field (heth.Init.RxBuffLen)
  2.c  The RX Ruffers addresses and sizes must be properly defined to be aligned
       to L1-CACHE line size (32 bytes).
*/

/* Data Type Definitions */
typedef enum
{
  RX_ALLOC_OK       = 0x00,
  RX_ALLOC_ERROR    = 0x01
} RxAllocStatusTypeDef;

typedef struct
{
  struct pbuf_custom pbuf_custom;
  uint8_t buff[(ETH_RX_BUF_SIZE + 31) & ~31] __ALIGNED(32);
} RxBuff_t;

/* Memory Pool Declaration */
#define ETH_RX_BUFFER_CNT             12U
LWIP_MEMPOOL_DECLARE(RX_POOL, ETH_RX_BUFFER_CNT, sizeof(RxBuff_t), "Zero-copy RX PBUF pool");

/* Variable Definitions */
/* [수정] volatile 추가. 이 플래그는 세 곳에서 만진다 —
 *   쓰기: HAL_ETH_RxAllocateCallback(EthIf) / pbuf_free_custom(tcpip 또는 앱 태스크)
 *   읽기: low_level_input, ethernetif_input(EthIf)
 * 스레드를 건너 갱신되므로 컴파일러가 레지스터에 캐싱하면 안 된다(ST 원본은 빠져 있다). */
static volatile uint8_t RxAllocStatus;

ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */

osSemaphoreId RxPktSemaphore = NULL;   /* Semaphore to signal incoming packets */
osSemaphoreId TxPktSemaphore = NULL;   /* Semaphore to signal transmit packet complete */

/* Global Ethernet handle */
ETH_HandleTypeDef heth;
ETH_TxPacketConfig TxConfig;

/* Private function prototypes -----------------------------------------------*/
int32_t ETH_PHY_IO_Init(void);
int32_t ETH_PHY_IO_DeInit (void);
int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal);
int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal);
int32_t ETH_PHY_IO_GetTick(void);

lan8742_Object_t LAN8742;
lan8742_IOCtx_t  LAN8742_IOCtx = {ETH_PHY_IO_Init,
                                  ETH_PHY_IO_DeInit,
                                  ETH_PHY_IO_WriteReg,
                                  ETH_PHY_IO_ReadReg,
                                  ETH_PHY_IO_GetTick};

/* USER CODE BEGIN 3 */

/* USER CODE END 3 */

/* Private functions ---------------------------------------------------------*/
void pbuf_free_custom(struct pbuf *p);

/**
  * @brief  Ethernet Rx Transfer completed callback
  * @param  handlerEth: ETH handler
  * @retval None
  */
void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *handlerEth)
{
  osSemaphoreRelease(RxPktSemaphore);
}
/**
  * @brief  Ethernet Tx Transfer completed callback
  * @param  handlerEth: ETH handler
  * @retval None
  */
void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *handlerEth)
{
  osSemaphoreRelease(TxPktSemaphore);
}
/**
  * @brief  Ethernet DMA transfer error callback
  * @param  handlerEth: ETH handler
  * @retval None
  */
void HAL_ETH_ErrorCallback(ETH_HandleTypeDef *handlerEth)
{
  if((HAL_ETH_GetDMAError(handlerEth) & ETH_DMASR_RBUS) == ETH_DMASR_RBUS)
  {
     osSemaphoreRelease(RxPktSemaphore);
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/*******************************************************************************
                       LL Driver Interface ( LwIP stack --> ETH)
*******************************************************************************/
/**
 * @brief In this function, the hardware should be initialized.
 * Called from ethernetif_init().
 *
 * @param netif the already initialized lwip network interface structure
 *        for this ethernetif
 */
static void low_level_init(struct netif *netif)
{
  HAL_StatusTypeDef hal_eth_init_status = HAL_OK;
/* USER CODE BEGIN OS_THREAD_ATTR_CMSIS_RTOS_V2 */
  osThreadAttr_t attributes;
/* USER CODE END OS_THREAD_ATTR_CMSIS_RTOS_V2 */
  uint32_t duplex, speed = 0;
  /* [수정] 오토네고가 끝나 '실제 속도'를 읽었을 때만 1. 아래 default 케이스 주석 참고. */
  uint32_t speedKnown = 0U;
  int32_t PHYLinkState = 0;
  ETH_MACConfigTypeDef MACConf = {0};
  /* Start ETH HAL Init */

   uint8_t MACAddr[6] ;
  heth.Instance = ETH;
  MACAddr[0] = 0x00;
  MACAddr[1] = 0x80;
  MACAddr[2] = 0xE1;
  MACAddr[3] = 0x00;
  MACAddr[4] = 0x00;
  MACAddr[5] = 0x00;
  heth.Init.MACAddr = &MACAddr[0];
  heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
  heth.Init.TxDesc = DMATxDscrTab;
  heth.Init.RxDesc = DMARxDscrTab;
  heth.Init.RxBuffLen = 1536;

  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

  hal_eth_init_status = HAL_ETH_Init(&heth);

  memset(&TxConfig, 0 , sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;

  /* End ETH HAL Init */

  /* Initialize the RX POOL */
  LWIP_MEMPOOL_INIT(RX_POOL);

#if LWIP_ARP || LWIP_ETHERNET

  /* set MAC hardware address length */
  netif->hwaddr_len = ETH_HWADDR_LEN;

  /* set MAC hardware address */
  netif->hwaddr[0] =  heth.Init.MACAddr[0];
  netif->hwaddr[1] =  heth.Init.MACAddr[1];
  netif->hwaddr[2] =  heth.Init.MACAddr[2];
  netif->hwaddr[3] =  heth.Init.MACAddr[3];
  netif->hwaddr[4] =  heth.Init.MACAddr[4];
  netif->hwaddr[5] =  heth.Init.MACAddr[5];

  /* maximum transfer unit */
  netif->mtu = ETH_MAX_PAYLOAD;

  /* Accept broadcast address and ARP traffic */
  /* don't set NETIF_FLAG_ETHARP if this device is not an ethernet one */
  #if LWIP_ARP
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
  #else
    netif->flags |= NETIF_FLAG_BROADCAST;
  #endif /* LWIP_ARP */

  /* create a binary semaphore used for informing ethernetif of frame reception */
  RxPktSemaphore = osSemaphoreNew(1, 0, NULL);

  /* create a binary semaphore used for informing ethernetif of frame transmission */
  TxPktSemaphore = osSemaphoreNew(1, 0, NULL);

  /* create the task that handles the ETH_MAC */
/* USER CODE BEGIN OS_THREAD_NEW_CMSIS_RTOS_V2 */
  memset(&attributes, 0x0, sizeof(osThreadAttr_t));
  attributes.name = "EthIf";
  attributes.stack_size = INTERFACE_THREAD_STACK_SIZE;
  attributes.priority = osPriorityRealtime;
  osThreadNew(ethernetif_input, netif, &attributes);
/* USER CODE END OS_THREAD_NEW_CMSIS_RTOS_V2 */

/* USER CODE BEGIN PHY_PRE_CONFIG */

/* USER CODE END PHY_PRE_CONFIG */
  /* Set PHY IO functions */
  LAN8742_RegisterBusIO(&LAN8742, &LAN8742_IOCtx);

  /* Initialize the LAN8742 ETH PHY */
  if(LAN8742_Init(&LAN8742) != LAN8742_STATUS_OK)
  {
    netif_set_link_down(netif);
    netif_set_down(netif);
    return;
  }

  if (hal_eth_init_status == HAL_OK)
  {
    /* [수정] 여기서 링크를 올리지 않는다 — 무조건 down으로 두고 나간다.
     *
     * ST 원본은 GetLinkState()를 곧바로 읽어 그 값으로 MAC을 설정하고 링크를 올렸다.
     * 그런데 이 시점은 부팅 직후라 오토네고가 끝나지 않았거나(2~3초 소요), 협상이
     * 실패해 parallel detect 결과(100M-HALF / 10M-HALF)가 나와 있기 쉽다.
     *
     * 그 값을 그대로 래치하면 치명적이다. netif가 up이 되는 순간
     * ethernet_link_thread의 두 분기가 **둘 다 막히기 때문이다**:
     *   · 첫 if   : 링크가 끊겨야(PHYLinkState <= LINK_DOWN) 진입
     *   · else if : netif가 down이어야 진입
     * 즉 한번 잘못된 속도로 올려두면 **영원히 재평가되지 않는다.**
     * (실측: `[ETH] init phy=10M-HALF -> MAC applied` 이후 20초 넘게 [ETH] 로그가
     *  단 한 줄도 안 나온다 = 그 상태로 고착.)
     *
     * → 판단을 전부 링크 스레드에 위임한다. 100ms 뒤 첫 폴에서 올바른 속도로 올려주고,
     *   아래 ③의 재평가/재협상 로직이 이후로도 계속 따라간다.
     *   부팅이 100ms 늦어지는 것뿐이고, 그 대가로 래치 경합이 통째로 사라진다. */
    (void)speed; (void)duplex; (void)speedKnown; (void)MACConf;

    /* 리셋에서 갓 나온 PHY를 알려진 좋은 상태로 만들고 협상을 처음부터 시작시킨다.
     * (파워다운 스트랩 오래치 / 직전 세션이 남긴 실패한 협상에서 확실히 벗어난다) */
    EthPhy_Recover("boot");

    PHYLinkState = LAN8742_GetLinkState(&LAN8742);
    netif_set_down(netif);
    netif_set_link_down(netif);
    EthDbg_Link("init ", PHYLinkState, 0U);
/* USER CODE BEGIN PHY_POST_CONFIG */

/* USER CODE END PHY_POST_CONFIG */
  }
  else
  {
    Error_Handler();
  }
#endif /* LWIP_ARP || LWIP_ETHERNET */

/* USER CODE BEGIN LOW_LEVEL_INIT */

/* USER CODE END LOW_LEVEL_INIT */
}

/**
 * @brief This function should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @param p the MAC packet to send (e.g. IP packet including MAC addresses and type)
 * @return ERR_OK if the packet could be sent
 *         an err_t value if the packet couldn't be sent
 *
 * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
 *       strange results. You might consider waiting for space in the DMA queue
 *       to become available since the stack doesn't retry to send a packet
 *       dropped because of memory failure (except for the TCP timers).
 */

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
  uint32_t i = 0U;
  struct pbuf *q = NULL;
  err_t errval = ERR_OK;
  ETH_BufferTypeDef Txbuffer[ETH_TX_DESC_CNT] = {0};

  memset(Txbuffer, 0 , ETH_TX_DESC_CNT*sizeof(ETH_BufferTypeDef));

  for(q = p; q != NULL; q = q->next)
  {
    if(i >= ETH_TX_DESC_CNT)
      return ERR_IF;

    Txbuffer[i].buffer = q->payload;
    Txbuffer[i].len = q->len;

    if(i>0)
    {
      Txbuffer[i-1].next = &Txbuffer[i];
    }

    if(q->next == NULL)
    {
      Txbuffer[i].next = NULL;
    }

    i++;
  }

  TxConfig.Length = p->tot_len;
  TxConfig.TxBuffer = Txbuffer;
  TxConfig.pData = p;

  pbuf_ref(p);

  do
  {
    if(HAL_ETH_Transmit_IT(&heth, &TxConfig) == HAL_OK)
    {
      errval = ERR_OK;
    }
    else
    {

      if(HAL_ETH_GetError(&heth) & HAL_ETH_ERROR_BUSY)
      {
        /* Wait for descriptors to become available */
        osSemaphoreAcquire(TxPktSemaphore, ETHIF_TX_TIMEOUT);
        HAL_ETH_ReleaseTxPacket(&heth);
        errval = ERR_BUF;
      }
      else
      {
        /* Other error */
        pbuf_free(p);
        errval =  ERR_IF;
      }
    }
  }while(errval == ERR_BUF);

  return errval;
}

/**
 * @brief Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return a pbuf filled with the received packet (including MAC header)
 *         NULL on memory error
   */
static struct pbuf * low_level_input(struct netif *netif)
{
  struct pbuf *p = NULL;

  if(RxAllocStatus == RX_ALLOC_OK)
  {
    HAL_ETH_ReadData(&heth, (void **)&p);
  }

  return p;
}

/**
 * @brief This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface. Then the type of the received packet is determined and
 * the appropriate input function is called.
 *
 * @param netif the lwip network interface structure for this ethernetif
 */
void ethernetif_input(void* argument)
{
  struct pbuf *p = NULL;
  struct netif *netif = (struct netif *) argument;

  for( ;; )
  {
    /* [수정] ST 원본은 (a) portMAX_DELAY로 무한 대기하고 (b) 세마포어를 받았을 때만
     * 수신을 시도했다. 그 조합에서 아래 데드락이 실제로 발생했다:
     *
     *   1. 대량 수신(펌웨어 전송)으로 RX pbuf 풀 고갈
     *   2. HAL_ETH_RxAllocateCallback 할당 실패 → RxAllocStatus = RX_ALLOC_ERROR
     *   3. low_level_input()이 RX_ALLOC_OK가 아니면 즉시 NULL → do/while 탈출 → 무한 대기
     *   4. 깨울 수 있는 건 ETH RX 인터럽트(디스크립터가 없어 오지 않음)와
     *      pbuf_free_custom()뿐인데, 플래그가 세워진 시점에 이미 모든 pbuf가 반납된
     *      뒤라면 pbuf_free_custom()이 다시 불릴 일이 없다 → 영구 정지
     *
     *   → MAC 수신이 죽어 ARP조차 응답하지 못한다(링크는 Up). 보드 리셋 외엔 복구 불가.
     *
     * 타임아웃만 유한값으로 바꾸는 것으론 부족하다. 타임아웃 시 osOK가 아니라서 원본은
     * 수신 시도 자체를 건너뛰고, 들어간다 해도 RxAllocStatus가 ERROR인 채라 또 NULL이다.
     * 그래서 '반환값과 무관하게 매번 재시도' + '막힌 플래그 해제'를 함께 둔다. */
    (void)osSemaphoreAcquire(RxPktSemaphore, TIME_WAITING_FOR_INPUT);

    /* 풀 고갈로 막혀 있었다면 풀어준다. 아직 비어 있으면 할당 콜백이 곧바로 다시
     * ERROR로 되돌리므로 무해하다(재시도 비용만 든다). */
    if (RxAllocStatus == RX_ALLOC_ERROR)
    {
      RxAllocStatus = RX_ALLOC_OK;
    }

    do
    {
      p = low_level_input( netif );
      if (p != NULL)
      {
        if (netif->input( p, netif) != ERR_OK )
        {
          pbuf_free(p);
        }
      }
    } while(p!=NULL);
  }
}

#if !LWIP_ARP
/**
 * This function has to be completed by user in case of ARP OFF.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if ...
 */
static err_t low_level_output_arp_off(struct netif *netif, struct pbuf *q, const ip4_addr_t *ipaddr)
{
  err_t errval;
  errval = ERR_OK;

/* USER CODE BEGIN 5 */

/* USER CODE END 5 */

  return errval;

}
#endif /* LWIP_ARP */

/**
 * @brief Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 * This function should be passed as a parameter to netif_add().
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if the loopif is initialized
 *         ERR_MEM if private data couldn't be allocated
 *         any other err_t on error
 */
err_t ethernetif_init(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
  /* Initialize interface hostname */
  netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

  /*
   * Initialize the snmp variables and counters inside the struct netif.
   * The last argument should be replaced with your link speed, in units
   * of bits per second.
   */
  // MIB2_INIT_NETIF(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED_OF_YOUR_NETIF_IN_BPS);

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
  /* We directly use etharp_output() here to save a function call.
   * You can instead declare your own function an call etharp_output()
   * from it if you have to do some checks before sending (e.g. if link
   * is available...) */

#if LWIP_IPV4
#if LWIP_ARP || LWIP_ETHERNET
#if LWIP_ARP
  netif->output = etharp_output;
#else
  /* The user should write its own code in low_level_output_arp_off function */
  netif->output = low_level_output_arp_off;
#endif /* LWIP_ARP */
#endif /* LWIP_ARP || LWIP_ETHERNET */
#endif /* LWIP_IPV4 */

#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 */

  netif->linkoutput = low_level_output;

  /* initialize the hardware */
  low_level_init(netif);

  return ERR_OK;
}

/**
  * @brief  Custom Rx pbuf free callback
  * @param  pbuf: pbuf to be freed
  * @retval None
  */
void pbuf_free_custom(struct pbuf *p)
{
  struct pbuf_custom* custom_pbuf = (struct pbuf_custom*)p;
  LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf);

  /* If the Rx Buffer Pool was exhausted, signal the ethernetif_input task to
   * call HAL_ETH_GetRxDataBuffer to rebuild the Rx descriptors. */

  if (RxAllocStatus == RX_ALLOC_ERROR)
  {
    RxAllocStatus = RX_ALLOC_OK;
    osSemaphoreRelease(RxPktSemaphore);
  }
}

/* USER CODE BEGIN 6 */

/**
* @brief  lwIP에 현재 시각(ms)을 제공한다. NO_SYS 여부와 무관하게 필요하다.
* @note   [RTOS 전환 시 확인함] 이 버전의 ST sys_arch.c는 mbox/sem/mutex/sys_init만 제공하고
*         sys_now()는 정의하지 않는다. 반면 lwIP 코어(timeouts.c, api_lib.c, tcp_out.c)는
*         sys_now()를 호출하므로, 이 정의를 지우면 undefined reference가 난다. 유지할 것.
*         HAL_GetTick()은 이제 TIM6 기반이며(FreeRTOS가 SysTick을 가져감) 어느 컨텍스트에서도 안전하다.
* @retval Current Time value (ms)
*/
u32_t sys_now(void)
{
  return HAL_GetTick();
}

/* USER CODE END 6 */

/**
  * @brief  Initializes the ETH MSP.
  * @param  ethHandle: ETH handle
  * @retval None
  */

void HAL_ETH_MspInit(ETH_HandleTypeDef* ethHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(ethHandle->Instance==ETH)
  {
  /* USER CODE BEGIN ETH_MspInit 0 */

  /* USER CODE END ETH_MspInit 0 */
    /* Enable Peripheral clock */
    __HAL_RCC_ETH_CLK_ENABLE();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    /**ETH GPIO Configuration
    PC1     ------> ETH_MDC
    PA1     ------> ETH_REF_CLK
    PA2     ------> ETH_MDIO
    PA7     ------> ETH_CRS_DV
    PC4     ------> ETH_RXD0
    PC5     ------> ETH_RXD1
    PB13     ------> ETH_TXD1
    PG11     ------> ETH_TX_EN
    PG13     ------> ETH_TXD0
    */
    GPIO_InitStruct.Pin = RMII_MDC_Pin|RMII_RXD0_Pin|RMII_RXD1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = RMII_REF_CLK_Pin|RMII_MDIO_Pin|RMII_CRS_DV_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = RMII_TXD1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(RMII_TXD1_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = RMII_TX_EN_Pin|RMII_TXD0_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* Peripheral interrupt init */
    HAL_NVIC_SetPriority(ETH_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(ETH_IRQn);
  /* USER CODE BEGIN ETH_MspInit 1 */

  /* USER CODE END ETH_MspInit 1 */
  }
}

void HAL_ETH_MspDeInit(ETH_HandleTypeDef* ethHandle)
{
  if(ethHandle->Instance==ETH)
  {
  /* USER CODE BEGIN ETH_MspDeInit 0 */

  /* USER CODE END ETH_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_ETH_CLK_DISABLE();

    /**ETH GPIO Configuration
    PC1     ------> ETH_MDC
    PA1     ------> ETH_REF_CLK
    PA2     ------> ETH_MDIO
    PA7     ------> ETH_CRS_DV
    PC4     ------> ETH_RXD0
    PC5     ------> ETH_RXD1
    PB13     ------> ETH_TXD1
    PG11     ------> ETH_TX_EN
    PG13     ------> ETH_TXD0
    */
    HAL_GPIO_DeInit(GPIOC, RMII_MDC_Pin|RMII_RXD0_Pin|RMII_RXD1_Pin);

    HAL_GPIO_DeInit(GPIOA, RMII_REF_CLK_Pin|RMII_MDIO_Pin|RMII_CRS_DV_Pin);

    HAL_GPIO_DeInit(RMII_TXD1_GPIO_Port, RMII_TXD1_Pin);

    HAL_GPIO_DeInit(GPIOG, RMII_TX_EN_Pin|RMII_TXD0_Pin);

    /* Peripheral interrupt Deinit*/
    HAL_NVIC_DisableIRQ(ETH_IRQn);

  /* USER CODE BEGIN ETH_MspDeInit 1 */

  /* USER CODE END ETH_MspDeInit 1 */
  }
}

/*******************************************************************************
                       PHI IO Functions
*******************************************************************************/
/**
  * @brief  Initializes the MDIO interface GPIO and clocks.
  * @param  None
  * @retval 0 if OK, -1 if ERROR
  */
int32_t ETH_PHY_IO_Init(void)
{
  /* We assume that MDIO GPIO configuration is already done
     in the ETH_MspInit() else it should be done here
  */

  /* Configure the MDIO Clock */
  HAL_ETH_SetMDIOClockRange(&heth);

  return 0;
}

/**
  * @brief  De-Initializes the MDIO interface .
  * @param  None
  * @retval 0 if OK, -1 if ERROR
  */
int32_t ETH_PHY_IO_DeInit (void)
{
  return 0;
}

/**
  * @brief  Read a PHY register through the MDIO interface.
  * @param  DevAddr: PHY port address
  * @param  RegAddr: PHY register address
  * @param  pRegVal: pointer to hold the register value
  * @retval 0 if OK -1 if Error
  */
int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal)
{
  if(HAL_ETH_ReadPHYRegister(&heth, DevAddr, RegAddr, pRegVal) != HAL_OK)
  {
    return -1;
  }

  return 0;
}

/**
  * @brief  Write a value to a PHY register through the MDIO interface.
  * @param  DevAddr: PHY port address
  * @param  RegAddr: PHY register address
  * @param  RegVal: Value to be written
  * @retval 0 if OK -1 if Error
  */
int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal)
{
  if(HAL_ETH_WritePHYRegister(&heth, DevAddr, RegAddr, RegVal) != HAL_OK)
  {
    return -1;
  }

  return 0;
}

/**
  * @brief  Get the time in millisecons used for internal PHY driver process.
  * @retval Time value
  */
int32_t ETH_PHY_IO_GetTick(void)
{
  return HAL_GetTick();
}

/**
  * @brief  Check the ETH link state then update ETH driver and netif link accordingly.
  * @retval None
  */
void ethernet_link_thread(void* argument)
{
  ETH_MACConfigTypeDef MACConf = {0};
  int32_t PHYLinkState = 0;
  uint32_t linkchanged = 0U, speed = 0U, duplex = 0U;

  struct netif *netif = (struct netif *) argument;
/* USER CODE BEGIN ETH link init */
  /* 현재 MAC에 실제로 적용되어 있는 속도/듀플렉스. PHY가 재협상으로 값을 바꿨을 때
   * '달라졌는지'를 판단하려면 적용값을 따로 들고 있어야 한다(netif의 up/down만으로는
   * 알 수 없다 — 그게 기존 고착 버그의 원인이었다). */
  static uint32_t s_appliedSpeed  = 0U;
  static uint32_t s_appliedDuplex = 0U;
  static uint32_t s_appliedValid  = 0U;

  /* 링크가 안 올라온 채로 몇 번 폴링했는지 / 재협상을 몇 번 시도했는지 */
  static uint32_t s_downPolls     = 0U;
  static uint32_t s_recoverTries  = 0U;
/* USER CODE END ETH link init */

  for(;;)
  {
  PHYLinkState = LAN8742_GetLinkState(&LAN8742);

  /* [수정] ST 원본은 linkchanged/speed/duplex를 루프 '밖'에 선언해두고 여기서 초기화하지
   * 않는다. 그래서 한 번 1이 되면 영영 1로 남아 아래 문제가 생겼다:
   *
   *   1. 최초 링크 업 — PHY가 100M 보고 → speed=100M, linkchanged=1 → 정상 설정
   *   2. 케이블 분리 → 링크 다운
   *   3. 재연결 — 이 스레드가 100ms마다 도는데 '오토네고 미완료' 순간에 걸리면
   *      switch가 default로 빠져 speed/duplex가 갱신되지 않는다.
   *   4. 그런데 linkchanged는 아직 1 → 지난번 값으로 MAC을 설정하고
   *      netif_set_link_up()까지 호출해 버린다.
   *   5. 이제 netif가 up이라 위 else-if 조건에 다시는 걸리지 않는다
   *      → PHY 실제 속도와 MAC 설정이 어긋난 채 고착 → 프레임 수신 불가.
   *
   * 증상: 링크는 Up인데 ARP조차 무응답. 보드 리셋이나 '운 좋은' 재연결로만 복구.
   * 폴링 시점이 오토네고 완료 전후 어디에 걸리느냐에 달린 경합이라 재현이 들쭉날쭉하다.
   *
   * → 매 회 0으로 초기화한다. 유효한 속도를 실제로 읽은 경우에만 MAC을 설정하고,
   *   아니면 netif를 down인 채로 두고 100ms 뒤 다시 시도한다. */
  linkchanged = 0U;

  /* PHY가 보고한 상태를 MAC 설정값으로 변환한다. 유효한 링크일 때만 linkchanged=1.
   * (AUTONEG_NOTDONE / LINK_DOWN / 읽기 오류는 전부 '아직 모름'으로 남긴다) */
  switch (PHYLinkState)
  {
  case LAN8742_STATUS_100MBITS_FULLDUPLEX:
    duplex = ETH_FULLDUPLEX_MODE;  speed = ETH_SPEED_100M;  linkchanged = 1;  break;
  case LAN8742_STATUS_100MBITS_HALFDUPLEX:
    duplex = ETH_HALFDUPLEX_MODE;  speed = ETH_SPEED_100M;  linkchanged = 1;  break;
  case LAN8742_STATUS_10MBITS_FULLDUPLEX:
    duplex = ETH_FULLDUPLEX_MODE;  speed = ETH_SPEED_10M;   linkchanged = 1;  break;
  case LAN8742_STATUS_10MBITS_HALFDUPLEX:
    duplex = ETH_HALFDUPLEX_MODE;  speed = ETH_SPEED_10M;   linkchanged = 1;  break;
  default:
    break;
  }

  if(netif_is_link_up(netif) && (PHYLinkState <= LAN8742_STATUS_LINK_DOWN))
  {
    HAL_ETH_Stop_IT(&heth);
    netif_set_down(netif);
    netif_set_link_down(netif);
    s_appliedValid = 0U;                 /* 적용값 무효화 */
    EthDbg_Link("down ", PHYLinkState, 0U);
  }
  else if(!netif_is_link_up(netif) && linkchanged)
  {
    /* Get MAC Config MAC */
    HAL_ETH_GetMACConfig(&heth, &MACConf);
    MACConf.DuplexMode = duplex;
    MACConf.Speed = speed;
    HAL_ETH_SetMACConfig(&heth, &MACConf);
    HAL_ETH_Start_IT(&heth);
    netif_set_up(netif);
    netif_set_link_up(netif);
    s_appliedSpeed = speed;  s_appliedDuplex = duplex;  s_appliedValid = 1U;
    EthDbg_Link("up   ", PHYLinkState, 1U);
  }
  else if(netif_is_link_up(netif) && linkchanged && s_appliedValid &&
          ((speed != s_appliedSpeed) || (duplex != s_appliedDuplex)))
  {
    /* ★ [추가] 링크는 유지된 채 속도/듀플렉스만 재협상된 경우.
     *
     * 원래 코드에는 이 경로가 아예 없었다. 위 두 분기는 각각 '링크 상실'과
     * 'netif가 down'일 때만 도는데, 링크가 up인 채 PHY만 100M→10M로 바뀌면
     * 어느 쪽에도 걸리지 않아 **MAC이 옛 설정에 영구 고착**된다
     * → 링크는 Up인데 프레임을 못 받는다(ARP조차 무응답).
     *
     * 파일 아래쪽 [진단] 블록의 주석이 "지금 구조에서는 아무도 이걸 보지 않는다"고
     * 적어둔 바로 그 구멍이다. 이 분기가 그것을 닫는다. */
    HAL_ETH_Stop_IT(&heth);
    HAL_ETH_GetMACConfig(&heth, &MACConf);
    MACConf.DuplexMode = duplex;
    MACConf.Speed = speed;
    HAL_ETH_SetMACConfig(&heth, &MACConf);
    HAL_ETH_Start_IT(&heth);
    s_appliedSpeed = speed;  s_appliedDuplex = duplex;
    EthDbg_Link("respd", PHYLinkState, 1U);
  }

  /* ★ [추가] 링크가 오래 안 올라오면 오토네고를 다시 건다.
   *
   * PHY는 보통 협상 실패 시 스스로 재시도하지만, 파워다운 스트랩 오래치나 협상이
   * 어긋난 상태로는 영영 안 올라온다(실측: TCP OTA 후 LED 꺼진 채 통신 불가).
   * 이 펌웨어에는 원래 PHY를 되살릴 수단이 전혀 없었다 — 그 마지막 방어선이다.
   *
   * 케이블이 그냥 빠져 있는 경우에도 여기 걸리므로, 링크가 한 번 올라올 때까지
   * 시도 횟수를 제한해 로그·MDIO 트래픽이 무한히 쌓이지 않게 한다. */
  if (linkchanged)
  {
    s_downPolls = 0U;
    s_recoverTries = 0U;
  }
  else
  {
    s_downPolls++;
    if ((s_downPolls >= ETH_RENEGO_AFTER_POLLS) && (s_recoverTries < ETH_RENEGO_MAX_TRIES))
    {
      s_downPolls = 0U;
      s_recoverTries++;
      EthPhy_Recover("link down too long");
    }
  }

/* USER CODE BEGIN ETH link Thread core code for User BSP */
    /* [진단] 링크가 up인 동안 PHY 속도가 바뀌는지 감시한다.
     *
     * 지금 구조에서는 아무도 이걸 보지 않는다 — 위 else-if는 netif가 down일 때만 돌기
     * 때문이다. 그래서 "링크를 유지한 채 100M→10M로 재협상"되면 MAC은 옛 설정 그대로
     * 남는다는 가설이 있었는데, 확인할 방법이 없었다.
     *
     * 상태가 바뀔 때만 찍는다(매 100ms 찍으면 로그가 OTA를 방해한다). */
    {
      static int32_t lastState = -99;
      if (PHYLinkState != lastState)
      {
        lastState = PHYLinkState;
        EthDbg_Link("poll ", PHYLinkState, 0U);
      }
    }
/* USER CODE END ETH link Thread core code for User BSP */

    osDelay(100);
  }
}

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
/* USER CODE BEGIN HAL ETH RxAllocateCallback */
  struct pbuf_custom *p = LWIP_MEMPOOL_ALLOC(RX_POOL);
  if (p)
  {
    /* Get the buff from the struct pbuf address. */
    *buff = (uint8_t *)p + offsetof(RxBuff_t, buff);
    p->custom_free_function = pbuf_free_custom;
    /* Initialize the struct pbuf.
    * This must be performed whenever a buffer's allocated because it may be
    * changed by lwIP or the app, e.g., pbuf_free decrements ref. */
    pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, p, *buff, ETH_RX_BUF_SIZE);
  }
  else
  {
    RxAllocStatus = RX_ALLOC_ERROR;
    *buff = NULL;
  }
/* USER CODE END HAL ETH RxAllocateCallback */
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
/* USER CODE BEGIN HAL ETH RxLinkCallback */

  struct pbuf **ppStart = (struct pbuf **)pStart;
  struct pbuf **ppEnd = (struct pbuf **)pEnd;
  struct pbuf *p = NULL;

  /* Get the struct pbuf from the buff address. */
  p = (struct pbuf *)(buff - offsetof(RxBuff_t, buff));
  p->next = NULL;
  p->tot_len = 0;
  p->len = Length;

  /* Chain the buffer. */
  if (!*ppStart)
  {
    /* The first buffer of the packet. */
    *ppStart = p;
  }
  else
  {
    /* Chain the buffer to the end of the packet. */
    (*ppEnd)->next = p;
  }
  *ppEnd  = p;

  /* Update the total length of all the buffers of the chain. Each pbuf in the chain should have its tot_len
   * set to its own length, plus the length of all the following pbufs in the chain. */
  for (p = *ppStart; p != NULL; p = p->next)
  {
    p->tot_len += Length;
  }

/* USER CODE END HAL ETH RxLinkCallback */
}

void HAL_ETH_TxFreeCallback(uint32_t * buff)
{
/* USER CODE BEGIN HAL ETH TxFreeCallback */

  pbuf_free((struct pbuf *)buff);

/* USER CODE END HAL ETH TxFreeCallback */
}

/* USER CODE BEGIN 8 */

/* USER CODE END 8 */
