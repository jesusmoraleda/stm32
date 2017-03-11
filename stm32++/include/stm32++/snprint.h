/**
 * @author Alexander Vassilev
 * @copyright BSD License
 */

#ifndef _SNPRINT_H
#define _SNPRINT_H

#include "tostring.h"
#include <assert.h>
#include <stdlib.h>

typedef void(*PrintSinkFunc)(const char* str, uint16_t len, uint8_t fd, void* userp);
enum: uint8_t { kPrintSinkLeaveBuffer = 1 };

void setPrintSink(PrintSinkFunc func, void* arg=0, uint8_t flags=0);
PrintSinkFunc printSink();
void* printSinkUserp();

#ifndef NOT_EMBEDDED
void semihostingPrintSink(const char* str, uint32_t len, int fd=1);
#else
void standardPrintSink(const char* str, uint32_t len, int fd=1);
#endif

char* tsnprintf(char* buf, uint32_t bufsize, const char* fmtStr);

template <typename Val, typename ...Args>
char* tsnprintf(char* buf, uint32_t bufsize, const char* fmtStr, Val val, Args... args)
{
    if (!buf)
        return nullptr;
    assert(bufsize);

    char* bufend = buf+bufsize-1; //point to last char
    do
    {
        char ch = *fmtStr;
        if (ch == 0)
        {
            *buf = 0;
            return buf;
        }
        if (ch == '%')
        {
            buf = toString(buf, bufend-buf+1, val);
            return tsnprintf(buf, bufend-buf+1, fmtStr+1, args...);
        }
        *(buf++) = *(fmtStr++);
    }
    while (buf < bufend);
    return nullptr;
}

template <int32_t BufSize, typename... Args>
typename std::enable_if<BufSize < 0, size_t>::type
ftprintf(uint8_t fd, const char* fmtStr, Args... args)
{
    extern PrintSinkFunc gPrintSinkFunc;
    extern void* gPrintSinkUserp;
    char sbuf[BufSize & 0x7fffffff];
    char* ret = tsnprintf(sbuf, BufSize, fmtStr, args...);
    size_t size = ret-sbuf;
    gPrintSinkFunc(sbuf, size, fd, gPrintSinkUserp);
    return size;
}

template <int32_t BufSize=64, typename... Args>
typename std::enable_if<BufSize >= 0, size_t>::type
ftprintf(uint8_t fd, const char* fmtStr, Args... args)
{
    extern PrintSinkFunc gPrintSinkFunc;
    extern void* gPrintSinkUserp;
    extern uint8_t gPrintSinkFlags;
    size_t bufsize = BufSize;

    char* sbuf;
    char* buf;
    if (gPrintSinkFlags & kPrintSinkLeaveBuffer)
    {
        sbuf = nullptr;
        buf = (char*)malloc(BufSize);
    }
    else
    {
        buf = sbuf = (char*)alloca(BufSize);
    }
    char* ret;
    for(;;)
    {
        ret = tsnprintf(buf, bufsize, fmtStr, args...);
        if (ret)
            break;

        //have to increase buf size
        bufsize *= 2;
        if (bufsize > 10240)
        {
            //too much, bail out
            if (buf != sbuf)
                free(buf);
            return 0;
        }
        buf = (buf == sbuf)
            ? (char*)malloc(bufsize)
            : (char*)realloc(buf, bufsize);
        if (!buf)
            return 0;
    }
    uint32_t size = ret-buf;
    gPrintSinkFunc(buf, size, fd, gPrintSinkUserp);
    if ((buf != sbuf) && ((gPrintSinkFlags & kPrintSinkLeaveBuffer) == 0))
        free(buf);
    return size;
}

/** Use this to free the buffer passed to the print sink in case the
 * kPrintSinkLeaveBuffer flag is set
 */
static inline void tprintf_free(void* ptr)
{
    free(ptr);
}

template <int32_t BufSize=64, typename ...Args>
uint16_t tprintf(const char* fmtStr, Args... args)
{
    return ftprintf<BufSize>(1, fmtStr, args...);
}

static inline void puts(const char* str, uint16_t len)
{
    extern PrintSinkFunc gPrintSinkFunc;
    extern void* gPrintSinkUserp;
    gPrintSinkFunc(str, len, 1, gPrintSinkUserp);
}

#endif
