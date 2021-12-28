/* SDSLib 2.0 -- A C dynamic strings library
 *
 * Copyright (c) 2006-2015, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2015, Oran Agra
 * Copyright (c) 2015, Redis Labs, Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024*1024)
extern const char *SDS_NOINIT;

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

// 定义了一个char 指针
typedef char *sds;

/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */
// lsb 代表有效位的意思，
// sdshdr5不使用，此处仅做占位处理
// attribute ((packed)) 取消编译阶段的内存优化对齐功能
// sds一共有5种类型的header。之所以有5种，是为了能让不同长度的字符串可以使用不同大小的header。这样，短字符串就能使用较小的header，从而节省内存。
//sdshdr5与其它几个header结构不同，它不包含alloc字段，而长度使用flags的高5位来存储。因此，它不能为字符串分配空余空间。如果字符串需要动态增长，那么它就必然要重新分配内存才行。所以说，这种类型的sds字符串更适合存储静态的短字符串（长度小于32）。
struct __attribute__ ((__packed__)) sdshdr5 {
    // 当字符串很小时， `flags` 是一个8 个字节的组合字符，低位 3 bit 是字符串类型，高位 5bit是字符串长度。
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len; /* used  当前已使用的内存空间长度*/
    uint8_t alloc; /* excluding the header and null terminator  分配的内存空间长度,等于buf[]的总长度-1，因为buf有包括一个\  0的结束符*/
    unsigned char flags; /* 3 lsb of type, 5 unused bits   只有3位有效位，因为类型的表示就是0到4，所有这个8位的flags 有5位没有被用到
 */
    char buf[] ;//实际的字符串存在这里
};
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len; /* used */
    uint16_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len; /* used */
    uint32_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len; /* used */
    uint64_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};

// 可以看出SDS_TYPE只占用了0,1,2,3,4五个数字，正好占用三位，我们就可以使用flags&SDS_TYPE_MASK来获取动态字符串对应的字符串类型
#define SDS_TYPE_5  0
#define SDS_TYPE_8  1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
#define SDS_TYPE_MASK 7
#define SDS_TYPE_BITS 3
// 可以看到s本来是指向buf的位置，减去struct的长度正好就是开始的位置
// 然后sh就指向了整个sds开始的位置
// T就是占位符，返回不同类型的struct
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (void*)((s)-(sizeof(struct sdshdr##T)));
//下面这个就是直接返回对应的struct
#define SDS_HDR(T,s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))
//这个类型的可以不关注因为没有被用到
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)

static inline size_t sdslen(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            // 一个字节高 5 位是长度，通过向右移动 3 位获得大小。
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->len;
    }
    return 0;
}

// sdsavail 查询字符串对象空闲内存大小
static inline size_t sdsavail(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            // 小于 32 长度的内存，都是直接申请的，没有空余内存。
            return 0;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}

static inline void sdssetlen(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len = newlen;
            break;
    }
}

static inline void sdsinclen(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                unsigned char newlen = SDS_TYPE_5_LEN(flags)+inc;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len += inc;
            break;
    }
}

/* sdsalloc() = sdsavail() + sdslen() */
static inline size_t sdsalloc(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->alloc;
    }
    return 0;
}

static inline void sdssetalloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            /* Nothing to do, this type has no total allocation info. */
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->alloc = newlen;
            break;
    }
}
// 据字符串长度，分配合适的内存空间，设置数据结构的相关的成员数据
sds sdsnewlen(const void *init, size_t initlen);
sds sdstrynewlen(const void *init, size_t initlen);
// 创建字符串对象
sds sdsnew(const char *init);
sds sdsempty(void);
sds sdsdup(const sds s);
// 释放字符串结构对象
void sdsfree(sds s);
sds sdsgrowzero(sds s, size_t len);
sds sdscatlen(sds s, const void *t, size_t len);
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);
sds sdscpylen(sds s, const char *t, size_t len);
sds sdscpy(sds s, const char *t);

sds sdscatvprintf(sds s, const char *fmt, va_list ap);
#ifdef __GNUC__
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

sds sdscatfmt(sds s, char const *fmt, ...);
sds sdstrim(sds s, const char *cset);
void sdssubstr(sds s, size_t start, size_t len);
void sdsrange(sds s, ssize_t start, ssize_t end);
void sdsupdatelen(sds s);
void sdsclear(sds s);
int sdscmp(const sds s1, const sds s2);
sds *sdssplitlen(const char *s, ssize_t len, const char *sep, int seplen, int *count);
void sdsfreesplitres(sds *tokens, int count);
void sdstolower(sds s);
void sdstoupper(sds s);
sds sdsfromlonglong(long long value);
sds sdscatrepr(sds s, const char *p, size_t len);
sds *sdssplitargs(const char *line, int *argc);
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
sds sdsjoin(char **argv, int argc, char *sep);
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);

/* Callback for sdstemplate. The function gets called by sdstemplate
 * every time a variable needs to be expanded. The variable name is
 * provided as variable, and the callback is expected to return a
 * substitution value. Returning a NULL indicates an error.
 */
typedef sds (*sdstemplate_callback_t)(const sds variable, void *arg);
sds sdstemplate(const char *template, sdstemplate_callback_t cb_func, void *cb_arg);

/* Low level functions exposed to the user API */
sds sdsMakeRoomFor(sds s, size_t addlen);
sds sdsMakeRoomForNonGreedy(sds s, size_t addlen);
void sdsIncrLen(sds s, ssize_t incr);
sds sdsRemoveFreeSpace(sds s);
sds sdsResize(sds s, size_t size);
size_t sdsAllocSize(sds s);
void *sdsAllocPtr(sds s);

/* Export the allocator used by SDS to the program using SDS.
 * Sometimes the program SDS is linked to, may use a different set of
 * allocators, but may want to allocate or free things that SDS will
 * respectively free or allocate. */
void *sds_malloc(size_t size);
void *sds_realloc(void *ptr, size_t size);
void sds_free(void *ptr);

#ifdef REDIS_TEST
int sdsTest(int argc, char *argv[], int flags);
#endif

#endif
