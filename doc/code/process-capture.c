#include "cuv.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

struct app
{
  struct cuv_task task;
  struct cuv_process_run run;
  char const *executable;
  char output[128];
  char error_output[128];
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
    .output = $.output,
    .output_capacity = sizeof ($.output),
    .error_output = $.error_output,
    .error_output_capacity = sizeof ($.error_output),
    .kill_signal = SIGTERM,
  };

  await$ (cuv_process_run, &$.run, &options);

  printf ("stdout: %.*s\n", (int)$.run.output_size, $.output);
  printf ("stderr: %.*s\n", (int)$.run.error_output_size, $.error_output);
  printf ("exit: %lld\n", (long long)$.run.exit_status);
  exit$ ();
}

int
main (int argc, char **argv)
{
  if (argc > 1 && strcmp (argv[1], "--child") == 0)
    {
      fputs ("hello from child", stdout);
      fputs ("child warning", stderr);
      return 3;
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

  int close_error = cuv_loop_close (&loop);
  if (error == 0)
    error = close_error;

  return error < 0;
}
