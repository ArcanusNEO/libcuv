#include "intern.h"

static void cuv_idle_close_cb (uv_handle_t *handle);
static void cuv_prepare_close_cb (uv_handle_t *handle);
static void cuv_check_close_cb (uv_handle_t *handle);
static void cuv_check_kick_close_cb (uv_handle_t *handle);

static int
cuv_idle_resource_close (struct cuv_resource *resource)
{
  auto watcher = container_of (resource, struct cuv_idle, resource);

  if (!resource->owner)
    return CUV_READY;

  if (watcher->state == CUV_PHASE_NEW || watcher->state == CUV_PHASE_CLOSED)
    {
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  if (watcher->state == CUV_PHASE_CLOSING)
    return CUV_PENDING;

  uv_idle_stop (&watcher->uv);
  watcher->waiter = null;
  watcher->state = CUV_PHASE_CLOSING;
  watcher->close_error = 0;
  uv_close ((uv_handle_t *)&watcher->uv, cuv_idle_close_cb);
  return CUV_PENDING;
}

static int
cuv_prepare_resource_close (struct cuv_resource *resource)
{
  auto watcher = container_of (resource, struct cuv_prepare, resource);

  if (!resource->owner)
    return CUV_READY;

  if (watcher->state == CUV_PHASE_NEW || watcher->state == CUV_PHASE_CLOSED)
    {
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  if (watcher->state == CUV_PHASE_CLOSING)
    return CUV_PENDING;

  uv_prepare_stop (&watcher->uv);
  watcher->waiter = null;
  watcher->state = CUV_PHASE_CLOSING;
  watcher->close_error = 0;
  uv_close ((uv_handle_t *)&watcher->uv, cuv_prepare_close_cb);
  return CUV_PENDING;
}

static int
cuv_check_resource_close (struct cuv_resource *resource)
{
  auto watcher = container_of (resource, struct cuv_check, resource);

  if (!resource->owner)
    return CUV_READY;

  if (watcher->state == CUV_PHASE_NEW || watcher->state == CUV_PHASE_CLOSED)
    {
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  if (watcher->state == CUV_PHASE_CLOSING)
    return CUV_PENDING;

  uv_check_stop (&watcher->uv);
  if (watcher->kick_initialized)
    uv_idle_stop (&watcher->kick);
  watcher->waiter = null;
  watcher->state = CUV_PHASE_CLOSING;
  watcher->close_error = 0;
  watcher->close_pending = 1 + !!watcher->kick_initialized;
  uv_close ((uv_handle_t *)&watcher->uv, cuv_check_close_cb);
  if (watcher->kick_initialized)
    uv_close ((uv_handle_t *)&watcher->kick, cuv_check_kick_close_cb);
  return CUV_PENDING;
}

static int
cuv_idle_init_owned (struct cuv_task *task, struct cuv_idle *watcher)
{
  if (watcher->state == CUV_PHASE_NEW || watcher->state == CUV_PHASE_CLOSED)
    {
      int error = uv_idle_init (&task->loop->uv, &watcher->uv);
      if (error < 0)
        return error;

      watcher->resource.close = cuv_idle_resource_close;
      watcher->resource.kind = CUV_RESOURCE_IDLE;
      watcher->waiter = null;
      watcher->close_waiter = null;
      watcher->close_error = 0;
      watcher->state = CUV_PHASE_INITIALIZED;
      cuv_resource_attach (&watcher->resource, task);
      return 0;
    }

  if (watcher->resource.owner != task)
    return UV_EPERM;

  if (watcher->state != CUV_PHASE_INITIALIZED)
    return UV_EBUSY;

  return 0;
}

static int
cuv_prepare_init_owned (struct cuv_task *task, struct cuv_prepare *watcher)
{
  if (watcher->state == CUV_PHASE_NEW || watcher->state == CUV_PHASE_CLOSED)
    {
      int error = uv_prepare_init (&task->loop->uv, &watcher->uv);
      if (error < 0)
        return error;

      watcher->resource.close = cuv_prepare_resource_close;
      watcher->resource.kind = CUV_RESOURCE_PREPARE;
      watcher->waiter = null;
      watcher->close_waiter = null;
      watcher->close_error = 0;
      watcher->state = CUV_PHASE_INITIALIZED;
      cuv_resource_attach (&watcher->resource, task);
      return 0;
    }

  if (watcher->resource.owner != task)
    return UV_EPERM;

  if (watcher->state != CUV_PHASE_INITIALIZED)
    return UV_EBUSY;

  return 0;
}

static int
cuv_check_init_owned (struct cuv_task *task, struct cuv_check *watcher)
{
  if (watcher->state == CUV_PHASE_NEW || watcher->state == CUV_PHASE_CLOSED)
    {
      int error = uv_check_init (&task->loop->uv, &watcher->uv);
      if (error < 0)
        return error;

      watcher->resource.close = cuv_check_resource_close;
      watcher->resource.kind = CUV_RESOURCE_CHECK;
      watcher->waiter = null;
      watcher->close_waiter = null;
      watcher->close_error = 0;
      watcher->kick_initialized = 0;
      watcher->close_pending = 0;
      watcher->state = CUV_PHASE_INITIALIZED;
      cuv_resource_attach (&watcher->resource, task);
      return 0;
    }

  if (watcher->resource.owner != task)
    return UV_EPERM;

  if (watcher->state != CUV_PHASE_INITIALIZED)
    return UV_EBUSY;

  return 0;
}

static void
cuv_idle_cb (uv_idle_t *handle)
{
  auto watcher = container_of (handle, struct cuv_idle, uv);
  struct cuv_task *task = watcher->waiter;

  if (!task)
    return;

  watcher->waiter = null;
  int error = uv_idle_stop (handle);
  cuv_task_wake (task, error);
}

static void
cuv_prepare_cb (uv_prepare_t *handle)
{
  auto watcher = container_of (handle, struct cuv_prepare, uv);
  struct cuv_task *task = watcher->waiter;

  if (!task)
    return;

  watcher->waiter = null;
  int error = uv_prepare_stop (handle);
  cuv_task_wake (task, error);
}

static void
cuv_check_kick_cb (uv_idle_t *handle)
{
  (void)handle;
}

static void
cuv_check_cb (uv_check_t *handle)
{
  auto watcher = container_of (handle, struct cuv_check, uv);
  struct cuv_task *task = watcher->waiter;

  if (!task)
    return;

  watcher->waiter = null;
  int error = uv_check_stop (handle);
  int kick_error = uv_idle_stop (&watcher->kick);
  if (error >= 0 && kick_error < 0)
    error = kick_error;
  cuv_task_wake (task, error);
}

static int
cuv_cancel_idle_wait (struct cuv_task *task)
{
  struct cuv_idle *watcher = task->wait.operation.idle_wait.watcher;

  if (!watcher || watcher->waiter != task
      || watcher->state != CUV_PHASE_INITIALIZED)
    return UV_EBUSY;

  int error = uv_idle_stop (&watcher->uv);
  if (error < 0)
    return error;

  watcher->waiter = null;
  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

static int
cuv_cancel_prepare_wait (struct cuv_task *task)
{
  struct cuv_prepare *watcher = task->wait.operation.prepare_wait.watcher;

  if (!watcher || watcher->waiter != task
      || watcher->state != CUV_PHASE_INITIALIZED)
    return UV_EBUSY;

  int error = uv_prepare_stop (&watcher->uv);
  if (error < 0)
    return error;

  watcher->waiter = null;
  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

static int
cuv_cancel_check_wait (struct cuv_task *task)
{
  struct cuv_check *watcher = task->wait.operation.check_wait.watcher;

  if (!watcher || watcher->waiter != task
      || watcher->state != CUV_PHASE_INITIALIZED)
    return UV_EBUSY;

  int error = uv_check_stop (&watcher->uv);
  if (error < 0)
    return error;

  error = uv_idle_stop (&watcher->kick);
  if (error < 0)
    return error;

  watcher->waiter = null;
  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

static void
cuv_idle_close_cb (uv_handle_t *handle)
{
  auto watcher = container_of (handle, struct cuv_idle, uv);
  struct cuv_task *owner = watcher->resource.owner;
  struct cuv_task *waiter = watcher->close_waiter;
  int error = watcher->close_error;

  watcher->waiter = null;
  watcher->close_waiter = null;
  watcher->close_error = 0;
  watcher->state = CUV_PHASE_CLOSED;
  cuv_resource_detach (&watcher->resource);

  if (waiter)
    {
      cuv_task_wake (waiter, error);
      return;
    }

  if (owner && owner->state == CUV_TASK_CLOSING)
    cuv_task_cleanup_resume (owner, error);
}

static void
cuv_prepare_close_cb (uv_handle_t *handle)
{
  auto watcher = container_of (handle, struct cuv_prepare, uv);
  struct cuv_task *owner = watcher->resource.owner;
  struct cuv_task *waiter = watcher->close_waiter;
  int error = watcher->close_error;

  watcher->waiter = null;
  watcher->close_waiter = null;
  watcher->close_error = 0;
  watcher->state = CUV_PHASE_CLOSED;
  cuv_resource_detach (&watcher->resource);

  if (waiter)
    {
      cuv_task_wake (waiter, error);
      return;
    }

  if (owner && owner->state == CUV_TASK_CLOSING)
    cuv_task_cleanup_resume (owner, error);
}

static void
cuv_check_finish_close (struct cuv_check *watcher)
{
  struct cuv_task *owner = watcher->resource.owner;
  struct cuv_task *waiter = watcher->close_waiter;
  int error = watcher->close_error;

  watcher->waiter = null;
  watcher->close_waiter = null;
  watcher->close_error = 0;
  watcher->close_pending = 0;
  watcher->kick_initialized = 0;
  watcher->state = CUV_PHASE_CLOSED;
  cuv_resource_detach (&watcher->resource);

  if (waiter)
    {
      cuv_task_wake (waiter, error);
      return;
    }

  if (owner && owner->state == CUV_TASK_CLOSING)
    cuv_task_cleanup_resume (owner, error);
}

static void
cuv_check_close_cb (uv_handle_t *handle)
{
  auto watcher = container_of (handle, struct cuv_check, uv);
  assert (watcher->close_pending);
  if (--watcher->close_pending == 0)
    cuv_check_finish_close (watcher);
}

static void
cuv_check_kick_close_cb (uv_handle_t *handle)
{
  auto watcher = container_of (handle, struct cuv_check, kick);
  assert (watcher->close_pending);
  if (--watcher->close_pending == 0)
    cuv_check_finish_close (watcher);
}

int
cuv_idle_wait (struct cuv_task *task, struct cuv_idle *watcher)
{
  if (!task || !watcher)
    return UV_EINVAL;

  int error = cuv_idle_init_owned (task, watcher);
  if (error < 0)
    return error;

  if (watcher->waiter)
    return UV_EBUSY;

  watcher->waiter = task;
  task->wait.operation.idle_wait.watcher = watcher;
  task->wait.cancel = cuv_cancel_idle_wait;

  error = uv_idle_start (&watcher->uv, cuv_idle_cb);
  if (error < 0)
    {
      watcher->waiter = null;
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_prepare_wait (struct cuv_task *task, struct cuv_prepare *watcher)
{
  if (!task || !watcher)
    return UV_EINVAL;

  int error = cuv_prepare_init_owned (task, watcher);
  if (error < 0)
    return error;

  if (watcher->waiter)
    return UV_EBUSY;

  watcher->waiter = task;
  task->wait.operation.prepare_wait.watcher = watcher;
  task->wait.cancel = cuv_cancel_prepare_wait;

  error = uv_prepare_start (&watcher->uv, cuv_prepare_cb);
  if (error < 0)
    {
      watcher->waiter = null;
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_check_wait (struct cuv_task *task, struct cuv_check *watcher)
{
  if (!task || !watcher)
    return UV_EINVAL;

  int error = cuv_check_init_owned (task, watcher);
  if (error < 0)
    return error;

  if (watcher->waiter)
    return UV_EBUSY;

  if (!watcher->kick_initialized)
    {
      error = uv_idle_init (&task->loop->uv, &watcher->kick);
      if (error < 0)
        return error;
      watcher->kick_initialized = 1;
    }

  watcher->waiter = task;
  task->wait.operation.check_wait.watcher = watcher;
  task->wait.cancel = cuv_cancel_check_wait;

  error = uv_check_start (&watcher->uv, cuv_check_cb);
  if (error < 0)
    {
      watcher->waiter = null;
      task->wait.cancel = null;
      return error;
    }

  error = uv_idle_start (&watcher->kick, cuv_check_kick_cb);
  if (error < 0)
    {
      uv_check_stop (&watcher->uv);
      watcher->waiter = null;
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_idle_close (struct cuv_task *task, struct cuv_idle *watcher)
{
  if (!task || !watcher)
    return UV_EINVAL;

  if (watcher->state == CUV_PHASE_NEW || watcher->state == CUV_PHASE_CLOSED)
    return CUV_READY;

  if (watcher->resource.owner != task)
    return UV_EPERM;

  if (watcher->waiter || watcher->close_waiter
      || watcher->state == CUV_PHASE_CLOSING)
    return UV_EBUSY;

  int error = uv_idle_stop (&watcher->uv);
  if (error < 0)
    return error;

  watcher->close_waiter = task;
  watcher->state = CUV_PHASE_CLOSING;
  watcher->close_error = 0;
  task->wait.cancel = null;
  uv_close ((uv_handle_t *)&watcher->uv, cuv_idle_close_cb);
  return CUV_PENDING;
}

int
cuv_prepare_close (struct cuv_task *task, struct cuv_prepare *watcher)
{
  if (!task || !watcher)
    return UV_EINVAL;

  if (watcher->state == CUV_PHASE_NEW || watcher->state == CUV_PHASE_CLOSED)
    return CUV_READY;

  if (watcher->resource.owner != task)
    return UV_EPERM;

  if (watcher->waiter || watcher->close_waiter
      || watcher->state == CUV_PHASE_CLOSING)
    return UV_EBUSY;

  int error = uv_prepare_stop (&watcher->uv);
  if (error < 0)
    return error;

  watcher->close_waiter = task;
  watcher->state = CUV_PHASE_CLOSING;
  watcher->close_error = 0;
  task->wait.cancel = null;
  uv_close ((uv_handle_t *)&watcher->uv, cuv_prepare_close_cb);
  return CUV_PENDING;
}

int
cuv_check_close (struct cuv_task *task, struct cuv_check *watcher)
{
  if (!task || !watcher)
    return UV_EINVAL;

  if (watcher->state == CUV_PHASE_NEW || watcher->state == CUV_PHASE_CLOSED)
    return CUV_READY;

  if (watcher->resource.owner != task)
    return UV_EPERM;

  if (watcher->waiter || watcher->close_waiter
      || watcher->state == CUV_PHASE_CLOSING)
    return UV_EBUSY;

  int error = uv_check_stop (&watcher->uv);
  if (error < 0)
    return error;

  if (watcher->kick_initialized)
    {
      error = uv_idle_stop (&watcher->kick);
      if (error < 0)
        return error;
    }

  watcher->close_waiter = task;
  watcher->state = CUV_PHASE_CLOSING;
  watcher->close_error = 0;
  watcher->close_pending = 1 + !!watcher->kick_initialized;
  task->wait.cancel = null;
  uv_close ((uv_handle_t *)&watcher->uv, cuv_check_close_cb);
  if (watcher->kick_initialized)
    uv_close ((uv_handle_t *)&watcher->kick, cuv_check_kick_close_cb);
  return CUV_PENDING;
}
