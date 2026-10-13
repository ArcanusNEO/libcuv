#include "intern.h"

static struct cuv_task **
cuv_stream_reader_slot (uv_stream_t *stream)
{
  switch (uv_handle_get_type ((uv_handle_t *)stream))
    {
    case UV_TCP:
      {
        auto tcp = container_of (stream, struct cuv_tcp, uv);
        return &tcp->reader;
      }

    case UV_NAMED_PIPE:
      {
        auto pipe = container_of (stream, struct cuv_pipe, uv);
        return &pipe->reader;
      }

    case UV_TTY:
      {
        auto tty = container_of (stream, struct cuv_tty, uv);
        return &tty->reader;
      }

    default:
      return null;
    }
}

static void
cuv_stream_mark_shutdown (uv_stream_t *stream)
{
  switch (uv_handle_get_type ((uv_handle_t *)stream))
    {
    case UV_TCP:
      container_of (stream, struct cuv_tcp, uv)->state = CUV_TCP_SHUTDOWN;
      break;

    case UV_NAMED_PIPE:
      container_of (stream, struct cuv_pipe, uv)->state = CUV_PIPE_SHUTDOWN;
      break;

    default:
      break;
    }
}

static void
cuv_stream_alloc_cb (uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
  (void)suggested_size;

  auto stream = (uv_stream_t *)handle;
  struct cuv_task **reader = cuv_stream_reader_slot (stream);
  struct cuv_task *task = reader ? *reader : null;

  if (!task)
    {
      *buf = uv_buf_init (null, 0);
      return;
    }

  *buf = uv_buf_init (task->wait.operation.stream_read.buffer,
                      task->wait.operation.stream_read.capacity);
}

static void
cuv_stream_read_cb (uv_stream_t *stream, ssize_t nread, uv_buf_t const *buf)
{
  (void)buf;

  struct cuv_task **reader = cuv_stream_reader_slot (stream);
  struct cuv_task *task = reader ? *reader : null;

  if (!task || nread == 0)
    return;

  int stop_error = uv_read_stop (stream);
  *reader = null;

  if (stop_error < 0)
    {
      cuv_task_wake (task, stop_error);
      return;
    }

  if (nread == UV_EOF)
    {
      *task->wait.operation.stream_read.size = 0;
      cuv_task_wake (task, 0);
      return;
    }

  if (nread < 0)
    {
      cuv_task_wake (task, nread);
      return;
    }

  *task->wait.operation.stream_read.size = nread;
  cuv_task_wake (task, 0);
}

