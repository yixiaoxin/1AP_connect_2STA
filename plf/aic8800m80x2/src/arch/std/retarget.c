#include <string.h>
#include <stdio.h>
#include "stdio_uart.h"

#if defined (__CC_ARM)
#include <rt_sys.h>

#pragma import(__use_no_semihosting_swi)

/* TODO: Standard IO device handles. */
#define STDIN       1
#define STDOUT      2
#define STDERR      3

/* Standard IO device name defines. */
const char __stdin_name[]  = "STDIN";
const char __stdout_name[] = "STDOUT";
const char __stderr_name[] = "STDERR";

int _sys_close(FILEHANDLE fh)
{
    return 0;
}

/**
 * write data
 *
 * @param fh - file handle
 * @param buf - data buffer
 * @param len - buffer length
 * @param mode - useless, for historical reasons
 * @return a positive number representing the number of characters not written.
 */
int _sys_write(FILEHANDLE fh, const unsigned char *buf, unsigned len, int mode)
{
    if ((fh == STDOUT) || (fh == STDERR)) {
        stdio_uart_nputs((const char*)buf, len);
        return 0;
    }

    if (fh == STDIN)
        return -1;

    return 0;
}

void _sys_exit(int return_code)
{
    /* TODO: perhaps exit the thread which is invoking this function */
    while (1);
}

#ifdef __MICROLIB
#include <stdio.h>

int fputc(int c, FILE *f)
{
    stdio_uart_putc((char)c);
    return 1;
}
#endif

#elif defined (__GNUC__)

#include <stdio.h>
#include <stdlib.h>

int _write(int fd, char *ptr, int len)
{
    /*
     * write "len" of char from "ptr" to file id "fd"
     * Return number of char written.
     *
    * Only work for STDOUT, STDIN, and STDERR
     */
    if (fd > 2)
    {
        return -1;
    }

    stdio_uart_nputs((const char*)ptr, len);

    return len;
}

#else
#error unknown complier
#endif

