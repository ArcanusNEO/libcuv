#include "intern.h"

static void cuv_tty_close_cb (uv_handle_t *handle);

static struct cuv_tty *
cuv_tty_from_handle (uv_handle_t *handle)
{
  return container_of (handle, struct cuv_tty, uv);
}

static int
cuv_tty_resource_close (struct cuv_resource *resource)
{
  auto tty = container_of (resource, struct cuv_tty, resource);

  if (!resource->owner)
    return CUV_READY;

  if (tty->state == CUV_TTY_NEW || tty->state == CUV_TTY_CLOSED)
    {
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  if (tty->state == CUV_TTY_CLOSING)
    return CUV_PENDING;

  tty->close_error = uv_read_stop ((uv_stream_t *)&tty->uv);
  tty->reader = null;
  tty->state = CUV_TTY_CLOSING;
  uv_close ((uv_handle_t *)&tty->uv, cuv_tty_close_cb);
  return CUV_PENDING;
}

static void
cuv_tty_close_cb (uv_handle_t *handle)
{
  struct cuv_tty *tty = cuv_tty_from_handle (handle);
  struct cuv_task *owner = tty->resource.owner;
  struct cuv_task *waiter = tty->close_waiter;
  int error = tty->close_error;

  tty->reader = null;
  tty->close_waiter = null;
  tty->close_error = 0;
  tty->state = CUV_TTY_CLOSED;
  cuv_resource_detach (&tty->resource);

  if (waiter)
    {
      cuv_task_wake (waiter, error);
      return;
    }

  if (owner && owner->state == CUV_TASK_CLOSING)
    cuv_task_cleanup_resume (owner, error);
}

int
cuv_tty_open (struct cuv_task *task, struct cuv_tty *tty, uv_file file)
{
  if (!task || !tty || file < 0)
    return UV_EINVAL;

  if (tty->state != CUV_TTY_NEW && tty->state != CUV_TTY_CLOSED)
    return UV_EBUSY;

  int error = uv_tty_init (&task->loop->uv, &tty->uv, file, 0);
  if (error < 0)
    return error;

  tty->resource.close = cuv_tty_resource_close;
  tty->resource.kind = CUV_RESOURCE_TTY;
  tty->reader = null;
  tty->close_waiter = null;
  tty->close_error = 0;
  tty->state = CUV_TTY_INITIALIZED;
  cuv_resource_attach (&tty->resource, task);
  return CUV_READY;
}

int
cuv_tty_read (struct cuv_task *task, isz *size, struct cuv_tty *tty,
              void *buffer, usz capacity)
{
  if (!task || !size || !tty || (!buffer && capacity))
    return UV_EINVAL;

  if (tty->resource.owner != task)
    return UV_EPERM;

  if (tty->state != CUV_TTY_INITIALIZED)
    return UV_EBUSY;

  return cuv_stream_read (task, (uv_stream_t *)&tty->uv, &tty->reader, size,
                          buffer, capacity);
}

int
cuv_tty_write (struct cuv_task *task, struct cuv_tty *tty, void const *buffer,
               usz length)
{
  if (!task || !tty || (!buffer && length))
    return UV_EINVAL;

  if (tty->resource.owner != task)
    return UV_EPERM;

  if (tty->state != CUV_TTY_INITIALIZED)
    return UV_EBUSY;

  return cuv_stream_write (task, (uv_stream_t *)&tty->uv, buffer, length);
}

int
cuv_tty_try_write (struct cuv_task *task, isz *size, struct cuv_tty *tty,
                   void const *buffer, usz length)
{
  if (!task || !size || !tty || (!buffer && length))
    return UV_EINVAL;

  if (tty->resource.owner != task)
    return UV_EPERM;

  if (tty->state != CUV_TTY_INITIALIZED)
    return UV_EBUSY;

  return cuv_stream_try_write (size, (uv_stream_t *)&tty->uv, buffer, length);
}

static int
cuv_tty_stream_ready (struct cuv_task *task, struct cuv_tty *tty)
{
  if (!task || !tty)
    return UV_EINVAL;

  if (tty->resource.owner != task)
    return UV_EPERM;

  if (tty->state != CUV_TTY_INITIALIZED)
    return UV_EBUSY;

  return 0;
}

int
cuv_tty_write_queue (struct cuv_task *task, struct cuv_tty *tty, usz *size)
{
  if (!size)
    return UV_EINVAL;

  int error = cuv_tty_stream_ready (task, tty);
  if (error < 0)
    return error;

  return cuv_stream_write_queue (size, (uv_stream_t *)&tty->uv);
}

int
cuv_tty_is_readable (struct cuv_task *task, struct cuv_tty *tty, int *readable)
{
  if (!readable)
    return UV_EINVAL;

  int error = cuv_tty_stream_ready (task, tty);
  if (error < 0)
    return error;

  return cuv_stream_is_readable (readable, (uv_stream_t *)&tty->uv);
}

int
cuv_tty_is_writable (struct cuv_task *task, struct cuv_tty *tty, int *writable)
{
  if (!writable)
    return UV_EINVAL;

  int error = cuv_tty_stream_ready (task, tty);
  if (error < 0)
    return error;

  return cuv_stream_is_writable (writable, (uv_stream_t *)&tty->uv);
}

int
cuv_tty_set_blocking (struct cuv_task *task, struct cuv_tty *tty, int blocking)
{
  int error = cuv_tty_stream_ready (task, tty);
  if (error < 0)
    return error;

  return cuv_stream_set_blocking ((uv_stream_t *)&tty->uv, blocking);
}

int
cuv_tty_set_mode (struct cuv_task *task, struct cuv_tty *tty,
                  uv_tty_mode_t mode)
{
  if (!task || !tty)
    return UV_EINVAL;

  if (tty->resource.owner != task)
    return UV_EPERM;

  if (tty->state != CUV_TTY_INITIALIZED)
    return UV_EBUSY;

  int error = uv_tty_set_mode (&tty->uv, mode);
  return error < 0 ? error : CUV_READY;
}

int
cuv_tty_get_winsize (struct cuv_task *task, struct cuv_tty *tty, int *width,
                     int *height)
{
  if (!task || !tty || !width || !height)
    return UV_EINVAL;

  if (tty->resource.owner != task)
    return UV_EPERM;

  if (tty->state != CUV_TTY_INITIALIZED)
    return UV_EBUSY;

  int error = uv_tty_get_winsize (&tty->uv, width, height);
  return error < 0 ? error : CUV_READY;
}

int
cuv_tty_close (struct cuv_task *task, struct cuv_tty *tty)
{
  if (!task || !tty)
    return UV_EINVAL;

  if (tty->state == CUV_TTY_NEW || tty->state == CUV_TTY_CLOSED)
    return CUV_READY;

  if (tty->resource.owner != task)
    return UV_EPERM;

  if (tty->reader || tty->close_waiter || tty->state == CUV_TTY_CLOSING)
    return UV_EBUSY;

  int error = uv_read_stop ((uv_stream_t *)&tty->uv);
  if (error < 0)
    return error;

  tty->close_waiter = task;
  tty->close_error = 0;
  tty->state = CUV_TTY_CLOSING;
  task->wait.cancel = null;
  uv_close ((uv_handle_t *)&tty->uv, cuv_tty_close_cb);
  return CUV_PENDING;
}

int
cuv_tty_reset_mode ()
{
  return uv_tty_reset_mode ();
}

int
cuv_tty_get_vterm_state (uv_tty_vtermstate_t *state)
{
  if (!state)
    return UV_EINVAL;

  return uv_tty_get_vterm_state (state);
}

int
cuv_tty_set_vterm_state (uv_tty_vtermstate_t state)
{
  if (state != UV_TTY_SUPPORTED && state != UV_TTY_UNSUPPORTED)
    return UV_EINVAL;

  uv_tty_set_vterm_state (state);
  return CUV_READY;
}