static int
cuv_cancel_stream_read (struct cuv_task *task)
{
  uv_stream_t *stream = task->wait.operation.stream_read.stream;
  struct cuv_task **reader = stream ? cuv_stream_reader_slot (stream) : null;

  if (!reader || *reader != task)
    return UV_EBUSY;

  int error = uv_read_stop (stream);
  if (error < 0)
    return error;

  *reader = null;
  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

static void
cuv_stream_write_cb (uv_write_t *req, int status)
{
  cuv_task_wake (cuv_task_from_req (req), status);
}

static void
cuv_stream_shutdown_cb (uv_shutdown_t *req, int status)
{
  struct cuv_task *task = cuv_task_from_req (req);

  if (status == 0)
    cuv_stream_mark_shutdown (task->wait.operation.stream_shutdown.stream);

  cuv_task_wake (task, status);
}

int
cuv_stream_read (struct cuv_task *task, uv_stream_t *stream,
                 struct cuv_task **reader, isz *size, void *buffer,
                 usz capacity)
{
  assert (task);
  assert (stream);
  assert (reader);
  assert (size);

  if (capacity > UINT_MAX)
    return UV_EINVAL;

  if (*reader)
    return UV_EBUSY;

  task->wait.operation.stream_read.stream = stream;
  task->wait.operation.stream_read.buffer = buffer;
  task->wait.operation.stream_read.capacity = capacity;
  task->wait.operation.stream_read.size = size;
  task->wait.cancel = cuv_cancel_stream_read;
  *reader = task;

  int error = uv_read_start (stream, cuv_stream_alloc_cb, cuv_stream_read_cb);
  if (error < 0)
    {
      *reader = null;
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_stream_write (struct cuv_task *task, uv_stream_t *stream,
                  void const *buffer, usz length)
{
  assert (task);
  assert (stream);

  if (length > UINT_MAX)
    return UV_EINVAL;

  auto req = (uv_write_t *)&task->wait.req;
  task->wait.operation.stream_write.buffer
      = uv_buf_init ((char *)buffer, length);
  task->wait.cancel = null;

  int error = uv_write (req, stream, &task->wait.operation.stream_write.buffer,
                        1, cuv_stream_write_cb);
  if (error < 0)
    return error;

  return CUV_PENDING;
}

int
cuv_stream_try_write (isz *size, uv_stream_t *stream, void const *buffer,
                      usz length)
{
  assert (size);
  assert (stream);

  if (length > UINT_MAX)
    return UV_EINVAL;

  uv_buf_t uv_buffer = uv_buf_init ((char *)buffer, length);
  int result = uv_try_write (stream, &uv_buffer, 1);
  if (result < 0)
    return result;

  *size = result;
  return CUV_READY;
}

int
cuv_stream_write_queue (usz *size, uv_stream_t *stream)
{
  assert (size);
  assert (stream);

  *size = uv_stream_get_write_queue_size (stream);
  return CUV_READY;
}

int
cuv_stream_is_readable (int *readable, uv_stream_t *stream)
{
  assert (readable);
  assert (stream);

  *readable = uv_is_readable (stream);
  return CUV_READY;
}

int
cuv_stream_is_writable (int *writable, uv_stream_t *stream)
{
  assert (writable);
  assert (stream);

  *writable = uv_is_writable (stream);
  return CUV_READY;
}

int
cuv_stream_set_blocking (uv_stream_t *stream, int blocking)
{
  assert (stream);

  int error = uv_stream_set_blocking (stream, !!blocking);
  return error < 0 ? error : CUV_READY;
}

int
cuv_stream_write2 (struct cuv_task *task, uv_stream_t *stream,
                   void const *buffer, usz length, uv_stream_t *send_handle)
{
  assert (task);
  assert (stream);
  assert (send_handle);

  if (length > UINT_MAX)
    return UV_EINVAL;

  auto req = (uv_write_t *)&task->wait.req;
  task->wait.operation.stream_write.buffer
      = uv_buf_init ((char *)buffer, length);
  task->wait.cancel = null;

  int error
      = uv_write2 (req, stream, &task->wait.operation.stream_write.buffer, 1,
                   send_handle, cuv_stream_write_cb);
  if (error < 0)
    return error;

  return CUV_PENDING;
}

int
cuv_stream_try_write2 (isz *size, uv_stream_t *stream, void const *buffer,
                       usz length, uv_stream_t *send_handle)
{
  assert (size);
  assert (stream);
  assert (send_handle);

  if (length > UINT_MAX)
    return UV_EINVAL;

  uv_buf_t uv_buffer = uv_buf_init ((char *)buffer, length);
  int result = uv_try_write2 (stream, &uv_buffer, 1, send_handle);
  if (result < 0)
    return result;

  *size = result;
  return CUV_READY;
}

int
cuv_stream_shutdown (struct cuv_task *task, uv_stream_t *stream)
{
  assert (task);
  assert (stream);

  auto req = (uv_shutdown_t *)&task->wait.req;
  task->wait.operation.stream_shutdown.stream = stream;
  task->wait.cancel = null;

  int error = uv_shutdown (req, stream, cuv_stream_shutdown_cb);
  if (error < 0)
    return error;

  return CUV_PENDING;
}
