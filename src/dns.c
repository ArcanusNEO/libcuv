#include "intern.h"

static void
cuv_resolve_cb (uv_getaddrinfo_t *req, int status, struct addrinfo *result)
{
  struct cuv_task *task = cuv_task_from_req (req);
  int error = status;

  if (status == 0)
    {
      struct addrinfo *entry = result;
      while (entry
             && !(entry->ai_family == AF_INET || entry->ai_family == AF_INET6))
        entry = entry->ai_next;

      if (!entry)
        error = UV_EAI_NONAME;
      else
        {
          usz size = entry->ai_family == AF_INET
                         ? sizeof (struct sockaddr_in)
                         : sizeof (struct sockaddr_in6);
          memcpy (task->wait.operation.resolve.address, entry->ai_addr, size);
        }
    }

  if (result)
    uv_freeaddrinfo (result);

  cuv_task_wake (task, error);
}

int
cuv_resolve (struct cuv_task *task, struct sockaddr_storage *address,
             char const *host, char const *service)
{
  if (!task || !address || !host)
    return UV_EINVAL;

  static struct addrinfo const hints = {
    .ai_family = AF_UNSPEC,
    .ai_socktype = SOCK_STREAM,
  };

  auto req = (uv_getaddrinfo_t *)&task->wait.req;

  task->wait.operation.resolve.address = address;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_getaddrinfo (&task->loop->uv, req, cuv_resolve_cb, host,
                              service, &hints);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

static void
cuv_getnameinfo_cb (uv_getnameinfo_t *req, int status, char const *hostname,
                    char const *service)
{
  struct cuv_task *task = cuv_task_from_req (req);
  char *host = task->wait.operation.getnameinfo.host;
  usz host_capacity = task->wait.operation.getnameinfo.host_capacity;
  char *output_service = task->wait.operation.getnameinfo.service;
  usz service_capacity = task->wait.operation.getnameinfo.service_capacity;
  int error = status;

  if (!error)
    {
      if ((host && !hostname) || (output_service && !service))
        error = UV_EAI_FAIL;
    }

  if (!error)
    {
      usz host_length = host ? strlen (hostname) + 1 : 0;
      usz service_length = output_service ? strlen (service) + 1 : 0;

      if ((host && host_length > host_capacity)
          || (output_service && service_length > service_capacity))
        error = UV_ENOBUFS;
      else
        {
          if (host)
            memcpy (host, hostname, host_length);

          if (output_service)
            memcpy (output_service, service, service_length);
        }
    }

  cuv_task_wake (task, error);
}

int
cuv_getnameinfo (struct cuv_task *task, char *host, usz host_capacity,
                 char *service, usz service_capacity,
                 struct sockaddr const *address, int flags)
{
  if (!task || !address)
    return UV_EINVAL;

  if ((!host && host_capacity) || (!service && service_capacity)
      || (host && !host_capacity) || (service && !service_capacity))
    return UV_EINVAL;

  if (!host && !service)
    return UV_EINVAL;

  auto req = (uv_getnameinfo_t *)&task->wait.req;

  task->wait.operation.getnameinfo.host = host;
  task->wait.operation.getnameinfo.host_capacity = host_capacity;
  task->wait.operation.getnameinfo.service = service;
  task->wait.operation.getnameinfo.service_capacity = service_capacity;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_getnameinfo (&task->loop->uv, req, cuv_getnameinfo_cb,
                              address, flags);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

static void
cuv_addrlist_release_storage (struct cuv_addrlist *list)
{
  if (!list)
    return;

  free (list->storage);
  list->storage = null;
  list->entries = null;
  list->count = 0;
}

static int
cuv_addrlist_resource_close (struct cuv_resource *resource)
{
  auto list = container_of (resource, struct cuv_addrlist, resource);

  cuv_addrlist_release_storage (list);
  list->active = 0;
  cuv_resource_detach (resource);
  return CUV_READY;
}

void
cuv_addrlist_clear (struct cuv_addrlist *list)
{
  if (!list || list->active)
    return;

  if (list->resource.owner)
    cuv_resource_detach (&list->resource);

  cuv_addrlist_release_storage (list);
  list->resource.close = null;
}

static int
cuv_addrlist_copy (struct cuv_addrlist *list, struct addrinfo *result)
{
  usz count = 0;
  usz canon_bytes = 0;

  for (struct addrinfo *entry = result; entry; entry = entry->ai_next)
    {
      if (entry->ai_addrlen > sizeof (struct sockaddr_storage))
        return UV_EOVERFLOW;

      if (entry->ai_addrlen && !entry->ai_addr)
        return UV_EAI_FAIL;

      if (count == SIZE_MAX)
        return UV_ENOMEM;
      ++count;

      if (entry->ai_canonname)
        {
          usz length = strlen (entry->ai_canonname) + 1;
          if (length > SIZE_MAX - canon_bytes)
            return UV_ENOMEM;
          canon_bytes += length;
        }
    }

  if (!count)
    return UV_EAI_NONAME;

  if (count > SIZE_MAX / sizeof (struct cuv_addrinfo))
    return UV_ENOMEM;

  usz entries_bytes = count * sizeof (struct cuv_addrinfo);
  if (canon_bytes > SIZE_MAX - entries_bytes)
    return UV_ENOMEM;

  usz total = entries_bytes + canon_bytes;
  void *storage = cuv_calloc (1, total);
  if (!storage)
    return UV_ENOMEM;

  struct cuv_addrinfo *entries = storage;
  char *text = storage;
  text += entries_bytes;
  usz index = 0;

  for (struct addrinfo *entry = result; entry; entry = entry->ai_next, ++index)
    {
      entries[index].flags = entry->ai_flags;
      entries[index].family = entry->ai_family;
      entries[index].socktype = entry->ai_socktype;
      entries[index].protocol = entry->ai_protocol;
      entries[index].address_length = entry->ai_addrlen;

      if (entry->ai_addr && entry->ai_addrlen)
        memcpy (&entries[index].address, entry->ai_addr, entry->ai_addrlen);

      if (entry->ai_canonname)
        {
          usz length = strlen (entry->ai_canonname) + 1;
          memcpy (text, entry->ai_canonname, length);
          entries[index].canonname = text;
          text += length;
        }
    }

  list->storage = storage;
  list->entries = entries;
  list->count = count;
  return 0;
}

static void
cuv_getaddrinfo_cb (uv_getaddrinfo_t *req, int status, struct addrinfo *result)
{
  struct cuv_task *task = cuv_task_from_req (req);
  struct cuv_addrlist *list = task->wait.operation.getaddrinfo.list;
  int error = status;

  list->active = 0;

  if (!error)
    error = cuv_addrlist_copy (list, result);

  if (result)
    uv_freeaddrinfo (result);

  if (error < 0)
    cuv_addrlist_release_storage (list);
  else
    {
      list->resource.close = cuv_addrlist_resource_close;
      list->resource.kind = CUV_RESOURCE_ADDRLIST;
      cuv_resource_attach (&list->resource, task);
    }

  cuv_task_wake (task, error);
}

int
cuv_getaddrinfo (struct cuv_task *task, struct cuv_addrlist *list,
                 char const *host, char const *service,
                 struct addrinfo const *hints)
{
  if (!task || !list || (!host && !service))
    return UV_EINVAL;

  if (list->active || list->resource.owner || list->storage || list->entries)
    return UV_EBUSY;

  auto req = (uv_getaddrinfo_t *)&task->wait.req;
  list->active = 1;
  list->count = 0;
  task->wait.operation.getaddrinfo.list = list;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_getaddrinfo (&task->loop->uv, req, cuv_getaddrinfo_cb, host,
                              service, hints);
  if (error < 0)
    {
      list->active = 0;
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}
