/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
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

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include "ae.h"
#include "zmalloc.h"

aeEventLoop *aeCreateEventLoop(void) {
    aeEventLoop *eventLoop;

    eventLoop = zmalloc(sizeof(*eventLoop));
    if (!eventLoop) return NULL;
    eventLoop->fileEventHead = NULL;
    eventLoop->timeEventHead = NULL;
    eventLoop->timeEventNextId = 0;
    eventLoop->stop = 0;
    return eventLoop;
}

void aeDeleteEventLoop(aeEventLoop *eventLoop) {
    zfree(eventLoop);
}

void aeStop(aeEventLoop *eventLoop) {
    eventLoop->stop = 1;
}

int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc)
{
    aeFileEvent *fe;

    fe = zmalloc(sizeof(*fe));
    if (fe == NULL) return AE_ERR;
    fe->fd = fd;
    fe->mask = mask;
    fe->fileProc = proc;
    fe->finalizerProc = finalizerProc;
    fe->clientData = clientData;
    fe->next = eventLoop->fileEventHead;
    eventLoop->fileEventHead = fe;
    return AE_OK;
}
// 删除某个节点（fd和mask等于入参的节点）
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
{
    aeFileEvent *fe, *prev = NULL;

    fe = eventLoop->fileEventHead;
    while(fe) {
        if (fe->fd == fd && fe->mask == mask) {
            // 说明待删除的节点是第一个节点，直接修改头节点的指针
            if (prev == NULL)
                eventLoop->fileEventHead = fe->next;
            else
                // 修改prev节点的next指针指向当前删除节点的下一个节点
                prev->next = fe->next;
            // 钩子函数
            if (fe->finalizerProc)
                fe->finalizerProc(eventLoop, fe->clientData);
            // 释放待删除节点的内存
            zfree(fe);
            return;
        }
        // 记录上一个节点，当找到待删除节点时，修改prev指针的next指针（如果prev非空）为待删除节点的下一个节点
        prev = fe;
        fe = fe->next;
    }
}
// 获取当前时间，秒和毫秒
static void aeGetTime(long *seconds, long *milliseconds)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec/1000;
}

static void aeAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;
    // 获取
    aeGetTime(&cur_sec, &cur_ms);
    // 绝对时间，秒数
    when_sec = cur_sec + milliseconds/1000;
    // 绝对时间，毫秒数
    when_ms = cur_ms + milliseconds%1000;
    // 大于一秒则进位到秒中
    if (when_ms >= 1000) {
        when_sec ++;
        when_ms -= 1000;
    }
    // 返回绝对时间的秒和毫秒
    *sec = when_sec;
    *ms = when_ms;
}

