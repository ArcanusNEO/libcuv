#include "intern.h"

static void cuv_udp_close_cb (uv_handle_t *handle);

static struct cuv_udp *
cuv_udp_from_handle (uv_handle_t *handle)
{
  return container_of (handle, struct cuv_udp, uv);
}

static int
cuv_udp_resource_close (struct cuv_resource *resource)
{
  auto udp = container_of (resource, struct cuv_udp, resource);

  if (!resource->owner)
    return CUV_READY;

  if (udp->state == CUV_UDP_NEW || udp->state == CUV_UDP_CLOSED)
    {
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  if (udp->state == CUV_UDP_CLOSING)
    return CUV_PENDING;

  uv_udp_recv_stop (&udp->uv);
  udp->receiver = null;
  udp->state = CUV_UDP_CLOSING;
  uv_close ((uv_handle_t *)&udp->uv, cuv_udp_close_cb);
  return CUV_PENDING;
}

static int
cuv_udp_init_owned_ex (struct cuv_task *task, struct cuv_udp *udp,
                       unsigned flags, int extended)
{
  if (udp->state == CUV_UDP_NEW)
    {
      int error = extended ? uv_udp_init_ex (&task->loop->uv, &udp->uv, flags)
                           : uv_udp_init (&task->loop->uv, &udp->uv);
      if (error < 0)
        return error;

      udp->resource.close = cuv_udp_resource_close;
      udp->resource.kind = CUV_RESOURCE_UDP;
      udp->state = CUV_UDP_INITIALIZED;
      cuv_resource_attach (&udp->resource, task);
      return 0;
    }

  if (udp->resource.owner != task)
    return UV_EPERM;

  if (udp->state == CUV_UDP_CLOSING || udp->state == CUV_UDP_CLOSED)
    return UV_EBUSY;

  return 0;
}

int
cuv_udp_init_owned (struct cuv_task *task, struct cuv_udp *udp)
{
  return cuv_udp_init_owned_ex (task, udp, 0, 0);
}

static void
cuv_udp_send_cb (uv_udp_send_t *req, int status)
{
  struct cuv_task *task = cuv_task_from_req (req);
  cuv_task_wake (task, status);
}

static void
cuv_udp_alloc_cb (uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
  (void)suggested_size;

  struct cuv_udp *udp = cuv_udp_from_handle (handle);
  struct cuv_task *task = udp->receiver;

  if (!task)
    {
      *buf = uv_buf_init (null, 0);
      return;
    }

  *buf = uv_buf_init (task->wait.operation.udp_recv.buffer,
                      task->wait.operation.udp_recv.capacity);
}

static int
cuv_udp_copy_address (struct sockaddr_storage *destination,
                      struct sockaddr const *source)
{
  if (!destination || !source)
    return 0;

  memset (destination, 0, sizeof (*destination));

  if (source->sa_family == AF_INET)
    {
      memcpy (destination, source, sizeof (struct sockaddr_in));
      return 0;
    }

  if (source->sa_family == AF_INET6)
    {
      memcpy (destination, source, sizeof (struct sockaddr_in6));
      return 0;
    }

  return UV_EAFNOSUPPORT;
}

static void
cuv_udp_recv_cb (uv_udp_t *handle, ssize_t nread, uv_buf_t const *buf,
                 struct sockaddr const *addr, unsigned flags)
{
  (void)buf;

  auto udp = container_of (handle, struct cuv_udp, uv);
  struct cuv_task *task = udp->receiver;

  if (!task)
    return;

  if (nread == 0 && !addr)
    return;

  uv_udp_recv_stop (handle);
  udp->receiver = null;

  if (nread < 0)
    {
      cuv_task_wake (task, nread);
      return;
    }

  int error
      = cuv_udp_copy_address (task->wait.operation.udp_recv.address, addr);
  if (error < 0)
    {
      cuv_task_wake (task, error);
      return;
    }

  *task->wait.operation.udp_recv.size = nread;
  if (task->wait.operation.udp_recv.flags)
    *task->wait.operation.udp_recv.flags = flags;

  cuv_task_wake (task, 0);
}

static int
cuv_cancel_udp_recv (struct cuv_task *task)
{
  struct cuv_udp *udp = task->wait.operation.udp_recv.udp;

  if (!udp || udp->receiver != task)
    return UV_EBUSY;

  int error = uv_udp_recv_stop (&udp->uv);
  if (error < 0)
    return error;

  udp->receiver = null;
  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

static unsigned int
cuv_udp_batch_buffer_length (struct cuv_udp_batch const *batch,
                             unsigned using_recvmmsg)
{
  usz length = batch->buffer_capacity;

  if (using_recvmmsg)
    {
      usz maximum = batch->capacity > UINT_MAX / CUV_UDP_BATCH_SLOT_SIZE
                        ? (usz)UINT_MAX
                        : batch->capacity * CUV_UDP_BATCH_SLOT_SIZE;
      if (length > maximum)
        length = maximum;
    }

  if (length > UINT_MAX)
    length = UINT_MAX;

  return length;
}

static void
cuv_udp_recv_many_alloc_cb (uv_handle_t *handle, size_t suggested_size,
                            uv_buf_t *buf)
{
  (void)suggested_size;

  struct cuv_udp *udp = cuv_udp_from_handle (handle);
  struct cuv_task *task = udp->receiver;

  if (!task)
    {
      *buf = uv_buf_init (null, 0);
      return;
    }

  struct cuv_udp_batch *batch = task->wait.operation.udp_recv_many.batch;
  unsigned length = cuv_udp_batch_buffer_length (
      batch, task->wait.operation.udp_recv_many.using_recvmmsg);
  *buf = uv_buf_init ((char *)batch->buffer, length);
}

static void
cuv_udp_recv_many_finish (struct cuv_task *task, int error)
{
  struct cuv_udp *udp = task->wait.operation.udp_recv_many.udp;
  int stop_error = uv_udp_recv_stop (&udp->uv);

  udp->receiver = null;
  if (error >= 0 && stop_error < 0)
    error = stop_error;

  cuv_task_wake (task, error);
}

static void
cuv_udp_recv_many_cb (uv_udp_t *handle, ssize_t nread, uv_buf_t const *buf,
                      struct sockaddr const *addr, unsigned flags)
{
  auto udp = container_of (handle, struct cuv_udp, uv);
  struct cuv_task *task = udp->receiver;

  if (!task)
    return;

  struct cuv_udp_batch *batch = task->wait.operation.udp_recv_many.batch;

  if (flags & UV_UDP_MMSG_FREE)
    {
      cuv_udp_recv_many_finish (task, 0);
      return;
    }

  if (nread < 0)
    {
      cuv_udp_recv_many_finish (task, nread);
      return;
    }

  if (nread == 0 && !addr)
    return;

  if (batch->count >= batch->capacity)
    {
      cuv_udp_recv_many_finish (task, 0);
      return;
    }

  struct cuv_udp_message *message = &batch->messages[batch->count];
  int error = cuv_udp_copy_address (&message->address, addr);
  if (error < 0)
    {
      cuv_udp_recv_many_finish (task, error);
      return;
    }

  message->data = buf ? buf->base : null;
  message->size
      = nread > 0 ? ((usz)nread < (usz)buf->len ? (usz)nread : (usz)buf->len)
                  : 0;
  message->flags = flags;
  ++batch->count;

  if (!(flags & UV_UDP_MMSG_CHUNK))
    cuv_udp_recv_many_finish (task, 0);
}

static int
cuv_cancel_udp_recv_many (struct cuv_task *task)
{
  struct cuv_udp *udp = task->wait.operation.udp_recv_many.udp;

  if (!udp || udp->receiver != task)
    return UV_EBUSY;

  int error = uv_udp_recv_stop (&udp->uv);
  if (error < 0)
    return error;

  udp->receiver = null;
  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

static void
cuv_udp_close_cb (uv_handle_t *handle)
{
  struct cuv_udp *udp = cuv_udp_from_handle (handle);
  struct cuv_task *owner = udp->resource.owner;
  struct cuv_task *waiter = udp->close_waiter;

  udp->receiver = null;
  udp->close_waiter = null;
  udp->state = CUV_UDP_CLOSED;
  cuv_resource_detach (&udp->resource);

  if (waiter)
    {
      cuv_task_wake (waiter, 0);
      return;
    }

  if (owner && owner->state == CUV_TASK_CLOSING)
    cuv_task_cleanup_resume (owner, 0);
}

int
cuv_udp_init (struct cuv_task *task, struct cuv_udp *udp, unsigned flags)
{
  if (!task || !udp)
    return UV_EINVAL;

  if (udp->state != CUV_UDP_NEW)
    return udp->resource.owner == task ? UV_EBUSY : UV_EPERM;

  int error = cuv_udp_init_owned_ex (task, udp, flags, 1);
  return error < 0 ? error : CUV_READY;
}

int
cuv_udp_open (struct cuv_task *task, struct cuv_udp *udp, uv_os_sock_t socket)
{
  if (!task || !udp)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = uv_udp_open (&udp->uv, socket);
  return error < 0 ? error : CUV_READY;
}

int
cuv_udp_open_ex (struct cuv_task *task, struct cuv_udp *udp,
                 uv_os_sock_t socket, unsigned flags)
{
  if (!task || !udp)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = uv_udp_open_ex (&udp->uv, socket, flags);
  return error < 0 ? error : CUV_READY;
}

int
cuv_udp_bind (struct cuv_task *task, struct cuv_udp *udp,
              struct sockaddr const *address, unsigned flags)
{
  if (!task || !udp || !address)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  if (udp->state != CUV_UDP_INITIALIZED)
    return UV_EBUSY;

  error = uv_udp_bind (&udp->uv, address, flags);
  if (error < 0)
    return error;

  udp->state = CUV_UDP_BOUND;
  return CUV_READY;
}

int
cuv_udp_connect (struct cuv_task *task, struct cuv_udp *udp,
                 struct sockaddr const *address)
{
  if (!task || !udp)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = uv_udp_connect (&udp->uv, address);
  if (error < 0)
    return error;

  udp->connected = address != null;
  return CUV_READY;
}

int
cuv_udp_send (struct cuv_task *task, struct cuv_udp *udp, void const *buffer,
              usz length, struct sockaddr const *address)
{
  if (!task || !udp || (!buffer && length))
    return UV_EINVAL;

  if (length > UINT_MAX)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  auto req = (uv_udp_send_t *)&task->wait.req;
  task->wait.operation.udp_send.buffer = uv_buf_init ((char *)buffer, length);
  task->wait.cancel = null;

  error = uv_udp_send (req, &udp->uv, &task->wait.operation.udp_send.buffer, 1,
                       address, cuv_udp_send_cb);
  if (error < 0)
    return error;

  return CUV_PENDING;
}

int
cuv_udp_recv (struct cuv_task *task, isz *size, struct cuv_udp *udp,
              void *buffer, usz capacity, struct sockaddr_storage *address,
              unsigned *flags)
{
  if (!task || !size || !udp || (!buffer && capacity))
    return UV_EINVAL;

  if (capacity > UINT_MAX)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  if (udp->receiver)
    return UV_EBUSY;

  task->wait.operation.udp_recv.udp = udp;
  task->wait.operation.udp_recv.buffer = buffer;
  task->wait.operation.udp_recv.capacity = capacity;
  task->wait.operation.udp_recv.size = size;
  task->wait.operation.udp_recv.address = address;
  task->wait.operation.udp_recv.flags = flags;
  task->wait.cancel = cuv_cancel_udp_recv;

  udp->receiver = task;

  error = uv_udp_recv_start (&udp->uv, cuv_udp_alloc_cb, cuv_udp_recv_cb);
  if (error < 0)
    {
      udp->receiver = null;
      task->wait.cancel = null;
      return error;
    }

  if (udp->state == CUV_UDP_INITIALIZED)
    udp->state = CUV_UDP_BOUND;

  return CUV_PENDING;
}

int
cuv_udp_recv_many (struct cuv_task *task, struct cuv_udp *udp,
                   struct cuv_udp_batch *batch)
{
  if (!task || !udp || !batch || !batch->messages || !batch->capacity
      || !batch->buffer || !batch->buffer_capacity)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  if (udp->receiver)
    return UV_EBUSY;

  unsigned using_recvmmsg = uv_udp_using_recvmmsg (&udp->uv);
  unsigned length = cuv_udp_batch_buffer_length (batch, using_recvmmsg);
  if (!length)
    return UV_ENOBUFS;
  if (using_recvmmsg && length < CUV_UDP_BATCH_SLOT_SIZE)
    return UV_ENOBUFS;

  batch->count = 0;
  task->wait.operation.udp_recv_many.udp = udp;
  task->wait.operation.udp_recv_many.batch = batch;
  task->wait.operation.udp_recv_many.using_recvmmsg = using_recvmmsg;
  task->wait.cancel = cuv_cancel_udp_recv_many;
  udp->receiver = task;

  error = uv_udp_recv_start (&udp->uv, cuv_udp_recv_many_alloc_cb,
                             cuv_udp_recv_many_cb);
  if (error < 0)
    {
      udp->receiver = null;
      task->wait.cancel = null;
      return error;
    }

  if (udp->state == CUV_UDP_INITIALIZED)
    udp->state = CUV_UDP_BOUND;

  return CUV_PENDING;
}

int
cuv_udp_close (struct cuv_task *task, struct cuv_udp *udp)
{
  if (!task || !udp)
    return UV_EINVAL;

  if (udp->state == CUV_UDP_NEW || udp->state == CUV_UDP_CLOSED)
    return CUV_READY;

  if (udp->resource.owner != task)
    return UV_EPERM;

  if (udp->receiver || udp->close_waiter || udp->state == CUV_UDP_CLOSING)
    return UV_EBUSY;

  udp->close_waiter = task;
  udp->state = CUV_UDP_CLOSING;
  task->wait.cancel = null;

  uv_close ((uv_handle_t *)&udp->uv, cuv_udp_close_cb);
  return CUV_PENDING;
}

int
cuv_udp_set_membership (struct cuv_task *task, struct cuv_udp *udp,
                        char const *multicast_address,
                        char const *interface_address,
                        uv_membership membership)
{
  if (!task || !udp || !multicast_address)
    return UV_EINVAL;

  if (membership != UV_JOIN_GROUP && membership != UV_LEAVE_GROUP)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = uv_udp_set_membership (&udp->uv, multicast_address,
                                 interface_address, membership);
  return error < 0 ? error : CUV_READY;
}

int
cuv_udp_set_source_membership (struct cuv_task *task, struct cuv_udp *udp,
                               char const *multicast_address,
                               char const *interface_address,
                               char const *source_address,
                               uv_membership membership)
{
  if (!task || !udp || !multicast_address || !source_address)
    return UV_EINVAL;

  if (membership != UV_JOIN_GROUP && membership != UV_LEAVE_GROUP)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = uv_udp_set_source_membership (&udp->uv, multicast_address,
                                        interface_address, source_address,
                                        membership);
  return error < 0 ? error : CUV_READY;
}

int
cuv_udp_set_multicast_loop (struct cuv_task *task, struct cuv_udp *udp,
                            int enable)
{
  if (!task || !udp)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = uv_udp_set_multicast_loop (&udp->uv, !!enable);
  return error < 0 ? error : CUV_READY;
}

int
cuv_udp_set_multicast_ttl (struct cuv_task *task, struct cuv_udp *udp, int ttl)
{
  if (!task || !udp)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = uv_udp_set_multicast_ttl (&udp->uv, ttl);
  return error < 0 ? error : CUV_READY;
}

int
cuv_udp_set_multicast_interface (struct cuv_task *task, struct cuv_udp *udp,
                                 char const *interface_address)
{
  if (!task || !udp)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = uv_udp_set_multicast_interface (&udp->uv, interface_address);
  return error < 0 ? error : CUV_READY;
}

int
cuv_udp_set_broadcast (struct cuv_task *task, struct cuv_udp *udp, int enable)
{
  if (!task || !udp)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = uv_udp_set_broadcast (&udp->uv, !!enable);
  return error < 0 ? error : CUV_READY;
}

int
cuv_udp_set_ttl (struct cuv_task *task, struct cuv_udp *udp, int ttl)
{
  if (!task || !udp)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = uv_udp_set_ttl (&udp->uv, ttl);
  return error < 0 ? error : CUV_READY;
}

static int
cuv_udp_copy_name (struct sockaddr_storage *address,
                   int (*getname) (uv_udp_t const *, struct sockaddr *, int *),
                   struct cuv_udp const *udp)
{
  memset (address, 0, sizeof (*address));
  int length = sizeof (*address);
  return getname (&udp->uv, (struct sockaddr *)address, &length);
}

int
cuv_udp_getsockname (struct cuv_task *task, struct cuv_udp *udp,
                     struct sockaddr_storage *address)
{
  if (!task || !udp || !address)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = cuv_udp_copy_name (address, uv_udp_getsockname, udp);
  return error < 0 ? error : CUV_READY;
}

int
cuv_udp_getpeername (struct cuv_task *task, struct cuv_udp *udp,
                     struct sockaddr_storage *address)
{
  if (!task || !udp || !address)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = cuv_udp_copy_name (address, uv_udp_getpeername, udp);
  return error < 0 ? error : CUV_READY;
}

int
cuv_udp_try_send (struct cuv_task *task, isz *size, struct cuv_udp *udp,
                  void const *buffer, usz length,
                  struct sockaddr const *address)
{
  if (!task || !size || !udp || (!buffer && length))
    return UV_EINVAL;

  if (length > UINT_MAX)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  uv_buf_t uv_buffer = uv_buf_init ((char *)buffer, length);
  int result = uv_udp_try_send (&udp->uv, &uv_buffer, 1, address);
  if (result < 0)
    return result;

  *size = result;
  return CUV_READY;
}

int
cuv_udp_try_send2 (struct cuv_task *task, int *sent, struct cuv_udp *udp,
                   unsigned count, uv_buf_t *buffers[],
                   unsigned buffer_counts[], struct sockaddr *addresses[],
                   unsigned flags)
{
  if (!task || !sent || !udp || !count || !buffers || !buffer_counts
      || !addresses)
    return UV_EINVAL;

  for (unsigned i = 0u; i < count; ++i)
    if (buffer_counts[i] && !buffers[i])
      return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  int result = uv_udp_try_send2 (&udp->uv, count, buffers, buffer_counts,
                                 addresses, flags);
  if (result < 0)
    return result;

  *sent = result;
  return CUV_READY;
}

int
cuv_udp_using_recvmmsg (struct cuv_task *task, struct cuv_udp *udp,
                        int *enabled)
{
  if (!task || !udp || !enabled)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  *enabled = uv_udp_using_recvmmsg (&udp->uv);
  return CUV_READY;
}

int
cuv_udp_send_queue (struct cuv_task *task, struct cuv_udp *udp, usz *size,
                    usz *count)
{
  if (!task || !udp || (!size && !count))
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  if (size)
    *size = uv_udp_get_send_queue_size (&udp->uv);
  if (count)
    *count = uv_udp_get_send_queue_count (&udp->uv);

  return CUV_READY;
}

static int
cuv_udp_buffer (struct cuv_task *task, struct cuv_udp *udp, int *size,
                int (*operation) (uv_handle_t *, int *))
{
  if (!task || !udp || !size)
    return UV_EINVAL;

  int error = cuv_udp_init_owned (task, udp);
  if (error < 0)
    return error;

  error = operation ((uv_handle_t *)&udp->uv, size);
  return error < 0 ? error : CUV_READY;
}

int
cuv_udp_send_buffer (struct cuv_task *task, struct cuv_udp *udp, int *size)
{
  return cuv_udp_buffer (task, udp, size, uv_send_buffer_size);
}

int
cuv_udp_recv_buffer (struct cuv_task *task, struct cuv_udp *udp, int *size)
{
  return cuv_udp_buffer (task, udp, size, uv_recv_buffer_size);
}
