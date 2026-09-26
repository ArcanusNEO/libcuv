#include "intern.h"

#include <limits.h>

static usz
cuv_process_run_chunk (usz size)
{
  return size > UINT_MAX ? UINT_MAX : size;
}

static void
cuv_process_run_wait_task (struct cuv_task *task)
    async$ (task;
            auto run = container_of (task, struct cuv_process_run, wait_task))
{
  await$ (cuv_process_wait, &run->process, &run->exit_status,
          &run->term_signal);
  exit$ ();
}

static void
cuv_process_run_input_task (struct cuv_task *task)
    async$ (task;
            auto run = container_of (task, struct cuv_process_run, input_task))
{
  while (run->input_offset < run->input_size)
    {
      await$ (cuv_pipe_write, &run->input_pipe,
              (char const *)run->input + run->input_offset,
              cuv_process_run_chunk (run->input_size - run->input_offset));

      run->input_offset
          += cuv_process_run_chunk (run->input_size - run->input_offset);
    }

  await$ (cuv_pipe_shutdown, &run->input_pipe);
  await$ (cuv_pipe_close, &run->input_pipe);
  exit$ ();
}

static void
cuv_process_run_output_task (struct cuv_task *task)
    async$ (task; auto run
                  = container_of (task, struct cuv_process_run, output_task))
{
  for (;;)
    {
      if (run->output_size < run->output_capacity)
        await$ (
            cuv_pipe_read, &run->output_read_size, &run->output_pipe,
            (char *)run->output + run->output_size,
            cuv_process_run_chunk (run->output_capacity - run->output_size));
      else
        await$ (cuv_pipe_read, &run->output_read_size, &run->output_pipe,
                run->output_scratch, sizeof (run->output_scratch));

      if (!run->output_read_size)
        break;

      if (run->output_size < run->output_capacity)
        run->output_size += run->output_read_size;
      else
        run->output_truncated = 1;
    }

  await$ (cuv_pipe_close, &run->output_pipe);
  exit$ ();
}

static void
cuv_process_run_error_task (struct cuv_task *task)
    async$ (task;
            auto run = container_of (task, struct cuv_process_run, error_task))
{
  for (;;)
    {
      if (run->error_output_size < run->error_output_capacity)
        await$ (cuv_pipe_read, &run->error_read_size, &run->error_pipe,
                (char *)run->error_output + run->error_output_size,
                cuv_process_run_chunk (run->error_output_capacity
                                       - run->error_output_size));
      else
        await$ (cuv_pipe_read, &run->error_read_size, &run->error_pipe,
                run->error_output_scratch, sizeof (run->error_output_scratch));

      if (!run->error_read_size)
        break;

      if (run->error_output_size < run->error_output_capacity)
        run->error_output_size += run->error_read_size;
      else
        run->error_output_truncated = 1;
    }

  await$ (cuv_pipe_close, &run->error_pipe);
  exit$ ();
}

static int
cuv_cancel_process_run (struct cuv_task *task)
{
  struct cuv_process_run *run = task->wait.operation.process_run.run;

  if (!run || run->group.waiter != task)
    return UV_EBUSY;

  /* With no kill policy, keep draining stdio and waiting for the child.
     The parent remains a group waiter and will wake only after natural
     completion, with its recorded timeout/cancellation error. */
  if (run->kill_signal == 0)
    return 0;

  if (run->process.state == CUV_PROCESS_RUNNING)
    run->kill_error = cuv_process_kill (&run->process, run->kill_signal);

  int error = cuv_task_group_cancel (&run->group);
  if (error < 0 && error != UV_EALREADY && error != UV_EBUSY)
    return error;

  return 0;
}

static int
cuv_process_run_start_children (struct cuv_process_run *run)
{
  int error = cuv_task_group_start (&run->group, &run->wait_task);
  if (error < 0)
    return error;

  if (run->input_active)
    {
      error = cuv_task_group_start (&run->group, &run->input_task);
      if (error < 0)
        return error;
    }

  if (run->output_active)
    {
      error = cuv_task_group_start (&run->group, &run->output_task);
      if (error < 0)
        return error;
    }

  if (run->error_output_active)
    {
      error = cuv_task_group_start (&run->group, &run->error_task);
      if (error < 0)
        return error;
    }

  return 0;
}

