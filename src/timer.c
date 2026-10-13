#include "intern.h"

static void cuv_timer_handle_close_cb (uv_handle_t *handle);

static int
cuv_timer_resource_close (struct cuv_resource *resource)
{
  auto timer = container_of (resource, struct cuv_timer, resource);

  if (!resource->owner)
    return CUV_READY;

  if (timer->state == CUV_REUSABLE_TIMER_NEW
      || timer->state == CUV_REUSABLE_TIMER_CLOSED)
    {
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  if (timer->state == CUV_REUSABLE_TIMER_CLOSING)
    return CUV_PENDING;

  uv_timer_stop (&timer->uv);
  timer->waiter = null;
  timer->pending = 0;
  timer->state = CUV_REUSABLE_TIMER_CLOSING;
  timer->close_error = 0;
  uv_close ((uv_handle_t *)&timer->uv, cuv_timer_handle_close_cb);
  return CUV_PENDING;
}

static int
cuv_timer_init_owned (struct cuv_task *task, struct cuv_timer *timer)
{
  if (timer->state == CUV_REUSABLE_TIMER_NEW
      || timer->state == CUV_REUSABLE_TIMER_CLOSED)
    {
      int error = uv_timer_init (&task->loop->uv, &timer->uv);
      if (error < 0)
        return error;

      timer->resource.close = cuv_timer_resource_close;
      timer->resource.kind = CUV_RESOURCE_TIMER;
      timer->waiter = null;
      timer->close_waiter = null;
      timer->pending = 0;
      timer->close_error = 0;
      timer->state = CUV_REUSABLE_TIMER_INITIALIZED;
      cuv_resource_attach (&timer->resource, task);
      return 0;
    }

  if (timer->resource.owner != task)
    return UV_EPERM;

  if (timer->state != CUV_REUSABLE_TIMER_INITIALIZED)
    return UV_EBUSY;

  return 0;
}

static int
cuv_timer_readable (struct cuv_task *task, struct cuv_timer *timer)
{
  if (!task || !timer)
    return UV_EINVAL;

  if (timer->state != CUV_REUSABLE_TIMER_INITIALIZED || !timer->resource.owner)
    return UV_EINVAL;

  if (timer->resource.owner->loop != task->loop)
    return UV_EPERM;

  return 0;
}

static void
cuv_timer_handle_cb (uv_timer_t *handle)
{
  auto timer = container_of (handle, struct cuv_timer, uv);
  struct cuv_task *task = timer->waiter;

  if (task)
    {
      timer->waiter = null;
      cuv_task_wake (task, 0);
      return;
    }

  if (timer->pending != UINT64_MAX)
    ++timer->pending;
}

static int
cuv_cancel_timer_wait (struct cuv_task *task)
{
  struct cuv_timer *timer = task->wait.operation.timer_wait.timer;

  if (!timer || timer->waiter != task
      || timer->state != CUV_REUSABLE_TIMER_INITIALIZED)
    return UV_EBUSY;

  timer->waiter = null;
  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

static void
cuv_timer_handle_close_cb (uv_handle_t *handle)
{
  auto timer = container_of (handle, struct cuv_timer, uv);
  struct cuv_task *owner = timer->resource.owner;
  struct cuv_task *waiter = timer->close_waiter;
  int error = timer->close_error;

  timer->waiter = null;
  timer->close_waiter = null;
  timer->pending = 0;
  timer->close_error = 0;
  timer->state = CUV_REUSABLE_TIMER_CLOSED;
  cuv_resource_detach (&timer->resource);

  if (waiter)
    {
      cuv_task_wake (waiter, error);
      return;
    }

  if (owner && owner->state == CUV_TASK_CLOSING)
    cuv_task_cleanup_resume (owner, error);
}

int
cuv_timer_start (struct cuv_task *task, struct cuv_timer *timer, u64 timeout,
                 u64 repeat)
{
  if (!task || !timer)
    return UV_EINVAL;

  int error = cuv_timer_init_owned (task, timer);
  if (error < 0)
    return error;

  if (timer->waiter || timer->close_waiter)
    return UV_EBUSY;

  timer->pending = 0;
  error = uv_timer_start (&timer->uv, cuv_timer_handle_cb, timeout, repeat);
  return error < 0 ? error : CUV_READY;
}

int
cuv_timer_wait (struct cuv_task *task, struct cuv_timer *timer)
{
  if (!task || !timer)
    return UV_EINVAL;

  int error = cuv_timer_init_owned (task, timer);
  if (error < 0)
    return error;

  if (timer->waiter)
    return UV_EBUSY;

  if (timer->pending)
    {
      --timer->pending;
      return CUV_READY;
    }

  if (!uv_is_active ((uv_handle_t *)&timer->uv))
    return UV_EINVAL;

  timer->waiter = task;
  task->wait.operation.timer_wait.timer = timer;
  task->wait.cancel = cuv_cancel_timer_wait;
  return CUV_PENDING;
}

int
cuv_timer_stop (struct cuv_task *task, struct cuv_timer *timer)
{
  if (!task || !timer)
    return UV_EINVAL;

  int error = cuv_timer_init_owned (task, timer);
  if (error < 0)
    return error;

  if (timer->waiter || timer->close_waiter)
    return UV_EBUSY;

  error = uv_timer_stop (&timer->uv);
  return error < 0 ? error : CUV_READY;
}

int
cuv_timer_again (struct cuv_task *task, struct cuv_timer *timer)
{
  if (!task || !timer)
    return UV_EINVAL;

  int error = cuv_timer_init_owned (task, timer);
  if (error < 0)
    return error;

  if (timer->waiter || timer->close_waiter)
    return UV_EBUSY;

  error = uv_timer_again (&timer->uv);
  return error < 0 ? error : CUV_READY;
}

int
cuv_timer_set_repeat (struct cuv_task *task, struct cuv_timer *timer,
                      u64 repeat)
{
  if (!task || !timer)
    return UV_EINVAL;

  int error = cuv_timer_init_owned (task, timer);
  if (error < 0)
    return error;

  if (timer->close_waiter)
    return UV_EBUSY;

  uv_timer_set_repeat (&timer->uv, repeat);
  return CUV_READY;
}

int
cuv_timer_get_repeat (struct cuv_task *task, struct cuv_timer *timer,
                      u64 *repeat)
{
  if (!repeat)
    return UV_EINVAL;

  int error = cuv_timer_readable (task, timer);
  if (error < 0)
    return error;

  *repeat = uv_timer_get_repeat (&timer->uv);
  return CUV_READY;
}

int
cuv_timer_get_due_in (struct cuv_task *task, struct cuv_timer *timer,
                      u64 *due_in)
{
  if (!due_in)
    return UV_EINVAL;

  int error = cuv_timer_readable (task, timer);
  if (error < 0)
    return error;

  *due_in = uv_timer_get_due_in (&timer->uv);
  return CUV_READY;
}

int
cuv_timer_close (struct cuv_task *task, struct cuv_timer *timer)
{
  if (!task || !timer)
    return UV_EINVAL;

  if (timer->state == CUV_REUSABLE_TIMER_NEW
      || timer->state == CUV_REUSABLE_TIMER_CLOSED)
    return CUV_READY;

  if (timer->resource.owner != task)
    return UV_EPERM;

  if (timer->waiter || timer->close_waiter
      || timer->state == CUV_REUSABLE_TIMER_CLOSING)
    return UV_EBUSY;

  int error = uv_timer_stop (&timer->uv);
  if (error < 0)
    return error;

  timer->pending = 0;
  timer->close_waiter = task;
  timer->state = CUV_REUSABLE_TIMER_CLOSING;
  timer->close_error = 0;
  task->wait.cancel = null;
  uv_close ((uv_handle_t *)&timer->uv, cuv_timer_handle_close_cb);
  return CUV_PENDING;
}
