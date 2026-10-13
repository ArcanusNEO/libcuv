#include <cuv.h>

struct example
{
  struct cuv_task task;
};

static void
example_run (struct cuv_task *task) async$ ((struct example *)task)
{
  await$ (100, cuv_sleep, 10);
  fprintf (stdout, "timer completed\n");
  exit$ ();
}

int
main ()
{
  struct cuv_loop loop = { 0 };
  struct example example = { 0 };

  int error = cuv_loop_init (&loop);
  if (error < 0)
    return 1;

  cuv_task_init (&example.task, &loop, example_run);
  cuv_task_start (&example.task);

  error = cuv_loop_run (&loop);
  if (error == 0)
    error = cuv_task_error (&example.task);

  int close_error = cuv_loop_close (&loop);
  if (error == 0)
    error = close_error;

  return error < 0;
}
