#include "cuv.h"

struct sleeper
{
  struct cuv_task task;
};

static void
sleeper_run (struct cuv_task *task) async$ ((struct sleeper *)task)
{
  await$ (cuv_sleep, 10);
  exit$ ();
}

struct observer
{
  struct cuv_task task;
  struct cuv_task *target;
};

static void
observer_run (struct cuv_task *task) async$ ((struct observer *)task)
{
  struct cuv_task_diagnostics diagnostics;
  assert (cuv_task_inspect ($.target, &diagnostics) == 0);

  printf ("target: state=%s wait=%s resources=%zu\n",
          cuv_task_state_name (diagnostics.state),
          diagnostics.operation_name ? diagnostics.operation_name : "none",
          diagnostics.resource_count);
  assert (cuv_debug_dump_loop (stdout, task->loop) == 0);
  exit$ ();
}

int
main ()
{
  struct cuv_loop loop;
  struct sleeper sleeper = { 0 };
  struct observer observer = {
    .target = &sleeper.task,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&sleeper.task, &loop, sleeper_run);
  cuv_task_init (&observer.task, &loop, observer_run);
  cuv_task_start (&sleeper.task);
  cuv_task_start (&observer.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);
  return 0;
}
