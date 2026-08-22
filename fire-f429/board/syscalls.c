/**
  * @file    syscalls.c
  * @brief   Minimal newlib retarget layer for a bare-metal STM32F429 build.
  *
  * _write() is redirected to the UART (USART1, PA9/PA10) so printf() output is
  * visible on a USB-serial virtual COM port at 115200 baud.
  */

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include "uart_printf.h"

/* Linker-provided end of static data / start of heap. */
extern char end[];
extern char _estack[];

#define HEAP_LIMIT ((char *)_estack - 0x1000)   /* leave room for the stack */

#ifdef __cplusplus
extern "C" {
#endif

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

/* printf()/putchar() output -> USART1 (PA9 TX). */
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
