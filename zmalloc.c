/* zmalloc - total amount of allocated memory aware version of malloc()
 *
 * Copyright (c) 2006-2009, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include <stdlib.h>
#include <string.h>

static size_t used_memory = 0;
/*
    分配sizeof(size_t)+size大小的内存，前面sizeof(size_t)个字节记录本次分配的大小，
    记录分配的总内存大小，返回用于存储数据的内存首地址，即跨过sizeof(size_t)大小个字节
*/
void *zmalloc(size_t size) {
    void *ptr = malloc(size+sizeof(size_t));

    if (!ptr) return NULL;
    *((size_t*)ptr) = size;
    used_memory += size+sizeof(size_t);
    return (char*)ptr+sizeof(size_t);
}
// 重新分配内存，ptr是旧数据的内存首地址，size是本次需要分片的内存大小
void *zrealloc(void *ptr, size_t size) {
    void *realptr;
    size_t oldsize;
    void *newptr;
    // ptr为空即没有旧数据，新申请一块内存即可，不涉及数据迁移
    if (ptr == NULL) return zmalloc(size);
    // 旧数据占据的内存大小
    realptr = (char*)ptr-sizeof(size_t);
    // 得到数据部分的内存大小
    oldsize = *((size_t*)realptr);
    // 以旧数据的内存地址为基地址，重新分配size+sizeof(size_t)大小的内存
    newptr = realloc(realptr,size+sizeof(size_t));
    if (!newptr) return NULL;
    // 记录数据部分的内存大小
    *((size_t*)newptr) = size;
    // 重新计算已分配内存的总大小，sizeof(size_t)这块内存仍然在使用，不需要计算
    used_memory -= oldsize;
    used_memory += size;
    // 返回存储数据的内存首地址
    return (char*)newptr+sizeof(size_t);
}

void zfree(void *ptr) {
    void *realptr;
    size_t oldsize;

    if (ptr == NULL) return;
    // 算出真正的内存首地址
    realptr = (char*)ptr-sizeof(size_t);
    oldsize = *((size_t*)realptr);
    // 减去释放的内存大小
    used_memory -= oldsize+sizeof(size_t);
    free(realptr);
}
// 复制字符串
char *zstrdup(const char *s) {
    size_t l = strlen(s)+1;
    char *p = zmalloc(l);

    memcpy(p,s,l);
    return p;
}

size_t zmalloc_used_memory(void) {
    return used_memory;
}
