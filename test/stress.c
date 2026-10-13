#include "cuv.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STRESS_CHILDREN 32

struct stress_child
{
  struct cuv_task task;
  unsigned steps;
  unsigned step;
  unsigned delay;
  unsigned completions;
};

static void
stress_child_run (struct cuv_task *task) async$ ((struct stress_child *)task)
{
  while ($.step < $.steps)
    {
      if (($.step & 1) == 0)
        yield$ ();
      else
        await$ (cuv_sleep, $.delay);

      ++$.step;
      ++$.completions;
    }

  exit$ ();
}

struct stress_test
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct stress_child children[STRESS_CHILDREN];
  struct cuv_task *winner;
  uint64_t random;
  usz rounds;
  usz round;
  usz completions;
};

static uint64_t
stress_random (struct stress_test *test)
{
  u64 value = test->random;
  value ^= value << 13;
  value ^= value >> 7;
  value ^= value << 17;
  test->random = value;
  return value;
}

static void
stress_test_run (struct cuv_task *task) async$ ((struct stress_test *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);

  while ($.round < $.rounds)
    {
      for (usz index = 0; index < STRESS_CHILDREN; ++index)
        {
          struct stress_child *child = &$.children[index];
          cuv_task_init (&child->task, task->loop, stress_child_run);
          child->steps = 1 + stress_random (&$) % 7;
          child->step = 0;
          child->delay = stress_random (&$) % 3;
          child->completions = 0;
          assert (cuv_task_group_start (&$.group, &child->task) == 0);
        }

      if (stress_random (&$) & 1)
        {
          $.winner = null;
          await$ (cuv_task_group_race, &$.group, &$.winner);
          assert ($.winner);
          assert (cuv_task_error ($.winner) == 0);
        }
      else
        await$ (cuv_task_group_wait, &$.group);

      assert ($.group.count == 0);
      assert (cuv_debug_check_group (&$.group) == 0);
      assert (cuv_debug_check_loop (task->loop) == 0);

      for (usz index = 0; index < STRESS_CHILDREN; ++index)
        $.completions += $.children[index].completions;

      ++$.round;
    }

  exit$ ();
}

static usz
stress_env_usz (char const *name, usz fallback)
{
  char const *value = getenv (name);
  if (!value || !*value)
    return fallback;

  char *end = null;
  unsigned long long parsed = strtoull (value, &end, 0);
  if (!end || *end || !parsed || parsed > SIZE_MAX)
    return fallback;

  return parsed;
}

int
main (void)
{
  struct cuv_loop loop;
  struct stress_test test = { 0 };

  test.rounds = stress_env_usz ("CUV_STRESS_ITERS", 500);
  test.random = stress_env_usz ("CUV_STRESS_SEED", 0xc0ffee123456789ULL);
  if (!test.random)
    test.random = 1;

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, stress_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.round == test.rounds);
  assert (cuv_debug_check_task (&test.task) == 0);
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);

  printf ("libcuv stress passed seed=%llu rounds=%zu completions=%zu\n",
          (unsigned long long)test.random, test.rounds, test.completions);
  return 0;
}
