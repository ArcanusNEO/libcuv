#include "intern.h"

static void cuv_pipe_close_cb (uv_handle_t *handle);

static struct cuv_pipe *
cuv_pipe_from_handle (uv_handle_t *handle)
{
  return container_of (handle, struct cuv_pipe, uv);
}

static int
cuv_pipe_resource_close (struct cuv_resource *resource)
{
  auto pipe = container_of (resource, struct cuv_pipe, resource);

  if (!resource->owner)
    return CUV_READY;

  if (pipe->state == CUV_PIPE_NEW || pipe->state == CUV_PIPE_CLOSED)
    {
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  if (pipe->state == CUV_PIPE_CLOSING)
    return CUV_PENDING;

  if (pipe->reader)
    uv_read_stop ((uv_stream_t *)&pipe->uv);

  pipe->reader = null;
  pipe->acceptor = null;
  pipe->state = CUV_PIPE_CLOSING;
  uv_close ((uv_handle_t *)&pipe->uv, cuv_pipe_close_cb);
  return CUV_PENDING;
}

static void
cuv_pipe_refresh_state (struct cuv_pipe *pipe)
{
  if (pipe->state != CUV_PIPE_INITIALIZED)
    return;

  auto stream = (uv_stream_t *)&pipe->uv;
  if (uv_is_readable (stream) || uv_is_writable (stream))
    pipe->state = CUV_PIPE_ACTIVE;
}

static int
cuv_pipe_init_owned (struct cuv_task *task, struct cuv_pipe *pipe, int ipc)
{
  unsigned normalized_ipc = !!ipc;

  if (pipe->state == CUV_PIPE_NEW)
    {
      int error = uv_pipe_init (&task->loop->uv, &pipe->uv, normalized_ipc);
      if (error < 0)
        return error;

      pipe->resource.close = cuv_pipe_resource_close;
      pipe->resource.kind = CUV_RESOURCE_PIPE;
      pipe->state = CUV_PIPE_INITIALIZED;
      pipe->ipc = normalized_ipc;
      cuv_resource_attach (&pipe->resource, task);
      return 0;
    }

  if (pipe->resource.owner != task)
    return UV_EPERM;

  if (pipe->state == CUV_PIPE_CLOSING || pipe->state == CUV_PIPE_CLOSED)
    return UV_EBUSY;

  if (pipe->ipc != normalized_ipc)
    return UV_EINVAL;

  return 0;
}

static void
cuv_pipe_connect_cb (uv_connect_t *req, int status)
{
  struct cuv_task *task = cuv_task_from_req (req);
  struct cuv_pipe *pipe = task->wait.operation.pipe_connect.pipe;

  if (status == 0)
    pipe->state = CUV_PIPE_ACTIVE;

  cuv_task_wake (task, status);
}

static int
cuv_pipe_accept_connection (struct cuv_task *task, struct cuv_pipe *server,
                            struct cuv_pipe *client, unsigned ipc)
{
  int error = cuv_pipe_init_owned (task, client, ipc);
  if (error < 0)
    return error;

  if (client->state != CUV_PIPE_INITIALIZED)
    return UV_EBUSY;

  error = uv_accept ((uv_stream_t *)&server->uv, (uv_stream_t *)&client->uv);
  if (error < 0)
    return error;

  client->state = CUV_PIPE_ACTIVE;
  server->connection_pending = 0;
  return 0;
}

static void
cuv_pipe_connection_cb (uv_stream_t *stream, int status)
{
  auto server = container_of (stream, struct cuv_pipe, uv);
  struct cuv_task *task = server->acceptor;

  if (status < 0)
    {
      if (task)
        {
          server->acceptor = null;
          cuv_task_wake (task, status);
        }
      else
        server->listen_error = status;
      return;
    }

  if (!task)
    {
      server->connection_pending = 1;
      return;
    }

  struct cuv_pipe *client = task->wait.operation.pipe_accept.client;
  unsigned ipc = task->wait.operation.pipe_accept.ipc;
  server->acceptor = null;

  int error = cuv_pipe_accept_connection (task, server, client, ipc);
  cuv_task_wake (task, error);
}

static int
cuv_cancel_pipe_accept (struct cuv_task *task)
{
  struct cuv_pipe *server = task->wait.operation.pipe_accept.server;

  if (!server || server->acceptor != task)
    return UV_EBUSY;

  server->acceptor = null;
  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

static void
cuv_pipe_close_cb (uv_handle_t *handle)
{
  struct cuv_pipe *pipe = cuv_pipe_from_handle (handle);
  struct cuv_task *owner = pipe->resource.owner;
  struct cuv_task *waiter = pipe->close_waiter;

  pipe->reader = null;
  pipe->acceptor = null;
  pipe->close_waiter = null;
  pipe->connection_pending = 0;
  pipe->listen_error = 0;
  pipe->state = CUV_PIPE_CLOSED;
  cuv_resource_detach (&pipe->resource);

  if (waiter)
    {
      cuv_task_wake (waiter, 0);
      return;
    }

  if (owner && owner->state == CUV_TASK_CLOSING)
    cuv_task_cleanup_resume (owner, 0);
}

int
cuv_pipe_init (struct cuv_task *task, struct cuv_pipe *pipe, int ipc)
{
  if (!task || !pipe)
    return UV_EINVAL;

  int error = cuv_pipe_init_owned (task, pipe, ipc);
  if (error < 0)
    return error;

  return CUV_READY;
}

int
cuv_pipe_open (struct cuv_task *task, struct cuv_pipe *pipe, uv_file file,
               int ipc)
{
  if (!task || !pipe || file < 0)
    return UV_EINVAL;

  int error = cuv_pipe_init_owned (task, pipe, ipc);
  if (error < 0)
    return error;

  if (pipe->state != CUV_PIPE_INITIALIZED)
    return UV_EBUSY;

  error = uv_pipe_open (&pipe->uv, file);
  if (error < 0)
    return error;

  pipe->state = CUV_PIPE_ACTIVE;
  return CUV_READY;
}

int
cuv_pipe_connect (struct cuv_task *task, struct cuv_pipe *pipe,
                  char const *name, int ipc)
{
  if (!name)
    return UV_EINVAL;

  return cuv_pipe_connect2 (task, pipe, name, strlen (name), 0, ipc);
}

int
cuv_pipe_connect2 (struct cuv_task *task, struct cuv_pipe *pipe,
                   char const *name, usz name_length, unsigned flags, int ipc)
{
  if (!task || !pipe || !name)
    return UV_EINVAL;

  int error = cuv_pipe_init_owned (task, pipe, ipc);
  if (error < 0)
    return error;

  if (pipe->state != CUV_PIPE_INITIALIZED)
    return UV_EBUSY;

  auto req = (uv_connect_t *)&task->wait.req;
  task->wait.operation.pipe_connect.pipe = pipe;
  task->wait.cancel = null;

  error = uv_pipe_connect2 (req, &pipe->uv, name, name_length, flags,
                            cuv_pipe_connect_cb);
  if (error < 0)
    return error;

  return CUV_PENDING;
}

int
cuv_pipe_bind (struct cuv_task *task, struct cuv_pipe *pipe, char const *name,
               int ipc)
{
  if (!name)
    return UV_EINVAL;

  return cuv_pipe_bind2 (task, pipe, name, strlen (name), 0, ipc);
}

int
cuv_pipe_bind2 (struct cuv_task *task, struct cuv_pipe *pipe, char const *name,
                usz name_length, unsigned flags, int ipc)
{
  if (!task || !pipe || !name)
    return UV_EINVAL;

  int error = cuv_pipe_init_owned (task, pipe, ipc);
  if (error < 0)
    return error;

  if (pipe->state != CUV_PIPE_INITIALIZED)
    return UV_EBUSY;

  error = uv_pipe_bind2 (&pipe->uv, name, name_length, flags);
  if (error < 0)
    return error;

  pipe->state = CUV_PIPE_BOUND;
  return CUV_READY;
}

int
cuv_pipe_listen (struct cuv_task *task, struct cuv_pipe *pipe, int backlog)
{
  if (!task || !pipe || backlog < 0)
    return UV_EINVAL;

  if (pipe->resource.owner != task)
    return UV_EPERM;

  if (pipe->state != CUV_PIPE_BOUND)
    return UV_EINVAL;

  int error
      = uv_listen ((uv_stream_t *)&pipe->uv, backlog, cuv_pipe_connection_cb);
  if (error < 0)
    return error;

  pipe->state = CUV_PIPE_LISTENING;
  return CUV_READY;
}

int
cuv_pipe_accept (struct cuv_task *task, struct cuv_pipe *server,
                 struct cuv_pipe *client, int ipc)
{
  if (!task || !server || !client || server == client)
    return UV_EINVAL;

  if (server->resource.owner != task)
    return UV_EPERM;

  if (server->state != CUV_PIPE_LISTENING)
    return UV_EINVAL;

  if (client->state != CUV_PIPE_NEW)
    return UV_EBUSY;

  if (server->acceptor)
    return UV_EBUSY;

  if (server->listen_error < 0)
    {
      int error = server->listen_error;
      server->listen_error = 0;
      return error;
    }

  unsigned normalized_ipc = !!ipc;

  if (server->connection_pending)
    {
      int error
          = cuv_pipe_accept_connection (task, server, client, normalized_ipc);
      return error < 0 ? error : CUV_READY;
    }

  task->wait.operation.pipe_accept.server = server;
  task->wait.operation.pipe_accept.client = client;
  task->wait.operation.pipe_accept.ipc = normalized_ipc;
  task->wait.cancel = cuv_cancel_pipe_accept;
  server->acceptor = task;
  return CUV_PENDING;
}

int
cuv_pipe_read (struct cuv_task *task, isz *size, struct cuv_pipe *pipe,
               void *buffer, usz capacity)
{
  if (!task || !size || !pipe || (!buffer && capacity))
    return UV_EINVAL;

  if (pipe->resource.owner != task)
    return UV_EPERM;

  cuv_pipe_refresh_state (pipe);
  if (pipe->state != CUV_PIPE_ACTIVE)
    return UV_ENOTCONN;

  return cuv_stream_read (task, (uv_stream_t *)&pipe->uv, &pipe->reader, size,
                          buffer, capacity);
}

int
cuv_pipe_write (struct cuv_task *task, struct cuv_pipe *pipe,
                void const *buffer, usz length)
{
  if (!task || !pipe || (!buffer && length))
    return UV_EINVAL;

  if (pipe->resource.owner != task)
    return UV_EPERM;

  cuv_pipe_refresh_state (pipe);
  if (pipe->state != CUV_PIPE_ACTIVE)
    return UV_ENOTCONN;

  return cuv_stream_write (task, (uv_stream_t *)&pipe->uv, buffer, length);
}

int
cuv_pipe_try_write (struct cuv_task *task, isz *size, struct cuv_pipe *pipe,
                    void const *buffer, usz length)
{
  if (!task || !size || !pipe || (!buffer && length))
    return UV_EINVAL;

  if (pipe->resource.owner != task)
    return UV_EPERM;

  cuv_pipe_refresh_state (pipe);
  if (pipe->state != CUV_PIPE_ACTIVE)
    return UV_ENOTCONN;

  return cuv_stream_try_write (size, (uv_stream_t *)&pipe->uv, buffer, length);
}

static int
cuv_pipe_stream_ready (struct cuv_task *task, struct cuv_pipe *pipe)
{
  if (!task || !pipe)
    return UV_EINVAL;

  if (pipe->resource.owner != task)
    return UV_EPERM;

  if (pipe->state == CUV_PIPE_NEW || pipe->state == CUV_PIPE_CLOSING
      || pipe->state == CUV_PIPE_CLOSED)
    return UV_EBADF;

  return 0;
}

int
cuv_pipe_write_queue (struct cuv_task *task, struct cuv_pipe *pipe, usz *size)
{
  if (!size)
    return UV_EINVAL;

  int error = cuv_pipe_stream_ready (task, pipe);
  if (error < 0)
    return error;

  return cuv_stream_write_queue (size, (uv_stream_t *)&pipe->uv);
}

int
cuv_pipe_is_readable (struct cuv_task *task, struct cuv_pipe *pipe,
                      int *readable)
{
  if (!readable)
    return UV_EINVAL;

  int error = cuv_pipe_stream_ready (task, pipe);
  if (error < 0)
    return error;

  return cuv_stream_is_readable (readable, (uv_stream_t *)&pipe->uv);
}

int
cuv_pipe_is_writable (struct cuv_task *task, struct cuv_pipe *pipe,
                      int *writable)
{
  if (!writable)
    return UV_EINVAL;

  int error = cuv_pipe_stream_ready (task, pipe);
  if (error < 0)
    return error;

  return cuv_stream_is_writable (writable, (uv_stream_t *)&pipe->uv);
}

int
cuv_pipe_set_blocking (struct cuv_task *task, struct cuv_pipe *pipe,
                       int blocking)
{
  int error = cuv_pipe_stream_ready (task, pipe);
  if (error < 0)
    return error;

  return cuv_stream_set_blocking ((uv_stream_t *)&pipe->uv, blocking);
}

static int
cuv_pipe_ipc_ready (struct cuv_task *task, struct cuv_pipe *pipe)
{
  if (!task || !pipe)
    return UV_EINVAL;

  if (pipe->resource.owner != task)
    return UV_EPERM;

  cuv_pipe_refresh_state (pipe);
  if (pipe->state != CUV_PIPE_ACTIVE)
    return UV_ENOTCONN;

  if (!pipe->ipc)
    return UV_EINVAL;

  return 0;
}

static int
cuv_pipe_send_handle (struct cuv_task *task, struct cuv_pipe *pipe,
                      struct cuv_resource *resource, uv_stream_t **send_handle)
{
  int error = cuv_pipe_ipc_ready (task, pipe);
  if (error < 0)
    return error;

  if (!resource || !send_handle || resource == &pipe->resource)
    return UV_EINVAL;

  if (resource->owner != task)
    return UV_EPERM;

  switch (resource->kind)
    {
    case CUV_RESOURCE_TCP:
      {
        auto tcp = container_of (resource, struct cuv_tcp, resource);
        if (tcp->state == CUV_TCP_NEW || tcp->state == CUV_TCP_CLOSING
            || tcp->state == CUV_TCP_CLOSED)
          return UV_EBADF;
        *send_handle = (uv_stream_t *)&tcp->uv;
        return 0;
      }

    case CUV_RESOURCE_PIPE:
      {
        auto send_pipe = container_of (resource, struct cuv_pipe, resource);
        if (send_pipe->state == CUV_PIPE_NEW
            || send_pipe->state == CUV_PIPE_CLOSING
            || send_pipe->state == CUV_PIPE_CLOSED)
          return UV_EBADF;
        *send_handle = (uv_stream_t *)&send_pipe->uv;
        return 0;
      }

    case CUV_RESOURCE_UDP:
      {
        auto udp = container_of (resource, struct cuv_udp, resource);
        if (udp->state == CUV_UDP_NEW || udp->state == CUV_UDP_CLOSING
            || udp->state == CUV_UDP_CLOSED)
          return UV_EBADF;
        *send_handle = (uv_stream_t *)&udp->uv;
        return 0;
      }

    default:
      return UV_EINVAL;
    }
}

int
cuv_pipe_write2 (struct cuv_task *task, struct cuv_pipe *pipe,
                 void const *buffer, usz length, struct cuv_resource *send)
{
  if (!task || !pipe || !send || !length || (!buffer && length))
    return UV_EINVAL;

  uv_stream_t *send_handle;
  int error = cuv_pipe_send_handle (task, pipe, send, &send_handle);
  if (error < 0)
    return error;

  return cuv_stream_write2 (task, (uv_stream_t *)&pipe->uv, buffer, length,
                            send_handle);
}

int
cuv_pipe_try_write2 (struct cuv_task *task, isz *size, struct cuv_pipe *pipe,
                     void const *buffer, usz length, struct cuv_resource *send)
{
  if (!task || !size || !pipe || !send || !length || (!buffer && length))
    return UV_EINVAL;

  uv_stream_t *send_handle;
  int error = cuv_pipe_send_handle (task, pipe, send, &send_handle);
  if (error < 0)
    return error;

  return cuv_stream_try_write2 (size, (uv_stream_t *)&pipe->uv, buffer, length,
                                send_handle);
}

static int
cuv_pipe_name (struct cuv_task *task, struct cuv_pipe *pipe, char *buffer,
               usz *size, int (*getname) (uv_pipe_t const *, char *, size_t *))
{
  if (!task || !pipe || !size || (!buffer && *size))
    return UV_EINVAL;

  if (pipe->resource.owner != task)
    return UV_EPERM;

  if (pipe->state == CUV_PIPE_NEW || pipe->state == CUV_PIPE_CLOSING
      || pipe->state == CUV_PIPE_CLOSED)
    return UV_EBADF;

  int error = getname (&pipe->uv, buffer, size);
  return error < 0 ? error : CUV_READY;
}

int
cuv_pipe_getsockname (struct cuv_task *task, struct cuv_pipe *pipe,
                      char *buffer, usz *size)
{
  return cuv_pipe_name (task, pipe, buffer, size, uv_pipe_getsockname);
}

int
cuv_pipe_getpeername (struct cuv_task *task, struct cuv_pipe *pipe,
                      char *buffer, usz *size)
{
  return cuv_pipe_name (task, pipe, buffer, size, uv_pipe_getpeername);
}

int
cuv_pipe_pending_instances (struct cuv_task *task, struct cuv_pipe *pipe,
                            int count)
{
  if (!task || !pipe)
    return UV_EINVAL;

  if (pipe->resource.owner != task)
    return UV_EPERM;

  if (pipe->state == CUV_PIPE_NEW || pipe->state == CUV_PIPE_CLOSING
      || pipe->state == CUV_PIPE_CLOSED)
    return UV_EBADF;

  uv_pipe_pending_instances (&pipe->uv, count);
  return CUV_READY;
}

int
cuv_pipe_pending_count (struct cuv_task *task, struct cuv_pipe *pipe,
                        int *count)
{
  if (!count)
    return UV_EINVAL;

  int error = cuv_pipe_ipc_ready (task, pipe);
  if (error < 0)
    return error;

  *count = uv_pipe_pending_count (&pipe->uv);
  return CUV_READY;
}

int
cuv_pipe_pending_type (struct cuv_task *task, struct cuv_pipe *pipe,
                       uv_handle_type *type)
{
  if (!type)
    return UV_EINVAL;

  int error = cuv_pipe_ipc_ready (task, pipe);
  if (error < 0)
    return error;

  *type = uv_pipe_pending_type (&pipe->uv);
  return CUV_READY;
}

int
cuv_pipe_chmod (struct cuv_task *task, struct cuv_pipe *pipe, int flags)
{
  if (!task || !pipe)
    return UV_EINVAL;

  if (pipe->resource.owner != task)
    return UV_EPERM;

  if (pipe->state == CUV_PIPE_NEW || pipe->state == CUV_PIPE_CLOSING
      || pipe->state == CUV_PIPE_CLOSED)
    return UV_EBADF;

  int error = uv_pipe_chmod (&pipe->uv, flags);
  return error < 0 ? error : CUV_READY;
}

static int
cuv_pipe_pending_is (struct cuv_task *task, struct cuv_pipe *pipe,
                     uv_handle_type type)
{
  int error = cuv_pipe_ipc_ready (task, pipe);
  if (error < 0)
    return error;

  if (uv_pipe_pending_count (&pipe->uv) <= 0)
    return UV_EAGAIN;

  if (uv_pipe_pending_type (&pipe->uv) != type)
    return UV_EINVAL;

  return 0;
}

int
cuv_pipe_receive_tcp (struct cuv_task *task, struct cuv_pipe *pipe,
                      struct cuv_tcp *tcp)
{
  if (!tcp)
    return UV_EINVAL;

  int error = cuv_pipe_pending_is (task, pipe, UV_TCP);
  if (error < 0)
    return error;

  if (tcp->state != CUV_TCP_NEW)
    return UV_EBUSY;

  error = cuv_tcp_init_owned (task, tcp);
  if (error < 0)
    return error;

  error = uv_accept ((uv_stream_t *)&pipe->uv, (uv_stream_t *)&tcp->uv);
  if (error < 0)
    return error;

  struct sockaddr_storage peer;
  int length = sizeof (peer);
  error = uv_tcp_getpeername (&tcp->uv, (struct sockaddr *)&peer, &length);
  tcp->state = error == 0 ? CUV_TCP_CONNECTED : CUV_TCP_BOUND;
  return CUV_READY;
}

int
cuv_pipe_receive_pipe (struct cuv_task *task, struct cuv_pipe *pipe,
                       struct cuv_pipe *received, int ipc)
{
  if (!received || pipe == received)
    return UV_EINVAL;

  int error = cuv_pipe_pending_is (task, pipe, UV_NAMED_PIPE);
  if (error < 0)
    return error;

  if (received->state != CUV_PIPE_NEW)
    return UV_EBUSY;

  error = cuv_pipe_init_owned (task, received, ipc);
  if (error < 0)
    return error;

  error = uv_accept ((uv_stream_t *)&pipe->uv, (uv_stream_t *)&received->uv);
  if (error < 0)
    return error;

  char name[1];
  usz name_size = sizeof (name);
  error = uv_pipe_getpeername (&received->uv, name, &name_size);
  received->state
      = error == 0 || error == UV_ENOBUFS ? CUV_PIPE_ACTIVE : CUV_PIPE_BOUND;
  return CUV_READY;
}

int
cuv_pipe_receive_udp (struct cuv_task *task, struct cuv_pipe *pipe,
                      struct cuv_udp *udp)
{
  if (!udp)
    return UV_EINVAL;

  int error = cuv_pipe_pending_is (task, pipe, UV_UDP);
  if (error < 0)
    return error;

  if (udp->state != CUV_UDP_NEW)
    return UV_EBUSY;

  error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = uv_accept ((uv_stream_t *)&pipe->uv, (uv_stream_t *)&udp->uv);
  if (error < 0)
    return error;

  struct sockaddr_storage peer;
  int length = sizeof (peer);
  error = uv_udp_getpeername (&udp->uv, (struct sockaddr *)&peer, &length);
  udp->connected = error == 0;
  udp->state = CUV_UDP_BOUND;
  return CUV_READY;
}

int
cuv_pipe_shutdown (struct cuv_task *task, struct cuv_pipe *pipe)
{
  if (!task || !pipe)
    return UV_EINVAL;

  if (pipe->resource.owner != task)
    return UV_EPERM;

  cuv_pipe_refresh_state (pipe);
  if (pipe->state != CUV_PIPE_ACTIVE)
    return UV_ENOTCONN;

  return cuv_stream_shutdown (task, (uv_stream_t *)&pipe->uv);
}

int
cuv_pipe_close (struct cuv_task *task, struct cuv_pipe *pipe)
{
  if (!task || !pipe)
    return UV_EINVAL;

  if (pipe->state == CUV_PIPE_NEW || pipe->state == CUV_PIPE_CLOSED)
    return CUV_READY;

  if (pipe->resource.owner != task)
    return UV_EPERM;

  if (pipe->reader || pipe->acceptor || pipe->close_waiter
      || pipe->state == CUV_PIPE_CLOSING)
    return UV_EBUSY;

  pipe->close_waiter = task;
  pipe->state = CUV_PIPE_CLOSING;
  task->wait.cancel = null;

  uv_close ((uv_handle_t *)&pipe->uv, cuv_pipe_close_cb);
  return CUV_PENDING;
}
