#include "cuv.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

struct app
{
  struct cuv_task task;
  struct cuv_process_run run;
  char const *executable;
};

static void
app_run (struct cuv_task *task) async$ ((struct app *)task)
{
  char *args[] = { (char *)$.executable, "--child", null };
  struct cuv_process_run_options options = {
    .process = {
      .file = args[0],
      .args = args,
    },
    .kill_signal = SIGTERM,
  };

  await$ (25, cuv_process_run, &$.run, &options);
  exit$ ();
}

int
main (int argc, char **argv)
{
  if (argc > 1 && strcmp (argv[1], "--child") == 0)
    {
      uv_sleep (10000);
      return 0;
    }

  struct cuv_loop loop;
  struct app app = { .executable = argv[0] };

  if (cuv_loop_init (&loop) < 0)
    return 1;

  cuv_task_init (&app.task, &loop, app_run);
  cuv_task_start (&app.task);

  int error = cuv_loop_run (&loop);
  if (error == 0)
    error = cuv_task_error (&app.task);

  if (error == UV_ETIMEDOUT && app.run.process.state == CUV_PROCESS_CLOSED)
    {
      puts ("process timed out and was reaped");
      error = 0;
    }

  int close_error = cuv_loop_close (&loop);
  if (error == 0)
    error = close_error;

  return error < 0;
}
