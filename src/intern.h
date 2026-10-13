#pragma once
#ifndef INTERN_H
#define INTERN_H 1

#include "cmacs.h"
#include "../include/cuv.h"

enum cuv_fault_point
{
  CUV_FAULT_ALLOC,
  CUV_FAULT_TIMER_REARM,
  CUV_FAULT_COUNT
};

#ifdef CUV_ENABLE_FAULT_INJECTION
int cuv_fault_check (enum cuv_fault_point point);
void *cuv_malloc (usz size);
void *cuv_calloc (usz count, usz size);
void *cuv_realloc (void *memory, usz size);
void cuv_fault_reset ();
void cuv_fault_fail_after (enum cuv_fault_point point, usz successful_hits,
                           int error);
#else
static inline int
cuv_fault_check (enum cuv_fault_point point)
{
  (void)point;
  return 0;
}

static inline void *
cuv_malloc (usz size)
{
  return malloc (size);
}

static inline void *
cuv_calloc (usz count, usz size)
{
  return calloc (count, size);
}

static inline void *
cuv_realloc (void *memory, usz size)
{
  return realloc (memory, size);
}
#endif

static inline void
cuv_list_init (struct lsnod *head)
{
  head->prev = head;
  head->next = head;
}

static inline int
cuv_list_empty (struct lsnod const *head)
{
  return head->next == head;
}

static inline struct cuv_task *
cuv_task_from_req (void *ptr)
{
  return container_of (ptr, struct cuv_task, wait.req);
}

static inline struct cuv_tcp *
cuv_tcp_from_handle (uv_handle_t *handle)
{
  return container_of (handle, struct cuv_tcp, uv);
}

void cuv_task_schedule (struct cuv_task *task);
void cuv_task_wake (struct cuv_task *task, int error);
void cuv_task_finish (struct cuv_task *task);
void cuv_task_cleanup_resume (struct cuv_task *task, int error);
int cuv_task_abort (struct cuv_task *task, int error);
void cuv_task_group_child_complete (struct cuv_task *task);

void cuv_resource_attach (struct cuv_resource *resource,
                          struct cuv_task *owner);

int cuv_cancel_req (struct cuv_task *task);

int cuv_stream_read (struct cuv_task *task, uv_stream_t *stream,
                     struct cuv_task **reader, isz *size, void *buffer,
                     usz capacity);
int cuv_stream_write (struct cuv_task *task, uv_stream_t *stream,
                      void const *buffer, usz length);
int cuv_stream_try_write (isz *size, uv_stream_t *stream, void const *buffer,
                          usz length);
int cuv_stream_write_queue (usz *size, uv_stream_t *stream);
int cuv_stream_is_readable (int *readable, uv_stream_t *stream);
int cuv_stream_is_writable (int *writable, uv_stream_t *stream);
int cuv_stream_set_blocking (uv_stream_t *stream, int blocking);
int cuv_stream_write2 (struct cuv_task *task, uv_stream_t *stream,
                       void const *buffer, usz length,
                       uv_stream_t *send_handle);
int cuv_stream_try_write2 (isz *size, uv_stream_t *stream, void const *buffer,
                           usz length, uv_stream_t *send_handle);
int cuv_stream_shutdown (struct cuv_task *task, uv_stream_t *stream);

int cuv_tcp_init_owned (struct cuv_task *task, struct cuv_tcp *tcp);
int cuv_udp_init_owned (struct cuv_task *task, struct cuv_udp *udp);

#endif /* INTERN_H */
