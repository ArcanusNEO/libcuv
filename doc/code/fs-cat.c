#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <cuv.h>

struct cat
{
  struct cuv_task task;
  struct cuv_file file;
  char const *path;
  char buffer[4096];
  isz size;
  i64 offset;
};

static void
cat_run (struct cuv_task *task) async$ ((struct cat *)task)
{
  await$ (cuv_fs_open, &$.file, $.path, O_RDONLY, 0);

  for (;;)
    {
      await$ (cuv_fs_read, &$.size, &$.file, $.buffer, sizeof ($.buffer),
              $.offset);
      if (!$.size)
        break;

      $.offset += $.size;

      char *cursor = $.buffer;
      isz remaining = $.size;
      while (remaining)
        {
          ssize_t written = write (STDOUT_FILENO, cursor, remaining);
          if (written <= 0)
            {
              task->error = uv_translate_sys_error (errno);
              loop$ ();
            }
          cursor += written;
          remaining -= written;
        }
    }

  await$ (cuv_fs_close, &$.file);
  exit$ ();
}

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "usage: %s FILE\n", argv[0]);
      return 2;
    }

  struct cuv_loop loop = { 0 };
  struct cat cat = { .path = argv[1] };

  int error = cuv_loop_init (&loop);
  if (error < 0)
    return 1;

  cuv_task_init (&cat.task, &loop, cat_run);
  cuv_task_start (&cat.task);

  error = cuv_loop_run (&loop);
  if (error == 0)
    error = cuv_task_error (&cat.task);

  int close_error = cuv_loop_close (&loop);
  if (error == 0)
    error = close_error;

  if (error < 0)
    fprintf (stderr, "libcuv: %s (%d)\n", uv_strerror (error), error);

  return error < 0;
}