long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc)
{
    long long id = eventLoop->timeEventNextId++;
    aeTimeEvent *te;

    te = zmalloc(sizeof(*te));
    if (te == NULL) return AE_ERR;
    te->id = id;
    aeAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;
    // 头插法插入eventLoop的timeEventHead队列
    te->next = eventLoop->timeEventHead;
    eventLoop->timeEventHead = te;
    return id;
}
// 删除一个timeEvent节点
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id)
{
    aeTimeEvent *te, *prev = NULL;

    te = eventLoop->timeEventHead;
    while(te) {
        if (te->id == id) {
            if (prev == NULL)
                eventLoop->timeEventHead = te->next;
            else
                prev->next = te->next;
            if (te->finalizerProc)
                te->finalizerProc(eventLoop, te->clientData);
            zfree(te);
            return AE_OK;
        }
        prev = te;
        te = te->next;
    }
    return AE_ERR; /* NO event with the specified ID found */
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted. */
// 找出最快到期的节点
static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop)
{
    aeTimeEvent *te = eventLoop->timeEventHead;
    aeTimeEvent *nearest = NULL;

    while(te) {
        /*
            nearest记录当前最快到期的节点，初始化为NULL
            1 nearest为空，把当前节点作为最小值
            2 when_sec小的作为最小值
            3 when_sec一样的情况下，when_ms小者为最小值
        */
        if (!nearest || te->when_sec < nearest->when_sec ||
                (te->when_sec == nearest->when_sec &&
                 te->when_ms < nearest->when_ms))
            nearest = te;
        te = te->next;
    }
    return nearest;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurrs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 *
 * The function returns the number of events processed. */
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
    int maxfd = 0, numfd = 0, processed = 0;
    fd_set rfds, wfds, efds;
    aeFileEvent *fe = eventLoop->fileEventHead;
    aeTimeEvent *te;
    long long maxId;
    AE_NOTUSED(flags);

    /* Nothing to do? return ASAP */
    // 两种类型的事件都不需要处理
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);

    /* Check file events */
    // 处理文件事件
    if (flags & AE_FILE_EVENTS) {
        while (fe != NULL) {
            // 根据需要处理的事件，设置对应的变量对应的位
            if (fe->mask & AE_READABLE) FD_SET(fe->fd, &rfds);
            if (fe->mask & AE_WRITABLE) FD_SET(fe->fd, &wfds);
            if (fe->mask & AE_EXCEPTION) FD_SET(fe->fd, &efds);
            // 记录最大文件描述符select的时候需要用
            if (maxfd < fe->fd) maxfd = fe->fd;
            numfd++;
            fe = fe->next;
        }
    }
    /* Note that we want call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
    // 有文件事件需要处理，或者有time事件并且没有设置AE_DONT_WAIT（设置的话就不会进入select定时阻塞）标记
    if (numfd || ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
        int retval;
        aeTimeEvent *shortest = NULL;
        /*
            struct timeval {
                long    tv_sec;         // seconds 
                long    tv_usec;        // and microseconds 
            };
        */
        struct timeval tv, *tvp;
        // 有time事件需要处理，并且没有设置AE_DONT_WAIT标记，则select可能会定时阻塞（如果有time节点的话）
        if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
            // 找出最快到期的节点
            shortest = aeSearchNearestTimer(eventLoop);
        // 有待到期的time节点
        if (shortest) {
            long now_sec, now_ms;

            /* Calculate the time missing for the nearest
             * timer to fire. */
            aeGetTime(&now_sec, &now_ms);
            tvp = &tv;
            // 算出相对时间，秒数
            tvp->tv_sec = shortest->when_sec - now_sec;
            // 不够，需要借位
            if (shortest->when_ms < now_ms) {
                // 微秒
                tvp->tv_usec = ((shortest->when_ms+1000) - now_ms)*1000;
                // 借一位，减一
                tvp->tv_sec --;
            } else {
                // 乘以1000，即微秒
                tvp->tv_usec = (shortest->when_ms - now_ms)*1000;
            }
        } else {
            // 没有到期的time节点
            /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to se the timeout
             * to zero */
            // 设置了AE_DONT_WAIT，则不会阻塞在select
            if (flags & AE_DONT_WAIT) {
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                // 一直阻塞直到有事件发生
                /* Otherwise we can block */
                tvp = NULL; /* wait forever */
            }
        }
        
        retval = select(maxfd+1, &rfds, &wfds, &efds, tvp);
        if (retval > 0) {
            fe = eventLoop->fileEventHead;
            while(fe != NULL) {
                int fd = (int) fe->fd;
                // 有感兴趣的事件发生
                if ((fe->mask & AE_READABLE && FD_ISSET(fd, &rfds)) ||
                    (fe->mask & AE_WRITABLE && FD_ISSET(fd, &wfds)) ||
                    (fe->mask & AE_EXCEPTION && FD_ISSET(fd, &efds)))
                {
                    int mask = 0;
                    // 记录发生了哪些感兴趣的事件
                    if (fe->mask & AE_READABLE && FD_ISSET(fd, &rfds))
                        mask |= AE_READABLE;
                    if (fe->mask & AE_WRITABLE && FD_ISSET(fd, &wfds))
                        mask |= AE_WRITABLE;
                    if (fe->mask & AE_EXCEPTION && FD_ISSET(fd, &efds))
                        mask |= AE_EXCEPTION;
                    // 执行回调
                    fe->fileProc(eventLoop, fe->fd, fe->clientData, mask);
                    processed++;
                    /* After an event is processed our file event list
                     * may no longer be the same, so what we do
                     * is to clear the bit for this file descriptor and
                     * restart again from the head. */
                    /*
                        执行完回调后，文件事件队列可能发生了变化，
                        重新开始遍历
                    */
                    fe = eventLoop->fileEventHead;
                    // 清除该文件描述符
                    FD_CLR(fd, &rfds);
                    FD_CLR(fd, &wfds);
                    FD_CLR(fd, &efds);
                } else {
                    fe = fe->next;
                }
            }
        }
    }
    /* Check time events */
    // 处理time事件
    if (flags & AE_TIME_EVENTS) {
        te = eventLoop->timeEventHead;
        // 先保存这次需要处理的最大id，防止在time回调了不断给队列新增节点，导致死循环
        maxId = eventLoop->timeEventNextId-1;
        while(te) {
            long now_sec, now_ms;
            long long id;
            // 在本次回调里新增的节点，跳过
            if (te->id > maxId) {
                te = te->next;
                continue;
            }
            // 获取当前时间
            aeGetTime(&now_sec, &now_ms);
            // 到期了
            if (now_sec > te->when_sec ||
                (now_sec == te->when_sec && now_ms >= te->when_ms))
            {
                int retval;

                id = te->id;
                // 执行回调
                retval = te->timeProc(eventLoop, id, te->clientData);
                /* After an event is processed our time event list may
                 * no longer be the same, so we restart from head.
                 * Still we make sure to don't process events registered
                 * by event handlers itself in order to don't loop forever.
                 * To do so we saved the max ID we want to handle. */
                // 继续注册事件，修改超时时间，否则删除该节点
                if (retval != AE_NOMORE) {
                    aeAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
                } else {
                    aeDeleteTimeEvent(eventLoop, id);
                }
                te = eventLoop->timeEventHead;
            } else {
                te = te->next;
            }
        }
    }
    // 处理的事件个数
    return processed; /* return the number of processed file/time events */
}

/* Wait for millseconds until the given file descriptor becomes
 * writable/readable/exception */
// 等待一个描述描述符的事件就绪
int aeWait(int fd, int mask, long long milliseconds) {
    struct timeval tv;
    fd_set rfds, wfds, efds;
    int retmask = 0, retval;

    tv.tv_sec = milliseconds/1000;
    tv.tv_usec = (milliseconds%1000)*1000;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);

    if (mask & AE_READABLE) FD_SET(fd,&rfds);
    if (mask & AE_WRITABLE) FD_SET(fd,&wfds);
    if (mask & AE_EXCEPTION) FD_SET(fd,&efds);
    if ((retval = select(fd+1, &rfds, &wfds, &efds, &tv)) > 0) {
        if (FD_ISSET(fd,&rfds)) retmask |= AE_READABLE;
        if (FD_ISSET(fd,&wfds)) retmask |= AE_WRITABLE;
        if (FD_ISSET(fd,&efds)) retmask |= AE_EXCEPTION;
        return retmask;
    } else {
        return retval;
    }
}

void aeMain(aeEventLoop *eventLoop)
{
    eventLoop->stop = 0;
    while (!eventLoop->stop)
        aeProcessEvents(eventLoop, AE_ALL_EVENTS);
}
