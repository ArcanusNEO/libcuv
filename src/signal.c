#include "intern.h"

static void cuv_signal_close_cb (uv_handle_t *handle);

static struct cuv_signal *
cuv_signal_from_handle (uv_handle_t *handle)
{
  return container_of (handle, struct cuv_signal, uv);
}

static int
cuv_signal_resource_close (struct cuv_resource *resource)
{
  auto signal = container_of (resource, struct cuv_signal, resource);

  if (!resource->owner)
    return CUV_READY;

  if (signal->state == CUV_SIGNAL_NEW || signal->state == CUV_SIGNAL_CLOSED)
    {
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  if (signal->state == CUV_SIGNAL_CLOSING)
    return CUV_PENDING;

  uv_signal_stop (&signal->uv);
  signal->waiter = null;
  signal->state = CUV_SIGNAL_CLOSING;
  uv_close ((uv_handle_t *)&signal->uv, cuv_signal_close_cb);
  return CUV_PENDING;
}

static int
cuv_signal_init_owned (struct cuv_task *task, struct cuv_signal *signal)
{
  if (signal->state == CUV_SIGNAL_NEW)
    {
      int error = uv_signal_init (&task->loop->uv, &signal->uv);
      if (error < 0)
        return error;

      signal->resource.close = cuv_signal_resource_close;
      signal->resource.kind = CUV_RESOURCE_SIGNAL;
      signal->state = CUV_SIGNAL_INITIALIZED;
      cuv_resource_attach (&signal->resource, task);
      return 0;
    }

  if (signal->resource.owner != task)
    return UV_EPERM;

  if (signal->state != CUV_SIGNAL_INITIALIZED)
    return UV_EBUSY;

  return 0;
}

static void
cuv_signal_cb (uv_signal_t *handle, int signum)
{
  auto signal = container_of (handle, struct cuv_signal, uv);
  struct cuv_task *task = signal->waiter;

  signal->waiter = null;
  signal->signum = signum;

  if (task)
    cuv_task_wake (task, 0);
}

static int
cuv_cancel_signal_wait (struct cuv_task *task)
{
  struct cuv_signal *signal = task->wait.operation.signal_wait.signal;

  if (!signal || signal->waiter != task)
    return UV_EBUSY;

  int error = uv_signal_stop (&signal->uv);
  if (error < 0)
    return error;

  signal->waiter = null;
  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

static void
cuv_signal_close_cb (uv_handle_t *handle)
{
  struct cuv_signal *signal = cuv_signal_from_handle (handle);
  struct cuv_task *owner = signal->resource.owner;
  struct cuv_task *waiter = signal->close_waiter;

  signal->waiter = null;
  signal->close_waiter = null;
  signal->state = CUV_SIGNAL_CLOSED;
  cuv_resource_detach (&signal->resource);

  if (waiter)
    {
      cuv_task_wake (waiter, 0);
      return;
    }

  if (owner && owner->state == CUV_TASK_CLOSING)
    cuv_task_cleanup_resume (owner, 0);
}

int
cuv_signal_wait (struct cuv_task *task, struct cuv_signal *signal, int signum)
{
  if (!task || !signal || signum <= 0)
    return UV_EINVAL;

  int error = cuv_signal_init_owned (task, signal);
  if (error < 0)
    return error;

  if (signal->waiter)
    return UV_EBUSY;

  task->wait.operation.signal_wait.signal = signal;
  task->wait.cancel = cuv_cancel_signal_wait;

  signal->waiter = task;
  signal->signum = signum;

  error = uv_signal_start_oneshot (&signal->uv, cuv_signal_cb, signum);
  if (error < 0)
    {
      signal->waiter = null;
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_signal_close (struct cuv_task *task, struct cuv_signal *signal)
{
  if (!task || !signal)
    return UV_EINVAL;

  if (signal->state == CUV_SIGNAL_NEW || signal->state == CUV_SIGNAL_CLOSED)
    return CUV_READY;

  if (signal->resource.owner != task)
    return UV_EPERM;

  if (signal->waiter || signal->close_waiter
      || signal->state == CUV_SIGNAL_CLOSING)
    return UV_EBUSY;

  int error = uv_signal_stop (&signal->uv);
  if (error < 0)
    return error;

  signal->close_waiter = task;
  signal->state = CUV_SIGNAL_CLOSING;
  task->wait.cancel = null;

  uv_close ((uv_handle_t *)&signal->uv, cuv_signal_close_cb);
  return CUV_PENDING;
}
