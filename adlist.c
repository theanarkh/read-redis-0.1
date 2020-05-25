/* adlist.c - A generic doubly linked list implementation
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
#include "adlist.h"
#include "zmalloc.h"

/* Create a new list. The created list can be freed with
 * AlFreeList(), but private value of every node need to be freed
 * by the user before to call AlFreeList().
 *
 * On error, NULL is returned. Otherwise the pointer to the new list. */
// 新建一个链表头结点
list *listCreate(void)
{
    struct list *list;

    if ((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;
    // 空链表，还没有节点
    list->head = list->tail = NULL;
    // 链表中的节点数
    list->len = 0;
     // 复制节点的函数
    list->dup = NULL;
    // 释放节点value域的函数
    list->free = NULL;
    // 匹配节点的函数
    list->match = NULL;
    return list;
}

/* Free the whole list.
 *
 * This function can't fail. */
// 释放一个链表
void listRelease(list *list)
{
    unsigned int len;
    listNode *current, *next;
    // 第一个节点
    current = list->head;
    // 链表中的节点数
    len = list->len;
    while(len--) {
        next = current->next;
        // 定义了free函数，则执行
        if (list->free) list->free(current->value);
        // 释放节点内存
        zfree(current);
        current = next;
    }
    // 释放链表内存
    zfree(list);
}

/* Add a new node to the list, to head, contaning the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
// 给链表新增一个节点，头插法
list *listAddNodeHead(list *list, void *value)
{
    listNode *node;
    // 分配一个新的listNode节点
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    // 插入的是第一个节点
    if (list->len == 0) {
        // 头尾指针指向第一个节点
        list->head = list->tail = node;
        // 第一个节点前后指针为空
        node->prev = node->next = NULL;
    } else {
        // 插入的不是第一个节点，头插法
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    // 节点数加一
    list->len++;
    return list;
}

/* Add a new node to the list, to tail, contaning the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
// 给链表新增一个节点，尾插法
list *listAddNodeTail(list *list, void *value)
{
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }
    list->len++;
    return list;
}

/* Remove the specified node from the specified list.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail. */
// 删除节点node
void listDelNode(list *list, listNode *node)
{
    // 有前指针说明不是第一个节点，则前节点的next指针指向node的第一个节点
    if (node->prev)
        node->prev->next = node->next;
    else
        // 前指针为空说明是第一个节点，更新头节点头指针的指向，指向node的下一个节点
        list->head = node->next;
    // next非空说明不是最后一个节点，更新node下一个节点的前指针为node的前一个节点
    if (node->next)
        node->next->prev = node->prev;
    else
        // 删除的是最后一个节点，则更新头节点尾指针的指向
        list->tail = node->prev;
    // 定义了free函数
    if (list->free) list->free(node->value);
    // 释放节点的内存
    zfree(node);
    // 节点数减一
    list->len--;
}

/* Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail. */
// 申请一个链表迭代器
listIter *listGetIterator(list *list, int direction)
{
    listIter *iter;
    
    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;
    // 从头还是从尾开始遍历
    if (direction == AL_START_HEAD)
        iter->next = list->head;
    else
        iter->next = list->tail;
    iter->direction = direction;
    return iter;
}

/* Release the iterator memory */
// 释放一个迭代器
void listReleaseIterator(listIter *iter) {
    zfree(iter);
}

/* Create an iterator in the list private iterator structure */
// 重置链表的迭代器，从头开始
void listRewind(list *list) {
    list->iter.next = list->head;
    list->iter.direction = AL_START_HEAD;
}
// 重置链表的迭代器，从尾巴开始
void listRewindTail(list *list) {
    list->iter.next = list->tail;
    list->iter.direction = AL_START_TAIL;
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage patter
 * is:
 *
 * iter = listGetItarotr(list,<direction>);
 * while ((node = listNextIterator(iter)) != NULL) {
 *     DoSomethingWith(listNodeValue(node));
 * }
 *
 * */
// 一次迭代，返回链表中的一个节点
listNode *listNext(listIter *iter)
{
    listNode *current = iter->next;

    if (current != NULL) {
        if (iter->direction == AL_START_HEAD)
            iter->next = current->next;
        else
            iter->next = current->prev;
    }
    return current;
}

/* List Yield just call listNext() against the list private iterator */
// 一次迭代，同上
listNode *listYield(list *list) {
    return listNext(&list->iter);
}

/* Duplicate the whole list. On out of memory NULL is returned.
 * On success a copy of the original list is returned.
 *
 * The 'Dup' method set with listSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original list both on success or error is never modified. */
// 复制一个链表
list *listDup(list *orig)
{
    list *copy;
    listIter *iter;
    listNode *node;
    // 申请一个链表头阶段
    if ((copy = listCreate()) == NULL)
        return NULL;
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
    // 申请一个新的链表头节点
    iter = listGetIterator(orig, AL_START_HEAD);
    while((node = listNext(iter)) != NULL) {
        void *value;
        // 定义了复制函数，比如深度复制
        if (copy->dup) {
            value = copy->dup(node->value);
            // 复制出错，释放刚才申请的内存
            if (value == NULL) {
                listRelease(copy);
                listReleaseIterator(iter);
                return NULL;
            }
        } else
            // 默认浅复制
            value = node->value;
        // 插入新的链表
        if (listAddNodeTail(copy, value) == NULL) {
            listRelease(copy);
            listReleaseIterator(iter);
            return NULL;
        }
    }
    // 用完了，释放迭代器
    listReleaseIterator(iter);
    return copy;
}

/* Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */
// 遍历链表，查找key对应的节点
listNode *listSearchKey(list *list, void *key)
{
    listIter *iter;
    listNode *node;

    iter = listGetIterator(list, AL_START_HEAD);
    while((node = listNext(iter)) != NULL) {
        // 定义了match函数
        if (list->match) {
            if (list->match(node->value, key)) {
                listReleaseIterator(iter);
                return node;
            }
        } else {
            // 默认比较内存地址
            if (key == node->value) {
                listReleaseIterator(iter);
                return node;
            }
        }
    }
    listReleaseIterator(iter);
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimante
 * and so on. If the index is out of range NULL is returned. */
// 返回链表的第index个字节
listNode *listIndex(list *list, int index) {
    listNode *n;
    // index小于0，则-1是最后一个节点
    if (index < 0) {
        /*
                    |
            ---------------------
                    |
         -2   -1    0   1   2
        假设index=-2，-index则为2，-index-1则为1。即向前走1个节点，
        因为初始化时n指向了尾节点，所以这时候返回的是倒数第二个节点
        */  
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else {
        // 索引为0对应的节点（第一个节点），如果只有2个节点，index大于节点数，则返回最后一个节点
        n = list->head;
        while(index-- && n) n = n->next;
    }
    return n;
}
