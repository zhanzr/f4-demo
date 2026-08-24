/**
  * @file    eth_http_server/src/lwipopts.h
  * @brief   lwIP Options Configuration (NO_SYS / raw API), based on the
  *          STM32F769I-Discovery eth_http lwipopts.h.
  */

#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/**
 * NO_SYS==1: Use lwIP without OS-awareness (no thread, semaphores, mutexes).
 * The HTTP server is a raw-TCP app driven from the main loop.
 */
#define NO_SYS                  1

/**
 * SYS_LIGHTWEIGHT_PROT==0: disable inter-task protection for critical
 * regions (single-threaded).
 */
#define SYS_LIGHTWEIGHT_PROT    0

/* ---------- Checksum options ---------- */
/* The STM32F4xx computes/verifies IP, UDP, TCP and ICMP checksums in
 * hardware; disable them in software. */
#define CHECKSUM_BY_HARDWARE
#ifdef CHECKSUM_BY_HARDWARE
  #define CHECKSUM_GEN_IP                 0
  #define CHECKSUM_GEN_UDP                0
  #define CHECKSUM_GEN_TCP                0
  #define CHECKSUM_CHECK_IP               0
  #define CHECKSUM_CHECK_UDP              0
  #define CHECKSUM_CHECK_TCP              0
  #define CHECKSUM_GEN_ICMP               0
#else
  #define CHECKSUM_GEN_IP                 1
  #define CHECKSUM_GEN_UDP                1
  #define CHECKSUM_GEN_TCP                1
  #define CHECKSUM_CHECK_IP               1
  #define CHECKSUM_CHECK_UDP              1
  #define CHECKSUM_CHECK_TCP              1
  #define CHECKSUM_GEN_ICMP               1
#endif

/* ---------- Sequential / socket layers ---------- */
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0

/* ---------- Memory options ---------- */
#define MEM_ALIGNMENT           4
#define MEM_SIZE                (16*1024)

#define MEMP_NUM_PBUF           10
#define MEMP_NUM_UDP_PCB        6
#define MEMP_NUM_TCP_PCB        10
#define MEMP_NUM_TCP_PCB_LISTEN 5
#define MEMP_NUM_TCP_SEG        8
#define MEMP_NUM_SYS_TIMEOUT    10

/* ---------- Pbuf options ---------- */
#define PBUF_POOL_SIZE          8
#define PBUF_POOL_BUFSIZE       1524

/* ---------- IPv4 options ---------- */
#define LWIP_IPV4                1

/* ---------- TCP options ---------- */
#define LWIP_TCP                1
#define TCP_TTL                 255
#define TCP_QUEUE_OOSEQ         0
#define TCP_MSS                 (1500 - 40)
#define TCP_SND_BUF             (4*TCP_MSS)
#define TCP_SND_QUEUELEN        (2* TCP_SND_BUF/TCP_MSS)
#define TCP_WND                 (2*TCP_MSS)

/* ---------- ICMP options ---------- */
#define LWIP_ICMP                       1

/* ---------- DHCP options ---------- */
#define LWIP_DHCP               1

/* ---------- UDP options ---------- */
#define LWIP_UDP                1
#define UDP_TTL                 255

/* ---------- Statistics options ---------- */
#define LWIP_STATS 0

/* ---------- link callback options ---------- */
#define LWIP_NETIF_LINK_CALLBACK        1

#endif /* __LWIPOPTS_H__ */