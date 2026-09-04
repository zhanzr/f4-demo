/**
  * @file    syscalls.c
  * @brief   Minimal newlib retarget layer for a bare-metal STM32F407 build.
  *
  * _write() is redirected to the UART (USART3, PD8/PD9) so printf() output is
  * visible on the on-board RS232/RS485 transceivers at 115200 baud.
  */

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include "uart_printf.h"

/* Linker-provided end of static data / start of heap. */
extern char end[];
/* _estack is a linker *address*, not an array. Declare it as a scalar so the
 * heap/stack guard below uses integer (not array-subscript) arithmetic; this
 * both removes a spurious -Warray-bounds warning and states the intent. */
extern char _estack;

/* Leave room for the stack: heap must stop 4 KB below the stack top. */
#define HEAP_LIMIT ((char *)((uintptr_t)&_estack - 0x1000U))

#ifdef __cplusplus
extern "C" {
#endif

/* ARM AEABI thread-pointer read. starm-clang's newlib implements errno as a
 * TLS variable (errno -> __tls), read through __aeabi_read_tp(). On an
 * unthreaded bare-metal target there is no thread pointer, so return a stable
 * static-address "thread" that __tls_offset/errno can index off of. GNU
 * newlib never references it (its errno is *__errno()), so GCC links find the
 * symbol unused and --gc-sections drops it. */
void *__aeabi_read_tp(void)
{
    static char tp_slot;
    return &tp_slot;
}

void _init(void)
{
}
void _fini(void)
{
}

void _exit(int status)
{
    (void)status;
    while (1)
    {
    }
}

int _close(int file)
{
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    errno = ENOSYS;
    return -1;
}

/* printf()/putchar() output -> USART3 (PD8 TX). */
int _write(int file, char *ptr, int len)
{
    int i;
    (void)file;
    for (i = 0; i < len; i++)
    {
        UART_PutChar((unsigned char)ptr[i]);
    }
    return len;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

void *_sbrk(int incr)
{
    static char *heap_end = 0;
    char *prev_heap_end;

    if (heap_end == 0)
    {
        heap_end = end;
    }
    prev_heap_end = heap_end;

    if (heap_end + incr > HEAP_LIMIT)
    {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap_end += incr;
    return prev_heap_end;
}

#ifdef __cplusplus
}
#endif
