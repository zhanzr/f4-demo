/**
  * @file    eth_http_server/src/arch/cc.h
  * @brief   lwIP compiler/architecture config (NO_SYS, GCC/ARM).
  */
#ifndef __ARCH_CC_H__
#define __ARCH_CC_H__

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* lwIP basic types */
typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uintptr_t mem_ptr_t;

/* Byte order (little-endian ARM) */
#define BYTE_ORDER LITTLE_ENDIAN

/* Diagnostics / assertions */
#define LWIP_PLATFORM_DIAG(x)   do { printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { printf("lwip assert: %s\n", x); \
                                     for (;;) { } } while (0)

/* Random number generator: fall back to a simple LCG (lwIP 2.0.3 default
 * uses LWIP_RAND() when LWIP_RAND == LWIP_RAND; define it explicitly). */
#ifdef LWIP_RAND
#undef LWIP_RAND
#endif
#define LWIP_RAND()  lwip_rand_fallback()

/* Provided in ethernetif.c. */
unsigned int lwip_rand_fallback(void);

#endif /* __ARCH_CC_H__ */