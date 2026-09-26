#include "intern.h"

static struct cuv_fs_event *
cuv_fs_event_from_handle (uv_handle_t *handle)
{
  return container_of (handle, struct cuv_fs_event, uv);
}

static struct cuv_fs_poll *
cuv_fs_poll_from_handle (uv_handle_t *handle)
{
  return container_of (handle, struct cuv_fs_poll, uv);
}

static void cuv_fs_event_close_cb (uv_handle_t *handle);
static void cuv_fs_poll_close_cb (uv_handle_t *handle);

static int
cuv_fs_event_resource_close (struct cuv_resource *resource)
{
  auto watcher = container_of (resource, struct cuv_fs_event, resource);

  if (!resource->owner)
    return CUV_READY;

  if (watcher->state == CUV_FS_EVENT_NEW
      || watcher->state == CUV_FS_EVENT_CLOSED)
    {
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  if (watcher->state == CUV_FS_EVENT_CLOSING)
    return CUV_PENDING;

  uv_fs_event_stop (&watcher->uv);
  watcher->waiter = null;
  watcher->state = CUV_FS_EVENT_CLOSING;
  watcher->close_error = 0;
  uv_close ((uv_handle_t *)&watcher->uv, cuv_fs_event_close_cb);
  return CUV_PENDING;
}

static int
cuv_fs_poll_resource_close (struct cuv_resource *resource)
{
  auto watcher = container_of (resource, struct cuv_fs_poll, resource);

  if (!resource->owner)
    return CUV_READY;

  if (watcher->state == CUV_FS_POLL_NEW
      || watcher->state == CUV_FS_POLL_CLOSED)
    {
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  if (watcher->state == CUV_FS_POLL_CLOSING)
    return CUV_PENDING;

  uv_fs_poll_stop (&watcher->uv);
  watcher->waiter = null;
  watcher->state = CUV_FS_POLL_CLOSING;
  watcher->close_error = 0;
  uv_close ((uv_handle_t *)&watcher->uv, cuv_fs_poll_close_cb);
  return CUV_PENDING;
}

static int
cuv_fs_event_init_owned (struct cuv_task *task, struct cuv_fs_event *watcher)
{
  if (watcher->state == CUV_FS_EVENT_NEW
      || watcher->state == CUV_FS_EVENT_CLOSED)
    {
      int error = uv_fs_event_init (&task->loop->uv, &watcher->uv);
      if (error < 0)
        return error;

      watcher->resource.close = cuv_fs_event_resource_close;
      watcher->resource.kind = CUV_RESOURCE_FS_EVENT;
      watcher->waiter = null;
      watcher->close_waiter = null;
      watcher->close_error = 0;
      watcher->state = CUV_FS_EVENT_INITIALIZED;
      cuv_resource_attach (&watcher->resource, task);
      return 0;
    }

  if (!watcher->resource.owner || watcher->resource.owner->loop != task->loop)
    return UV_EPERM;

  if (watcher->state != CUV_FS_EVENT_INITIALIZED)
    return UV_EBUSY;

  return 0;
}

static int
cuv_fs_poll_init_owned (struct cuv_task *task, struct cuv_fs_poll *watcher)
{
  if (watcher->state == CUV_FS_POLL_NEW
      || watcher->state == CUV_FS_POLL_CLOSED)
    {
      int error = uv_fs_poll_init (&task->loop->uv, &watcher->uv);
      if (error < 0)
        return error;

      watcher->resource.close = cuv_fs_poll_resource_close;
      watcher->resource.kind = CUV_RESOURCE_FS_POLL;
      watcher->waiter = null;
      watcher->close_waiter = null;
      watcher->close_error = 0;
      watcher->state = CUV_FS_POLL_INITIALIZED;
      cuv_resource_attach (&watcher->resource, task);
      return 0;
    }

  if (watcher->resource.owner != task)
    return UV_EPERM;

  if (watcher->state != CUV_FS_POLL_INITIALIZED)
    return UV_EBUSY;

  return 0;
}

static void
cuv_fs_event_cb (uv_fs_event_t *handle, char const *filename, int events,
                 int status)
{
  auto watcher = container_of (handle, struct cuv_fs_event, uv);
  struct cuv_task *task = watcher->waiter;

  if (!task)
    return;

  watcher->waiter = null;

  int error = uv_fs_event_stop (handle);
  if (status < 0)
    error = status;

  if (!error)
    {
      char *output = task->wait.operation.fs_event_wait.filename;
      usz capacity = task->wait.operation.fs_event_wait.filename_capacity;

      *task->wait.operation.fs_event_wait.events = events;

      if (output)
        {
          if (!filename)
            output[0] = '\0';
          else
            {
              usz length = strlen (filename) + 1;
              if (length > capacity)
                error = UV_ENOBUFS;
              else
                memcpy (output, filename, length);
            }
        }
    }

  cuv_task_wake (task, error);
}

static void
cuv_fs_poll_cb (uv_fs_poll_t *handle, int status, uv_stat_t const *previous,
                uv_stat_t const *current)
{
  auto watcher = container_of (handle, struct cuv_fs_poll, uv);
  struct cuv_task *task = watcher->waiter;

  if (!task)
    return;

  watcher->waiter = null;

  int error = uv_fs_poll_stop (handle);
  if (!error)
    {
      *task->wait.operation.fs_poll_wait.status = status;

      if (status == 0)
        {
          *task->wait.operation.fs_poll_wait.previous = *previous;
          *task->wait.operation.fs_poll_wait.current = *current;
        }
    }

  cuv_task_wake (task, error);
}

static int
cuv_cancel_fs_event_wait (struct cuv_task *task)
{
  struct cuv_fs_event *watcher = task->wait.operation.fs_event_wait.watcher;

  if (!watcher || watcher->waiter != task
      || watcher->state != CUV_FS_EVENT_INITIALIZED)
    return UV_EBUSY;

  int error = uv_fs_event_stop (&watcher->uv);
  if (error < 0)
    return error;

  watcher->waiter = null;
  watcher->close_waiter = task;
  watcher->close_error = UV_ECANCELED;
  watcher->state = CUV_FS_EVENT_CLOSING;
  uv_close ((uv_handle_t *)&watcher->uv, cuv_fs_event_close_cb);
  return 0;
}

static int
cuv_cancel_fs_poll_wait (struct cuv_task *task)
{
  struct cuv_fs_poll *watcher = task->wait.operation.fs_poll_wait.watcher;

  if (!watcher || watcher->waiter != task
      || watcher->state != CUV_FS_POLL_INITIALIZED)
    return UV_EBUSY;

  int error = uv_fs_poll_stop (&watcher->uv);
  if (error < 0)
    return error;

  watcher->waiter = null;
  watcher->close_waiter = task;
  watcher->close_error = UV_ECANCELED;
  watcher->state = CUV_FS_POLL_CLOSING;
  uv_close ((uv_handle_t *)&watcher->uv, cuv_fs_poll_close_cb);
  return 0;
}

static void
cuv_fs_event_close_cb (uv_handle_t *handle)
{
  struct cuv_fs_event *watcher = cuv_fs_event_from_handle (handle);
  struct cuv_task *owner = watcher->resource.owner;
  struct cuv_task *waiter = watcher->close_waiter;
  int error = watcher->close_error;

  watcher->waiter = null;
  watcher->close_waiter = null;
  watcher->close_error = 0;
  watcher->state = CUV_FS_EVENT_CLOSED;
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
cuv_fs_poll_close_cb (uv_handle_t *handle)
{
  struct cuv_fs_poll *watcher = cuv_fs_poll_from_handle (handle);
  struct cuv_task *owner = watcher->resource.owner;
  struct cuv_task *waiter = watcher->close_waiter;
  int error = watcher->close_error;

  watcher->waiter = null;
  watcher->close_waiter = null;
  watcher->close_error = 0;
  watcher->state = CUV_FS_POLL_CLOSED;
  cuv_resource_detach (&watcher->resource);

  if (waiter)
    {
      cuv_task_wake (waiter, error);
      return;
    }

  if (owner && owner->state == CUV_TASK_CLOSING)
    cuv_task_cleanup_resume (owner, error);
}

int
cuv_fs_event_wait (struct cuv_task *task, struct cuv_fs_event *watcher,
                   char *filename, usz filename_capacity, int *events,
                   char const *path, unsigned flags)
{
  if (!task || !watcher || !events || !path)
    return UV_EINVAL;

  if ((!filename && filename_capacity) || (filename && !filename_capacity))
    return UV_EINVAL;

  int error = cuv_fs_event_init_owned (task, watcher);
  if (error < 0)
    return error;

  if (watcher->waiter || watcher->close_waiter)
    return UV_EBUSY;

  task->wait.operation.fs_event_wait.watcher = watcher;
  task->wait.operation.fs_event_wait.filename = filename;
  task->wait.operation.fs_event_wait.filename_capacity = filename_capacity;
  task->wait.operation.fs_event_wait.events = events;
  task->wait.cancel = cuv_cancel_fs_event_wait;
  watcher->waiter = task;

  error = uv_fs_event_start (&watcher->uv, cuv_fs_event_cb, path, flags);
  if (error < 0)
    {
      watcher->waiter = null;
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_fs_event_close (struct cuv_task *task, struct cuv_fs_event *watcher)
{
  if (!task || !watcher)
    return UV_EINVAL;

  if (watcher->state == CUV_FS_EVENT_NEW
      || watcher->state == CUV_FS_EVENT_CLOSED)
    return CUV_READY;

  if (watcher->resource.owner != task)
    return UV_EPERM;

  if (watcher->waiter || watcher->close_waiter
      || watcher->state == CUV_FS_EVENT_CLOSING)
    return UV_EBUSY;

  int error = uv_fs_event_stop (&watcher->uv);
  if (error < 0)
    return error;

  watcher->close_waiter = task;
  watcher->close_error = 0;
  watcher->state = CUV_FS_EVENT_CLOSING;
  task->wait.cancel = null;
  uv_close ((uv_handle_t *)&watcher->uv, cuv_fs_event_close_cb);
  return CUV_PENDING;
}

int
cuv_fs_poll_wait (struct cuv_task *task, struct cuv_fs_poll *watcher,
                  int *status, uv_stat_t *previous, uv_stat_t *current,
                  char const *path, unsigned interval)
{
  if (!task || !watcher || !status || !previous || !current || !path)
    return UV_EINVAL;

  int error = cuv_fs_poll_init_owned (task, watcher);
  if (error < 0)
    return error;

  if (watcher->waiter || watcher->close_waiter)
    return UV_EBUSY;

  task->wait.operation.fs_poll_wait.watcher = watcher;
  task->wait.operation.fs_poll_wait.status = status;
  task->wait.operation.fs_poll_wait.previous = previous;
  task->wait.operation.fs_poll_wait.current = current;
  task->wait.cancel = cuv_cancel_fs_poll_wait;
  watcher->waiter = task;

  error = uv_fs_poll_start (&watcher->uv, cuv_fs_poll_cb, path, interval);
  if (error < 0)
    {
      watcher->waiter = null;
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_fs_poll_close (struct cuv_task *task, struct cuv_fs_poll *watcher)
{
  if (!task || !watcher)
    return UV_EINVAL;

  if (watcher->state == CUV_FS_POLL_NEW
      || watcher->state == CUV_FS_POLL_CLOSED)
    return CUV_READY;

  if (watcher->resource.owner != task)
    return UV_EPERM;

  if (watcher->waiter || watcher->close_waiter
      || watcher->state == CUV_FS_POLL_CLOSING)
    return UV_EBUSY;

  int error = uv_fs_poll_stop (&watcher->uv);
  if (error < 0)
    return error;

  watcher->close_waiter = task;
  watcher->close_error = 0;
  watcher->state = CUV_FS_POLL_CLOSING;
  task->wait.cancel = null;
  uv_close ((uv_handle_t *)&watcher->uv, cuv_fs_poll_close_cb);
  return CUV_PENDING;
}

int
cuv_fs_event_getpath (struct cuv_task *task, struct cuv_fs_event *watcher,
                      char *buffer, usz *size)
{
  if (!task || !watcher || !buffer || !size)
    return UV_EINVAL;

  if (!watcher->resource.owner || watcher->resource.owner->loop != task->loop)
    return UV_EPERM;

  if (watcher->state != CUV_FS_EVENT_INITIALIZED)
    return UV_EBADF;

  size_t length = *size;
  int error = uv_fs_event_getpath (&watcher->uv, buffer, &length);
  *size = length;
  return error < 0 ? error : CUV_READY;
}

int
cuv_fs_poll_getpath (struct cuv_task *task, struct cuv_fs_poll *watcher,
                     char *buffer, usz *size)
{
  if (!task || !watcher || !buffer || !size)
    return UV_EINVAL;

  if (!watcher->resource.owner || watcher->resource.owner->loop != task->loop)
    return UV_EPERM;

  if (watcher->state != CUV_FS_POLL_INITIALIZED)
    return UV_EBADF;

  size_t length = *size;
  int error = uv_fs_poll_getpath (&watcher->uv, buffer, &length);
  *size = length;
  return error < 0 ? error : CUV_READY;
}
