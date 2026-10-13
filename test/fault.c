#include "../src/intern.h"

#include <assert.h>
#include <stdio.h>

struct fault_sleep
{
  struct cuv_task task;
};

static void
fault_sleep_run (struct cuv_task *task) async$ ((struct fault_sleep *)task)
{
  await$ (cuv_sleep, 1);
  exit$ ();
}

static void
fault_timer_allocation (void)
{
  struct cuv_loop loop;
  struct fault_sleep test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_fault_reset ();
  cuv_fault_fail_after (CUV_FAULT_ALLOC, 0, UV_ENOMEM);
  cuv_task_init (&test.task, &loop, fault_sleep_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == UV_ENOMEM);
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

static void
fault_timer_rearm (void)
{
  struct cuv_loop loop;
  struct fault_sleep test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_fault_reset ();
  cuv_fault_fail_after (CUV_FAULT_TIMER_REARM, 0, UV_EIO);
  cuv_task_init (&test.task, &loop, fault_sleep_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == UV_EIO);
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

struct fault_scandir
{
  struct cuv_task task;
  struct cuv_dirlist list;
};

static void
fault_scandir_run (struct cuv_task *task) async$ ((struct fault_scandir *)task)
{
  await$ (cuv_fs_scandir, &$.list, ".", 0);
  exit$ ();
}

static void
fault_scandir_allocation (usz successful_hits)
{
  struct cuv_loop loop;
  struct fault_scandir test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_fault_reset ();
  cuv_fault_fail_after (CUV_FAULT_ALLOC, successful_hits, UV_ENOMEM);
  cuv_task_init (&test.task, &loop, fault_scandir_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == UV_ENOMEM);
  assert (!test.list.entries);
  assert (!test.list.resource.owner);
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

struct fault_addrlist
{
  struct cuv_task task;
  struct cuv_addrlist list;
};

static void
fault_addrlist_run (struct cuv_task *task)
    async$ ((struct fault_addrlist *)task)
{
  await$ (cuv_getaddrinfo, &$.list, "127.0.0.1", null, null);
  exit$ ();
}

static void
fault_addrlist_allocation (void)
{
  struct cuv_loop loop;
  struct fault_addrlist test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_fault_reset ();
  cuv_fault_fail_after (CUV_FAULT_ALLOC, 0, UV_ENOMEM);
  cuv_task_init (&test.task, &loop, fault_addrlist_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == UV_ENOMEM);
  assert (!test.list.storage);
  assert (!test.list.entries);
  assert (!test.list.resource.owner);
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

static void
fault_invariant_detection (void)
{
  struct cuv_loop loop;
  struct fault_sleep task = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&task.task, &loop, fault_sleep_run);
  assert (cuv_debug_check_task (&task.task) == 0);
  assert (cuv_debug_check_loop (&loop) == 0);

  ++loop.ready_count;
  assert (cuv_debug_check_loop (&loop) == UV_EINVAL);
  --loop.ready_count;
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

int
main (void)
{
  fault_timer_allocation ();
  fault_timer_rearm ();
  fault_scandir_allocation (0);
  fault_scandir_allocation (1);
  fault_addrlist_allocation ();
  fault_invariant_detection ();
  puts ("libcuv fault injection passed");
  return 0;
}
