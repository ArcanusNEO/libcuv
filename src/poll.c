#include "intern.h"

static void cuv_poll_close_cb (uv_handle_t *handle);

static struct cuv_poll *
cuv_poll_from_handle (uv_handle_t *handle)
{
  return container_of (handle, struct cuv_poll, uv);
}

static int
cuv_poll_resource_close (struct cuv_resource *resource)
{
  auto poll = container_of (resource, struct cuv_poll, resource);

  if (!resource->owner)
    return CUV_READY;

  if (poll->state == CUV_POLL_NEW || poll->state == CUV_POLL_CLOSED)
    {
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  if (poll->state == CUV_POLL_CLOSING)
    return CUV_PENDING;

  poll->close_error = uv_poll_stop (&poll->uv);
  poll->waiter = null;
  poll->state = CUV_POLL_CLOSING;
  uv_close ((uv_handle_t *)&poll->uv, cuv_poll_close_cb);
  return CUV_PENDING;
}

static int
cuv_poll_init_owned (struct cuv_task *task, struct cuv_poll *poll, int fd,
                     uv_os_sock_t socket, int socket_mode)
{
  if (poll->state != CUV_POLL_NEW && poll->state != CUV_POLL_CLOSED)
    return UV_EBUSY;

  int error = socket_mode
                  ? uv_poll_init_socket (&task->loop->uv, &poll->uv, socket)
                  : uv_poll_init (&task->loop->uv, &poll->uv, fd);
  if (error < 0)
    return error;

  poll->resource.close = cuv_poll_resource_close;
  poll->resource.kind = CUV_RESOURCE_POLL;
  poll->waiter = null;
  poll->close_waiter = null;
  poll->close_error = 0;
  poll->state = CUV_POLL_INITIALIZED;
  cuv_resource_attach (&poll->resource, task);
  return 0;
}

static void
cuv_poll_cb (uv_poll_t *handle, int status, int events)
{
  auto poll = container_of (handle, struct cuv_poll, uv);
  struct cuv_task *task = poll->waiter;

  if (!task)
    return;

  poll->waiter = null;

  int error = uv_poll_stop (handle);
  if (status < 0)
    error = status;

  if (!error)
    *task->wait.operation.poll_wait.events = events;

  cuv_task_wake (task, error);
}

static int
cuv_cancel_poll_wait (struct cuv_task *task)
{
  struct cuv_poll *poll = task->wait.operation.poll_wait.poll;

  if (!poll || poll->waiter != task || poll->state != CUV_POLL_INITIALIZED)
    return UV_EBUSY;

  int error = uv_poll_stop (&poll->uv);
  if (error < 0)
    return error;

  poll->waiter = null;
  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

static void
cuv_poll_close_cb (uv_handle_t *handle)
{
  struct cuv_poll *poll = cuv_poll_from_handle (handle);
  struct cuv_task *owner = poll->resource.owner;
  struct cuv_task *waiter = poll->close_waiter;
  int error = poll->close_error;

  poll->waiter = null;
  poll->close_waiter = null;
  poll->close_error = 0;
  poll->state = CUV_POLL_CLOSED;
  cuv_resource_detach (&poll->resource);

  if (waiter)
    {
      cuv_task_wake (waiter, error);
      return;
    }

  if (owner && owner->state == CUV_TASK_CLOSING)
    cuv_task_cleanup_resume (owner, error);
}

int
cuv_poll_init (struct cuv_task *task, struct cuv_poll *poll, int fd)
{
  if (!task || !poll || fd < 0)
    return UV_EINVAL;

  int error = cuv_poll_init_owned (task, poll, fd, 0, 0);
  return error < 0 ? error : CUV_READY;
}

int
cuv_poll_init_socket (struct cuv_task *task, struct cuv_poll *poll,
                      uv_os_sock_t socket)
{
  if (!task || !poll)
    return UV_EINVAL;

  int error = cuv_poll_init_owned (task, poll, -1, socket, 1);
  return error < 0 ? error : CUV_READY;
}

int
cuv_poll_wait (struct cuv_task *task, struct cuv_poll *poll, int *events,
               int interests)
{
  auto const valid
      = UV_READABLE | UV_WRITABLE | UV_DISCONNECT | UV_PRIORITIZED;

  if (!task || !poll || !events || !interests || (interests & ~valid))
    return UV_EINVAL;

  if (poll->resource.owner != task)
    return UV_EPERM;

  if (poll->state != CUV_POLL_INITIALIZED || poll->waiter
      || poll->close_waiter)
    return UV_EBUSY;

  task->wait.operation.poll_wait.poll = poll;
  task->wait.operation.poll_wait.events = events;
  task->wait.cancel = cuv_cancel_poll_wait;
  poll->waiter = task;

  int error = uv_poll_start (&poll->uv, interests, cuv_poll_cb);
  if (error < 0)
    {
      poll->waiter = null;
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_poll_close (struct cuv_task *task, struct cuv_poll *poll)
{
  if (!task || !poll)
    return UV_EINVAL;

  if (poll->state == CUV_POLL_NEW || poll->state == CUV_POLL_CLOSED)
    return CUV_READY;

  if (poll->resource.owner != task)
    return UV_EPERM;

  if (poll->waiter || poll->close_waiter || poll->state == CUV_POLL_CLOSING)
    return UV_EBUSY;

  int error = uv_poll_stop (&poll->uv);
  if (error < 0)
    return error;

  poll->close_waiter = task;
  poll->close_error = 0;
  poll->state = CUV_POLL_CLOSING;
  task->wait.cancel = null;
  uv_close ((uv_handle_t *)&poll->uv, cuv_poll_close_cb);
  return CUV_PENDING;
}
