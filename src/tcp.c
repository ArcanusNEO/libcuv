#include "intern.h"

static void cuv_tcp_close_cb (uv_handle_t *handle);

static int
cuv_tcp_resource_close (struct cuv_resource *resource)
{
  auto tcp = container_of (resource, struct cuv_tcp, resource);
  struct cuv_task *owner = resource->owner;

  if (!owner)
    return CUV_READY;

  if (tcp->state == CUV_TCP_CLOSED || tcp->state == CUV_TCP_NEW)
    {
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  if (tcp->state == CUV_TCP_CLOSING)
    return CUV_PENDING;

  tcp->reader = null;
  tcp->acceptor = null;
  tcp->state = CUV_TCP_CLOSING;
  uv_close ((uv_handle_t *)&tcp->uv, cuv_tcp_close_cb);
  return CUV_PENDING;
}

static int
cuv_tcp_init_owned_ex (struct cuv_task *task, struct cuv_tcp *tcp,
                       unsigned flags, int extended)
{
  if (tcp->state == CUV_TCP_NEW)
    {
      int error = extended ? uv_tcp_init_ex (&task->loop->uv, &tcp->uv, flags)
                           : uv_tcp_init (&task->loop->uv, &tcp->uv);
      if (error < 0)
        return error;

      tcp->resource.close = cuv_tcp_resource_close;
      tcp->resource.kind = CUV_RESOURCE_TCP;
      tcp->state = CUV_TCP_INITIALIZED;
      cuv_resource_attach (&tcp->resource, task);
      return 0;
    }

  if (tcp->resource.owner != task)
    return UV_EPERM;

  if (tcp->state == CUV_TCP_CLOSING || tcp->state == CUV_TCP_CLOSED)
    return UV_EBUSY;

  return 0;
}

int
cuv_tcp_init_owned (struct cuv_task *task, struct cuv_tcp *tcp)
{
  return cuv_tcp_init_owned_ex (task, tcp, 0, 0);
}

static void
cuv_tcp_connect_cb (uv_connect_t *req, int status)
{
  struct cuv_task *task = cuv_task_from_req (req);
  struct cuv_tcp *tcp = task->wait.operation.tcp_connect.tcp;

  if (status == 0)
    tcp->state = CUV_TCP_CONNECTED;

  cuv_task_wake (task, status);
}

static int
cuv_tcp_accept_connection (struct cuv_task *task, struct cuv_tcp *server,
                           struct cuv_tcp *client)
{
  int error = cuv_tcp_init_owned (task, client);
  if (error < 0)
    return error;

  if (client->state != CUV_TCP_INITIALIZED)
    return UV_EBUSY;

  error = uv_accept ((uv_stream_t *)&server->uv, (uv_stream_t *)&client->uv);
  if (error < 0)
    return error;

  client->state = CUV_TCP_CONNECTED;
  server->connection_pending = 0;
  return 0;
}

static void
cuv_tcp_connection_cb (uv_stream_t *stream, int status)
{
  auto server = container_of (stream, struct cuv_tcp, uv);
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

  struct cuv_tcp *client = task->wait.operation.tcp_accept.client;
  server->acceptor = null;

  int error = cuv_tcp_accept_connection (task, server, client);
  cuv_task_wake (task, error);
}

static int
cuv_cancel_tcp_accept (struct cuv_task *task)
{
  struct cuv_tcp *server = task->wait.operation.tcp_accept.server;

  if (!server || server->acceptor != task)
    return UV_EBUSY;

  server->acceptor = null;
  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

static void
cuv_tcp_close_cb (uv_handle_t *handle)
{
  struct cuv_tcp *tcp = cuv_tcp_from_handle (handle);
  struct cuv_task *owner = tcp->resource.owner;
  struct cuv_task *waiter = tcp->close_waiter;

  tcp->close_waiter = null;
  tcp->reader = null;
  tcp->acceptor = null;
  tcp->connection_pending = 0;
  tcp->listen_error = 0;
  tcp->state = CUV_TCP_CLOSED;
  cuv_resource_detach (&tcp->resource);

  if (waiter)
    {
      cuv_task_wake (waiter, 0);
      return;
    }

  if (owner && owner->state == CUV_TASK_CLOSING)
    cuv_task_cleanup_resume (owner, 0);
}

int
cuv_tcp_init (struct cuv_task *task, struct cuv_tcp *tcp, unsigned flags)
{
  if (!task || !tcp)
    return UV_EINVAL;

  if (tcp->state != CUV_TCP_NEW)
    return tcp->resource.owner == task ? UV_EBUSY : UV_EPERM;

  int error = cuv_tcp_init_owned_ex (task, tcp, flags, 1);
  return error < 0 ? error : CUV_READY;
}

int
cuv_tcp_open (struct cuv_task *task, struct cuv_tcp *tcp, uv_os_sock_t socket)
{
  if (!task || !tcp)
    return UV_EINVAL;

  if (tcp->state != CUV_TCP_NEW)
    return tcp->resource.owner == task ? UV_EBUSY : UV_EPERM;

  int error = cuv_tcp_init_owned (task, tcp);
  if (error < 0)
    return error;

  error = uv_tcp_open (&tcp->uv, socket);
  if (error < 0)
    return error;

  struct sockaddr_storage peer;
  int length = sizeof (peer);
  error = uv_tcp_getpeername (&tcp->uv, (struct sockaddr *)&peer, &length);
  tcp->state = error == 0 ? CUV_TCP_CONNECTED : CUV_TCP_BOUND;
  return CUV_READY;
}

static int
cuv_tcp_name (struct cuv_task *task, struct cuv_tcp *tcp,
              struct sockaddr_storage *address,
              int (*getname) (uv_tcp_t const *, struct sockaddr *, int *))
{
  if (!task || !tcp || !address)
    return UV_EINVAL;

  if (tcp->resource.owner != task)
    return UV_EPERM;

  if (tcp->state == CUV_TCP_NEW || tcp->state == CUV_TCP_CLOSING
      || tcp->state == CUV_TCP_CLOSED)
    return UV_EBADF;

  memset (address, 0, sizeof (*address));
  int length = sizeof (*address);
  int error = getname (&tcp->uv, (struct sockaddr *)address, &length);
  return error < 0 ? error : CUV_READY;
}

int
cuv_tcp_getsockname (struct cuv_task *task, struct cuv_tcp *tcp,
                     struct sockaddr_storage *address)
{
  return cuv_tcp_name (task, tcp, address, uv_tcp_getsockname);
}

int
cuv_tcp_getpeername (struct cuv_task *task, struct cuv_tcp *tcp,
                     struct sockaddr_storage *address)
{
  return cuv_tcp_name (task, tcp, address, uv_tcp_getpeername);
}

int
cuv_tcp_connect (struct cuv_task *task, struct cuv_tcp *tcp,
                 struct sockaddr const *address)
{
  if (!task || !tcp || !address)
    return UV_EINVAL;

  int error = cuv_tcp_init_owned (task, tcp);
  if (error < 0)
    return error;

  if (tcp->state == CUV_TCP_CONNECTED || tcp->state == CUV_TCP_SHUTDOWN)
    return UV_EISCONN;

  if (tcp->state != CUV_TCP_INITIALIZED && tcp->state != CUV_TCP_BOUND)
    return UV_EBUSY;

  auto req = (uv_connect_t *)&task->wait.req;
  task->wait.operation.tcp_connect.tcp = tcp;
  task->wait.cancel = null;

  error = uv_tcp_connect (req, &tcp->uv, address, cuv_tcp_connect_cb);
  if (error < 0)
    return error;

  return CUV_PENDING;
}

int
cuv_tcp_bind (struct cuv_task *task, struct cuv_tcp *tcp,
              struct sockaddr const *address, unsigned flags)
{
  if (!task || !tcp || !address)
    return UV_EINVAL;

  int error = cuv_tcp_init_owned (task, tcp);
  if (error < 0)
    return error;

  if (tcp->state != CUV_TCP_INITIALIZED)
    return UV_EBUSY;

  error = uv_tcp_bind (&tcp->uv, address, flags);
  if (error < 0)
    return error;

  tcp->state = CUV_TCP_BOUND;
  return CUV_READY;
}

int
cuv_tcp_listen (struct cuv_task *task, struct cuv_tcp *tcp, int backlog)
{
  if (!task || !tcp || backlog < 0)
    return UV_EINVAL;

  if (tcp->resource.owner != task)
    return UV_EPERM;

  if (tcp->state != CUV_TCP_BOUND)
    return UV_EINVAL;

  int error
      = uv_listen ((uv_stream_t *)&tcp->uv, backlog, cuv_tcp_connection_cb);
  if (error < 0)
    return error;

  tcp->state = CUV_TCP_LISTENING;
  return CUV_READY;
}

int
cuv_tcp_accept (struct cuv_task *task, struct cuv_tcp *server,
                struct cuv_tcp *client)
{
  if (!task || !server || !client || server == client)
    return UV_EINVAL;

  if (server->resource.owner != task)
    return UV_EPERM;

  if (server->state != CUV_TCP_LISTENING)
    return UV_EINVAL;

  if (client->state != CUV_TCP_NEW)
    return UV_EBUSY;

  if (server->acceptor)
    return UV_EBUSY;

  if (server->listen_error < 0)
    {
      int error = server->listen_error;
      server->listen_error = 0;
      return error;
    }

  if (server->connection_pending)
    {
      int error = cuv_tcp_accept_connection (task, server, client);
      return error < 0 ? error : CUV_READY;
    }

  task->wait.operation.tcp_accept.server = server;
  task->wait.operation.tcp_accept.client = client;
  task->wait.cancel = cuv_cancel_tcp_accept;
  server->acceptor = task;
  return CUV_PENDING;
}

int
cuv_tcp_read (struct cuv_task *task, isz *size, struct cuv_tcp *tcp,
              void *buffer, usz capacity)
{
  if (!task || !size || !tcp || (!buffer && capacity))
    return UV_EINVAL;

  if (tcp->resource.owner != task)
    return UV_EPERM;

  if (tcp->state != CUV_TCP_CONNECTED)
    return UV_ENOTCONN;

  return cuv_stream_read (task, (uv_stream_t *)&tcp->uv, &tcp->reader, size,
                          buffer, capacity);
}

int
cuv_tcp_write (struct cuv_task *task, struct cuv_tcp *tcp, void const *buffer,
               usz length)
{
  if (!task || !tcp || (!buffer && length))
    return UV_EINVAL;

  if (tcp->resource.owner != task)
    return UV_EPERM;

  if (tcp->state != CUV_TCP_CONNECTED)
    return UV_ENOTCONN;

  return cuv_stream_write (task, (uv_stream_t *)&tcp->uv, buffer, length);
}

int
cuv_tcp_try_write (struct cuv_task *task, isz *size, struct cuv_tcp *tcp,
                   void const *buffer, usz length)
{
  if (!task || !size || !tcp || (!buffer && length))
    return UV_EINVAL;

  if (tcp->resource.owner != task)
    return UV_EPERM;

  if (tcp->state != CUV_TCP_CONNECTED)
    return UV_ENOTCONN;

  return cuv_stream_try_write (size, (uv_stream_t *)&tcp->uv, buffer, length);
}

static int
cuv_tcp_stream_ready (struct cuv_task *task, struct cuv_tcp *tcp)
{
  if (!task || !tcp)
    return UV_EINVAL;

  if (tcp->resource.owner != task)
    return UV_EPERM;

  if (tcp->state == CUV_TCP_NEW || tcp->state == CUV_TCP_CLOSING
      || tcp->state == CUV_TCP_CLOSED)
    return UV_EBADF;

  return 0;
}

int
cuv_tcp_write_queue (struct cuv_task *task, struct cuv_tcp *tcp, usz *size)
{
  if (!size)
    return UV_EINVAL;

  int error = cuv_tcp_stream_ready (task, tcp);
  if (error < 0)
    return error;

  return cuv_stream_write_queue (size, (uv_stream_t *)&tcp->uv);
}

int
cuv_tcp_is_readable (struct cuv_task *task, struct cuv_tcp *tcp, int *readable)
{
  if (!readable)
    return UV_EINVAL;

  int error = cuv_tcp_stream_ready (task, tcp);
  if (error < 0)
    return error;

  return cuv_stream_is_readable (readable, (uv_stream_t *)&tcp->uv);
}

int
cuv_tcp_is_writable (struct cuv_task *task, struct cuv_tcp *tcp, int *writable)
{
  if (!writable)
    return UV_EINVAL;

  int error = cuv_tcp_stream_ready (task, tcp);
  if (error < 0)
    return error;

  return cuv_stream_is_writable (writable, (uv_stream_t *)&tcp->uv);
}

int
cuv_tcp_set_blocking (struct cuv_task *task, struct cuv_tcp *tcp, int blocking)
{
  int error = cuv_tcp_stream_ready (task, tcp);
  if (error < 0)
    return error;

  return cuv_stream_set_blocking ((uv_stream_t *)&tcp->uv, blocking);
}

static int
cuv_tcp_buffer (struct cuv_task *task, struct cuv_tcp *tcp, int *size,
                int (*operation) (uv_handle_t *, int *))
{
  if (!size)
    return UV_EINVAL;

  int error = cuv_tcp_stream_ready (task, tcp);
  if (error < 0)
    return error;

  error = operation ((uv_handle_t *)&tcp->uv, size);
  return error < 0 ? error : CUV_READY;
}

int
cuv_tcp_send_buffer (struct cuv_task *task, struct cuv_tcp *tcp, int *size)
{
  return cuv_tcp_buffer (task, tcp, size, uv_send_buffer_size);
}

int
cuv_tcp_recv_buffer (struct cuv_task *task, struct cuv_tcp *tcp, int *size)
{
  return cuv_tcp_buffer (task, tcp, size, uv_recv_buffer_size);
}

int
cuv_tcp_shutdown (struct cuv_task *task, struct cuv_tcp *tcp)
{
  if (!task || !tcp)
    return UV_EINVAL;

  if (tcp->resource.owner != task)
    return UV_EPERM;

  if (tcp->state != CUV_TCP_CONNECTED)
    return UV_ENOTCONN;

  return cuv_stream_shutdown (task, (uv_stream_t *)&tcp->uv);
}

int
cuv_tcp_close (struct cuv_task *task, struct cuv_tcp *tcp)
{
  if (!task || !tcp)
    return UV_EINVAL;

  if (tcp->state == CUV_TCP_NEW || tcp->state == CUV_TCP_CLOSED)
    return CUV_READY;

  if (tcp->resource.owner != task)
    return UV_EPERM;

  if (tcp->reader || tcp->acceptor || tcp->close_waiter
      || tcp->state == CUV_TCP_CLOSING)
    return UV_EBUSY;

  tcp->close_waiter = task;
  tcp->state = CUV_TCP_CLOSING;
  task->wait.cancel = null;

  uv_close ((uv_handle_t *)&tcp->uv, cuv_tcp_close_cb);
  return CUV_PENDING;
}

int
cuv_tcp_close_reset (struct cuv_task *task, struct cuv_tcp *tcp)
{
  if (!task || !tcp)
    return UV_EINVAL;

  if (tcp->resource.owner != task)
    return UV_EPERM;

  if (tcp->state != CUV_TCP_CONNECTED)
    return tcp->state == CUV_TCP_SHUTDOWN ? UV_EINVAL : UV_ENOTCONN;

  if (tcp->reader || tcp->acceptor || tcp->close_waiter)
    return UV_EBUSY;

  int error = uv_tcp_close_reset (&tcp->uv, cuv_tcp_close_cb);
  if (error < 0)
    return error;

  tcp->close_waiter = task;
  tcp->state = CUV_TCP_CLOSING;
  task->wait.cancel = null;
  return CUV_PENDING;
}

int
cuv_tcp_nodelay (struct cuv_task *task, struct cuv_tcp *tcp, int enable)
{
  if (!task || !tcp)
    return UV_EINVAL;

  int error = cuv_tcp_init_owned (task, tcp);
  if (error < 0)
    return error;

  error = uv_tcp_nodelay (&tcp->uv, !!enable);
  return error < 0 ? error : CUV_READY;
}

int
cuv_tcp_keepalive (struct cuv_task *task, struct cuv_tcp *tcp, int enable,
                   unsigned delay)
{
  if (!task || !tcp)
    return UV_EINVAL;

  int error = cuv_tcp_init_owned (task, tcp);
  if (error < 0)
    return error;

  error = uv_tcp_keepalive (&tcp->uv, !!enable, delay);
  return error < 0 ? error : CUV_READY;
}

int
cuv_tcp_keepalive_ex (struct cuv_task *task, struct cuv_tcp *tcp, int enable,
                      unsigned idle, unsigned interval, unsigned count)
{
  if (!task || !tcp)
    return UV_EINVAL;

  int error = cuv_tcp_init_owned (task, tcp);
  if (error < 0)
    return error;

  error = uv_tcp_keepalive_ex (&tcp->uv, !!enable, idle, interval, count);
  return error < 0 ? error : CUV_READY;
}

int
cuv_tcp_simultaneous_accepts (struct cuv_task *task, struct cuv_tcp *tcp,
                              int enable)
{
  if (!task || !tcp)
    return UV_EINVAL;

  int error = cuv_tcp_init_owned (task, tcp);
  if (error < 0)
    return error;

  error = uv_tcp_simultaneous_accepts (&tcp->uv, !!enable);
  return error < 0 ? error : CUV_READY;
}
