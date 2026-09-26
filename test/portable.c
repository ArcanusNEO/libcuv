#include "cuv.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct portable_child
{
  struct cuv_task task;
  unsigned done;
};

static void
portable_child_run (struct cuv_task *task)
    async$ ((struct portable_child *)task)
{
  await$ (cuv_sleep, 1);
  $.done = 1;
  exit$ ();
}

struct portable_test
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct portable_child children[4];
  struct cuv_file file;
  char path[1024];
  char output[64];
  char input[64];
  unsigned char random[32];
  isz size;
};

static void
portable_test_run (struct cuv_task *task) async$ ((struct portable_test *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);

  for (usz index = 0; index < 4; ++index)
    {
      cuv_task_init (&$.children[index].task, task->loop, portable_child_run);
      assert (cuv_task_group_start (&$.group, &$.children[index].task) == 0);
    }

  await$ (cuv_task_group_wait, &$.group);

  for (usz index = 0; index < 4; ++index)
    assert ($.children[index].done);

  await$ (cuv_random, $.random, sizeof ($.random), 0);

  memcpy ($.output, "portable libcuv\n", 15);
  await$ (cuv_fs_open, &$.file, $.path,
          UV_FS_O_CREAT | UV_FS_O_TRUNC | UV_FS_O_RDWR, 0600);
  await$ (cuv_fs_write, &$.size, &$.file, $.output, 15, 0);
  assert ($.size == 15);
  await$ (cuv_fs_read, &$.size, &$.file, $.input, 15, 0);
  assert ($.size == 15);
  assert (memcmp ($.input, $.output, 15) == 0);
  await$ (cuv_fs_close, &$.file);
  await$ (cuv_fs_unlink, $.path);
  exit$ ();
}

int
main (void)
{
  struct cuv_loop loop;
  struct portable_test test = { 0 };
  char temporary[768];
  u64 now;
  int backend_fd;
  int backend_timeout;
  usz size = sizeof (temporary);

  assert (uv_os_tmpdir (temporary, &size) == 0);
  assert ((size_t)snprintf (test.path, sizeof (test.path),
                            "%s/libcuv-portable-%lld.tmp", temporary,
                            (long long)uv_os_getpid ())
          < sizeof (test.path));

  assert (cuv_loop_init (&loop) == 0);
  assert (cuv_loop_alive (&loop) == 0);
  assert (cuv_loop_update_time (&loop) == 0);
  assert (cuv_loop_now (&loop, &now) == 0);
  assert (cuv_loop_backend_fd (&loop, &backend_fd) == 0);
  assert (backend_fd >= -1);
  assert (cuv_loop_backend_timeout (&loop, &backend_timeout) == 0);
  assert (backend_timeout >= -1);
  cuv_task_init (&test.task, &loop, portable_test_run);
  cuv_task_start (&test.task);
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (cuv_loop_alive (&loop) == 0);
  assert (cuv_debug_check_task (&test.task) == 0);
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);

  puts ("libcuv portable smoke passed");
  return 0;
}