int
cuv_process_run (struct cuv_task *task, struct cuv_process_run *run,
                 struct cuv_process_run_options const *options)
{
  if (!task || !run || !options || !options->process.file)
    return UV_EINVAL;

  if (run->initialized)
    return UV_EALREADY;

  if ((!options->input && options->input_size)
      || (!options->output && options->output_capacity)
      || (!options->error_output && options->error_output_capacity)
      || options->kill_signal < 0)
    return UV_EINVAL;

  /* The convenience layer owns child stdio 0, 1, and 2. */
  if (options->process.stdio || options->process.stdio_count)
    return UV_EINVAL;

  memset (run, 0, sizeof (*run));
  run->initialized = 1;
  run->input = options->input;
  run->input_size = options->input_size;
  run->output = options->output;
  run->output_capacity = options->output_capacity;
  run->error_output = options->error_output;
  run->error_output_capacity = options->error_output_capacity;
  run->kill_signal = options->kill_signal;

  run->input_active = options->input != null;
  run->output_active = options->output != null;
  run->error_output_active = options->error_output != null;

  int error = cuv_task_group_init (&run->group, task);
  if (error < 0)
    return error;

  cuv_task_init (&run->wait_task, task->loop, cuv_process_run_wait_task);
  cuv_task_init (&run->input_task, task->loop, cuv_process_run_input_task);
  cuv_task_init (&run->output_task, task->loop, cuv_process_run_output_task);
  cuv_task_init (&run->error_task, task->loop, cuv_process_run_error_task);

  if (run->input_active)
    {
      error = cuv_pipe_init (task, &run->input_pipe, 0);
      if (error < 0)
        return error;
    }

  if (run->output_active)
    {
      error = cuv_pipe_init (task, &run->output_pipe, 0);
      if (error < 0)
        return error;
    }

  if (run->error_output_active)
    {
      error = cuv_pipe_init (task, &run->error_pipe, 0);
      if (error < 0)
        return error;
    }

  uv_stdio_container_t stdio[3] = {
    { .flags = UV_IGNORE },
    { .flags = UV_IGNORE },
    { .flags = UV_IGNORE },
  };

  if (run->input_active)
    {
      stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
      stdio[0].data.stream = (uv_stream_t *)&run->input_pipe.uv;
    }

  if (run->output_active)
    {
      stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
      stdio[1].data.stream = (uv_stream_t *)&run->output_pipe.uv;
    }

  if (run->error_output_active)
    {
      stdio[2].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
      stdio[2].data.stream = (uv_stream_t *)&run->error_pipe.uv;
    }

  uv_process_options_t actual = options->process;
  actual.stdio_count = 3;
  actual.stdio = stdio;

  error = cuv_process_spawn (task, &run->process, &actual);
  if (error < 0)
    return error;

  error = cuv_process_run_start_children (run);
  if (error < 0)
    {
      cuv_task_group_cancel (&run->group);
      return error;
    }

  /* Children cannot run until the current task yields, so transfer after all
     group starts have succeeded. */
  cuv_resource_transfer (&run->process.resource, &run->wait_task);

  if (run->input_active)
    cuv_resource_transfer (&run->input_pipe.resource, &run->input_task);
  if (run->output_active)
    cuv_resource_transfer (&run->output_pipe.resource, &run->output_task);
  if (run->error_output_active)
    cuv_resource_transfer (&run->error_pipe.resource, &run->error_task);

  int status = cuv_task_group_wait (task, &run->group);
  if (status == CUV_PENDING)
    {
      task->wait.operation.process_run.run = run;
      task->wait.cancel = cuv_cancel_process_run;
    }

  return status;
}

int
cuv_process_run_clear (struct cuv_process_run *run)
{
  if (!run || !run->initialized)
    return UV_EINVAL;

  if (run->group.count || run->group.waiter || run->group.closing
      || run->process.resource.owner || run->input_pipe.resource.owner
      || run->output_pipe.resource.owner || run->error_pipe.resource.owner)
    return UV_EBUSY;

  cuv_resource_detach (&run->group.resource);
  memset (run, 0, sizeof (*run));
  return 0;
}
