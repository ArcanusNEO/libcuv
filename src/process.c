#include "intern.h"

static void cuv_process_close_cb (uv_handle_t *handle);

static struct cuv_process *
cuv_process_from_handle (uv_handle_t *handle)
{
  return container_of (handle, struct cuv_process, uv);
}

static int
cuv_process_resource_close (struct cuv_resource *resource)
{
  auto process = container_of (resource, struct cuv_process, resource);

  if (!resource->owner)
    return CUV_READY;

  switch (process->state)
    {
    case CUV_PROCESS_NEW:
    case CUV_PROCESS_CLOSED:
      cuv_resource_detach (resource);
      return CUV_READY;

    case CUV_PROCESS_RUNNING:
      /*
       * Do not close a running process handle on Unix.  Doing so before the
       * exit callback would prevent libuv from reaping the child.
       */
      return CUV_PENDING;

    case CUV_PROCESS_EXITED:
    case CUV_PROCESS_FAILED:
      process->state = CUV_PROCESS_CLOSING;
      uv_close ((uv_handle_t *)&process->uv, cuv_process_close_cb);
      return CUV_PENDING;

    case CUV_PROCESS_CLOSING:
      return CUV_PENDING;
    }

  return UV_EINVAL;
}

static void
cuv_process_exit_cb (uv_process_t *handle, int64_t exit_status,
                     int term_signal)
{
  auto process = container_of (handle, struct cuv_process, uv);

  process->exit_status = exit_status;
  process->term_signal = term_signal;
  process->state = CUV_PROCESS_EXITED;

  process->state = CUV_PROCESS_CLOSING;
  uv_close ((uv_handle_t *)handle, cuv_process_close_cb);
}

static void
cuv_process_finish_wait (struct cuv_process *process, struct cuv_task *task)
{
  if (task->wait.operation.process_wait.exit_status)
    *task->wait.operation.process_wait.exit_status = process->exit_status;

  if (task->wait.operation.process_wait.term_signal)
    *task->wait.operation.process_wait.term_signal = process->term_signal;
}

static void
cuv_process_close_cb (uv_handle_t *handle)
{
  struct cuv_process *process = cuv_process_from_handle (handle);
  struct cuv_task *owner = process->resource.owner;
  struct cuv_task *waiter = process->waiter;

  process->waiter = null;
  process->state = CUV_PROCESS_CLOSED;
  cuv_resource_detach (&process->resource);

  if (waiter)
    {
      cuv_process_finish_wait (process, waiter);
      cuv_task_wake (waiter, 0);
      return;
    }

  if (owner && owner->state == CUV_TASK_CLOSING
      && owner->cleanup == &process->resource)
    cuv_task_cleanup_resume (owner, process->spawn_error);
}

static int
cuv_cancel_process_wait (struct cuv_task *task)
{
  struct cuv_process *process = task->wait.operation.process_wait.process;

  if (!process || process->waiter != task)
    return UV_EBUSY;

  process->waiter = null;
  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

int
cuv_process_spawn (struct cuv_task *task, struct cuv_process *process,
                   uv_process_options_t const *options)
{
  if (!task || !process || !options || !options->file)
    return UV_EINVAL;

  if (process->state != CUV_PROCESS_NEW)
    return UV_EBUSY;

  uv_process_options_t actual = *options;
  actual.exit_cb = cuv_process_exit_cb;

  process->resource.close = cuv_process_resource_close;
  process->resource.kind = CUV_RESOURCE_PROCESS;
  process->spawn_error = 0;

  int error = uv_spawn (&task->loop->uv, &process->uv, &actual);

  /*
   * uv_spawn() is unusual among libuv handle constructors: the process
   * handle must be uv_close()'d even when spawn itself fails.  Attach the
   * resource on both paths so ordinary await$ error propagation performs
   * the required cleanup.
   */
  cuv_resource_attach (&process->resource, task);

  if (error < 0)
    {
      process->spawn_error = error;
      process->state = CUV_PROCESS_FAILED;
      return error;
    }

  process->state = CUV_PROCESS_RUNNING;
  return CUV_READY;
}

int
cuv_process_wait (struct cuv_task *task, struct cuv_process *process,
                  i64 *exit_status, int *term_signal)
{
  if (!task || !process)
    return UV_EINVAL;

  if (process->state == CUV_PROCESS_NEW)
    return UV_EINVAL;

  if (process->state == CUV_PROCESS_FAILED)
    return process->spawn_error < 0 ? process->spawn_error : UV_EINVAL;

  if (process->state == CUV_PROCESS_CLOSED)
    {
      if (exit_status)
        *exit_status = process->exit_status;
      if (term_signal)
        *term_signal = process->term_signal;
      return CUV_READY;
    }

  if (process->resource.owner != task)
    return UV_EPERM;

  if (process->waiter)
    return UV_EBUSY;

  process->waiter = task;
  task->wait.operation.process_wait.process = process;
  task->wait.operation.process_wait.exit_status = exit_status;
  task->wait.operation.process_wait.term_signal = term_signal;
  task->wait.cancel = cuv_cancel_process_wait;

  return CUV_PENDING;
}

int
cuv_process_kill (struct cuv_process *process, int signum)
{
  if (!process || signum < 0)
    return UV_EINVAL;

  if (process->state != CUV_PROCESS_RUNNING)
    return UV_ESRCH;

  return uv_process_kill (&process->uv, signum);
}

int
cuv_process_pid (struct cuv_process const *process, uv_pid_t *pid)
{
  if (!process || !pid)
    return UV_EINVAL;

  if (process->state == CUV_PROCESS_NEW
      || process->state == CUV_PROCESS_FAILED)
    return UV_ESRCH;

  *pid = uv_process_get_pid (&process->uv);
  return CUV_READY;
}

int
cuv_kill (uv_pid_t pid, int signum)
{
  if (pid <= 0 || signum < 0)
    return UV_EINVAL;

  return uv_kill (pid, signum);
}

int
cuv_disable_stdio_inheritance ()
{
  uv_disable_stdio_inheritance ();
  return CUV_READY;
}
