#include <cuv.h>

enum
{
  BENCH_TASKS = 256,
  BENCH_YIELDS = 4096
};

struct bench_task
{
  struct cuv_task task;
  usz remaining;
};

static void
bench_run (struct cuv_task *task) async$ ((struct bench_task *)task)
{
  while ($.remaining)
    {
      --$.remaining;
      yield$ ();
    }
  exit$ ();
}

int
main ()
{
  struct cuv_loop loop = { 0 };
  struct bench_task *tasks = calloc (BENCH_TASKS, sizeof (struct bench_task));
  if (!tasks)
    return 1;

  int error = cuv_loop_init (&loop);
  if (error < 0)
    {
      free (tasks);
      return 1;
    }

  for (usz i = 0; i < BENCH_TASKS; ++i)
    {
      tasks[i].remaining = BENCH_YIELDS;
      cuv_task_init (&tasks[i].task, &loop, bench_run);
      cuv_task_start (&tasks[i].task);
    }

  u64 begin = uv_hrtime ();
  error = cuv_loop_run (&loop);
  u64 elapsed = uv_hrtime () - begin;

  if (error == 0)
    for (usz i = 0; i < BENCH_TASKS; ++i)
      if (cuv_task_error (&tasks[i].task) < 0)
        {
          error = cuv_task_error (&tasks[i].task);
          break;
        }

  int close_error = cuv_loop_close (&loop);
  if (error == 0)
    error = close_error;

  if (error < 0)
    {
      fprintf (stderr, "benchmark failed: %s\n", uv_strerror (error));
      free (tasks);
      return 1;
    }

  u64 dispatches = (u64)BENCH_TASKS * ((u64)BENCH_YIELDS + 1);
  double seconds = (double)elapsed / 1000000000.0;
  double rate = seconds > 0 ? (double)dispatches / seconds : 0;

  printf ("tasks=%u yields/task=%u dispatches=%llu elapsed=%.6f s "
          "rate=%.0f dispatch/s\n",
          BENCH_TASKS, BENCH_YIELDS, (unsigned long long)dispatches, seconds,
          rate);

  free (tasks);
}
