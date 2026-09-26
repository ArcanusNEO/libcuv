#include "intern.h"

static void
cuv_file_cleanup_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  struct cuv_file *file = task->wait.operation.fs_file.file;
  int error = req->result < 0 ? req->result : 0;

  uv_fs_req_cleanup (req);

  if (error < 0)
    {
      uv_fs_t fallback = { 0 };
      int fallback_error
          = uv_fs_close (&task->loop->uv, &fallback, file->fd, null);
      uv_fs_req_cleanup (&fallback);

      if (fallback_error < 0 && task->error == 0)
        error = fallback_error;
    }

  file->open = 0;
  file->fd = -1;
  cuv_resource_detach (&file->resource);
  cuv_task_cleanup_resume (task, error);
}

static int
cuv_file_resource_close (struct cuv_resource *resource)
{
  auto file = container_of (resource, struct cuv_file, resource);
  struct cuv_task *owner = resource->owner;

  if (!file->open || !owner)
    {
      file->open = 0;
      file->fd = -1;
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  auto req = (uv_fs_t *)&owner->wait.req;
  owner->wait.operation.fs_file.file = file;

  int error
      = uv_fs_close (&owner->loop->uv, req, file->fd, cuv_file_cleanup_cb);
  if (error >= 0)
    return CUV_PENDING;

  uv_fs_t fallback = { 0 };
  int fallback_error
      = uv_fs_close (&owner->loop->uv, &fallback, file->fd, null);
  uv_fs_req_cleanup (&fallback);

  file->open = 0;
  file->fd = -1;
  cuv_resource_detach (resource);

  return fallback_error < 0 ? fallback_error : error;
}

static void
cuv_fs_open_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  int error = 0;

  if (req->result < 0)
    error = req->result;
  else
    {
      struct cuv_file *file = task->wait.operation.fs_file.file;

      file->fd = req->result;
      file->open = 1;
      file->resource.close = cuv_file_resource_close;
      file->resource.kind = CUV_RESOURCE_FILE;
      cuv_resource_attach (&file->resource, task);
    }

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

static void
cuv_fs_read_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  int error = 0;

  if (req->result < 0)
    error = req->result;
  else
    *task->wait.operation.fs_io.size = req->result;

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

static void
cuv_fs_write_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  int error = 0;

  if (req->result < 0)
    error = req->result;
  else
    *task->wait.operation.fs_io.size = req->result;

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

static void
cuv_fs_close_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  struct cuv_file *file = task->wait.operation.fs_file.file;
  int error = 0;

  if (req->result < 0)
    error = req->result;
  else
    {
      file->open = 0;
      file->fd = -1;
      cuv_resource_detach (&file->resource);
    }

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

int
cuv_fs_open (struct cuv_task *task, struct cuv_file *file, char const *path,
             int flags, int mode)
{
  if (!task || !file || !path)
    return UV_EINVAL;

  if (file->open || file->resource.owner)
    return UV_EBUSY;

  auto req = (uv_fs_t *)&task->wait.req;

  task->wait.operation.fs_file.file = file;
  task->wait.cancel = cuv_cancel_req;

  int error
      = uv_fs_open (&task->loop->uv, req, path, flags, mode, cuv_fs_open_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_fs_read (struct cuv_task *task, isz *size, struct cuv_file *file,
             void *buffer, usz capacity, i64 offset)
{
  if (!task || !size || !file || (!buffer && capacity))
    return UV_EINVAL;

  if (!file->open)
    return UV_EBADF;

  if (file->resource.owner != task)
    return UV_EPERM;

  if (capacity > UINT_MAX)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;

  task->wait.operation.fs_io.buffer = uv_buf_init (buffer, capacity);
  task->wait.operation.fs_io.size = size;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_read (&task->loop->uv, req, file->fd,
                          &task->wait.operation.fs_io.buffer, 1, offset,
                          cuv_fs_read_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_fs_write (struct cuv_task *task, isz *size, struct cuv_file *file,
              void const *buffer, usz length, i64 offset)
{
  if (!task || !size || !file || (!buffer && length))
    return UV_EINVAL;

  if (!file->open)
    return UV_EBADF;

  if (file->resource.owner != task)
    return UV_EPERM;

  if (length > UINT_MAX)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;

  task->wait.operation.fs_io.buffer = uv_buf_init ((char *)buffer, length);
  task->wait.operation.fs_io.size = size;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_write (&task->loop->uv, req, file->fd,
                           &task->wait.operation.fs_io.buffer, 1, offset,
                           cuv_fs_write_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_fs_close (struct cuv_task *task, struct cuv_file *file)
{
  if (!task || !file)
    return UV_EINVAL;

  if (!file->open)
    return CUV_READY;

  if (file->resource.owner != task)
    return UV_EPERM;

  auto req = (uv_fs_t *)&task->wait.req;

  task->wait.operation.fs_file.file = file;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_close (&task->loop->uv, req, file->fd, cuv_fs_close_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

static void
cuv_dirlist_release_storage (struct cuv_dirlist *list)
{
  if (!list)
    return;

  for (usz index = 0; index < list->count; ++index)
    free (list->entries[index].name);

  free (list->entries);
  list->entries = null;
  list->count = 0;
}

static int
cuv_dirlist_resource_close (struct cuv_resource *resource)
{
  auto list = container_of (resource, struct cuv_dirlist, resource);

  cuv_dirlist_release_storage (list);
  list->active = 0;
  cuv_resource_detach (resource);
  return CUV_READY;
}

void
cuv_dirlist_clear (struct cuv_dirlist *list)
{
  if (!list || list->active)
    return;

  if (list->resource.owner)
    cuv_resource_detach (&list->resource);

  cuv_dirlist_release_storage (list);
  list->resource.close = null;
}

static void
cuv_fs_stat_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  int error = req->result < 0 ? req->result : 0;

  if (!error)
    *task->wait.operation.fs_stat.stat = req->statbuf;

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

static void
cuv_fs_status_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  int error = req->result < 0 ? req->result : 0;

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

static void
cuv_fs_size_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  int error = req->result < 0 ? req->result : 0;

  if (!error)
    *task->wait.operation.fs_io.size = req->result;

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

static void
cuv_fs_scandir_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  struct cuv_dirlist *list = task->wait.operation.fs_scandir.list;
  int error = req->result < 0 ? req->result : 0;

  list->active = 0;

  usz count = req->result > 0 ? req->result : 0;

  if (!error && count)
    {
      if (count > SIZE_MAX / sizeof (*list->entries))
        error = UV_ENOMEM;
      else
        {
          list->entries = cuv_calloc (count, sizeof (*list->entries));
          if (!list->entries)
            error = UV_ENOMEM;
        }
    }

  if (!error)
    {

      for (usz index = 0; index < count; ++index)
        {
          uv_dirent_t entry;
          int next_error = uv_fs_scandir_next (req, &entry);

          if (next_error < 0)
            {
              error = next_error == UV_EOF ? UV_EIO : next_error;
              break;
            }

          usz length = strlen (entry.name) + 1;
          char *name = cuv_malloc (length);
          if (!name)
            {
              error = UV_ENOMEM;
              break;
            }

          memcpy (name, entry.name, length);
          list->entries[index].name = name;
          list->entries[index].type = entry.type;
          ++list->count;
        }
    }

  uv_fs_req_cleanup (req);

  if (error < 0)
    cuv_dirlist_release_storage (list);
  else
    {
      list->resource.close = cuv_dirlist_resource_close;
      list->resource.kind = CUV_RESOURCE_DIRLIST;
      cuv_resource_attach (&list->resource, task);
    }

  cuv_task_wake (task, error);
}

int
cuv_fs_stat (struct cuv_task *task, uv_stat_t *stat, char const *path)
{
  if (!task || !stat || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.operation.fs_stat.stat = stat;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_stat (&task->loop->uv, req, path, cuv_fs_stat_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_fs_lstat (struct cuv_task *task, uv_stat_t *stat, char const *path)
{
  if (!task || !stat || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.operation.fs_stat.stat = stat;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_lstat (&task->loop->uv, req, path, cuv_fs_stat_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_fstat (struct cuv_task *task, uv_stat_t *stat, struct cuv_file *file)
{
  if (!task || !stat || !file)
    return UV_EINVAL;

  if (!file->open)
    return UV_EBADF;

  if (file->resource.owner != task)
    return UV_EPERM;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.operation.fs_stat.stat = stat;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_fstat (&task->loop->uv, req, file->fd, cuv_fs_stat_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_fs_unlink (struct cuv_task *task, char const *path)
{
  if (!task || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_unlink (&task->loop->uv, req, path, cuv_fs_status_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_fs_rmdir (struct cuv_task *task, char const *path)
{
  if (!task || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_rmdir (&task->loop->uv, req, path, cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_rename (struct cuv_task *task, char const *path, char const *new_path)
{
  if (!task || !path || !new_path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  int error
      = uv_fs_rename (&task->loop->uv, req, path, new_path, cuv_fs_status_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_fs_mkdir (struct cuv_task *task, char const *path, int mode)
{
  if (!task || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_mkdir (&task->loop->uv, req, path, mode, cuv_fs_status_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

int
cuv_fs_scandir (struct cuv_task *task, struct cuv_dirlist *list,
                char const *path, int flags)
{
  if (!task || !list || !path)
    return UV_EINVAL;

  if (list->active || list->resource.owner || list->entries)
    return UV_EBUSY;

  auto req = (uv_fs_t *)&task->wait.req;

  list->active = 1;
  list->count = 0;
  task->wait.operation.fs_scandir.list = list;
  task->wait.cancel = cuv_cancel_req;

  int error
      = uv_fs_scandir (&task->loop->uv, req, path, flags, cuv_fs_scandir_cb);
  if (error < 0)
    {
      list->active = 0;
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}

static int
cuv_fs_check_file_owner (struct cuv_task *task, struct cuv_file *file)
{
  if (!task || !file)
    return UV_EINVAL;

  if (!file->open)
    return UV_EBADF;

  if (file->resource.owner != task)
    return UV_EPERM;

  return 0;
}

static int
cuv_fs_copy_path_result (char *buffer, usz capacity, char const *path)
{
  if (!buffer || !capacity || !path)
    return UV_EINVAL;

  usz length = strlen (path) + 1;
  if (length > capacity)
    {
      buffer[0] = 0;
      return UV_ENOBUFS;
    }

  memcpy (buffer, path, length);
  return 0;
}

static void
cuv_fs_ptr_path_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  int error = req->result < 0 ? req->result : 0;

  if (!error)
    error = cuv_fs_copy_path_result (task->wait.operation.fs_path.path,
                                     task->wait.operation.fs_path.capacity,
                                     req->ptr);

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

static void
cuv_fs_req_path_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  int error = req->result < 0 ? req->result : 0;

  if (!error)
    error = cuv_fs_copy_path_result (task->wait.operation.fs_path.path,
                                     task->wait.operation.fs_path.capacity,
                                     req->path);

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

static void
cuv_fs_mkstemp_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  struct cuv_file *file = task->wait.operation.fs_path.file;
  int error = req->result < 0 ? req->result : 0;

  if (!error)
    {
      file->fd = req->result;
      file->open = 1;
      file->resource.close = cuv_file_resource_close;
      file->resource.kind = CUV_RESOURCE_FILE;
      cuv_resource_attach (&file->resource, task);

      error = cuv_fs_copy_path_result (task->wait.operation.fs_path.path,
                                       task->wait.operation.fs_path.capacity,
                                       req->path);
    }

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

static void
cuv_fs_statfs_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  int error = req->result < 0 ? req->result : 0;

  if (!error)
    *task->wait.operation.fs_statfs.statfs = *(uv_statfs_t *)req->ptr;

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

static void
cuv_dir_cleanup_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  struct cuv_dir *dir = task->wait.operation.fs_dir.dir;
  int error = req->result < 0 ? req->result : 0;

  uv_fs_req_cleanup (req);

  if (error < 0 && dir->uv)
    {
      uv_fs_t fallback = { 0 };
      int fallback_error
          = uv_fs_closedir (&task->loop->uv, &fallback, dir->uv, null);
      uv_fs_req_cleanup (&fallback);

      if (fallback_error < 0 && task->error == 0)
        error = fallback_error;
    }

  dir->uv = null;
  dir->open = 0;
  cuv_resource_detach (&dir->resource);
  cuv_task_cleanup_resume (task, error);
}

static int
cuv_dir_resource_close (struct cuv_resource *resource)
{
  auto dir = container_of (resource, struct cuv_dir, resource);
  struct cuv_task *owner = resource->owner;

  if (!dir->open || !dir->uv || !owner)
    {
      dir->uv = null;
      dir->open = 0;
      cuv_resource_detach (resource);
      return CUV_READY;
    }

  auto req = (uv_fs_t *)&owner->wait.req;
  owner->wait.operation.fs_dir.dir = dir;

  int error
      = uv_fs_closedir (&owner->loop->uv, req, dir->uv, cuv_dir_cleanup_cb);
  if (error >= 0)
    return CUV_PENDING;

  uv_fs_t fallback = { 0 };
  int fallback_error
      = uv_fs_closedir (&owner->loop->uv, &fallback, dir->uv, null);
  uv_fs_req_cleanup (&fallback);

  dir->uv = null;
  dir->open = 0;
  cuv_resource_detach (resource);
  return fallback_error < 0 ? fallback_error : error;
}

static void
cuv_fs_opendir_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  struct cuv_dir *dir = task->wait.operation.fs_dir.dir;
  int error = req->result < 0 ? req->result : 0;

  if (!error)
    {
      dir->uv = req->ptr;
      dir->open = 1;
      dir->resource.close = cuv_dir_resource_close;
      dir->resource.kind = CUV_RESOURCE_DIR;
      cuv_resource_attach (&dir->resource, task);
    }

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

static void
cuv_fs_closedir_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  struct cuv_dir *dir = task->wait.operation.fs_dir.dir;
  int error = req->result < 0 ? req->result : 0;

  if (!error)
    {
      dir->uv = null;
      dir->open = 0;
      cuv_resource_detach (&dir->resource);
    }

  uv_fs_req_cleanup (req);
  cuv_task_wake (task, error);
}

static void
cuv_fs_readdir_cb (uv_fs_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);
  struct cuv_dir *dir = task->wait.operation.fs_readdir.dir;
  struct cuv_dirlist *list = task->wait.operation.fs_readdir.list;
  uv_dirent_t *entries = task->wait.operation.fs_readdir.entries;
  usz capacity = task->wait.operation.fs_readdir.capacity;
  int error = req->result < 0 ? req->result : 0;
  usz count = req->result > 0 ? req->result : 0;

  list->active = 0;

  if (!error && count > capacity)
    error = UV_EIO;

  if (!error && count)
    {
      if (count > SIZE_MAX / sizeof (*list->entries))
        error = UV_ENOMEM;
      else
        {
          list->entries = cuv_calloc (count, sizeof (*list->entries));
          if (!list->entries)
            error = UV_ENOMEM;
        }
    }

  if (!error)
    for (usz index = 0; index < count; ++index)
      {
        if (!entries[index].name)
          {
            error = UV_EIO;
            break;
          }

        usz length = strlen (entries[index].name) + 1;
        char *name = cuv_malloc (length);
        if (!name)
          {
            error = UV_ENOMEM;
            break;
          }

        memcpy (name, entries[index].name, length);
        list->entries[index].name = name;
        list->entries[index].type = entries[index].type;
        ++list->count;
      }

  uv_fs_req_cleanup (req);
  free (entries);

  if (dir->uv)
    {
      dir->uv->dirents = null;
      dir->uv->nentries = 0;
    }

  if (error < 0)
    cuv_dirlist_release_storage (list);
  else
    {
      list->resource.close = cuv_dirlist_resource_close;
      list->resource.kind = CUV_RESOURCE_DIRLIST;
      cuv_resource_attach (&list->resource, task);
    }

  cuv_task_wake (task, error);
}

int
cuv_fs_access (struct cuv_task *task, char const *path, int mode)
{
  if (!task || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  int error
      = uv_fs_access (&task->loop->uv, req, path, mode, cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_chmod (struct cuv_task *task, char const *path, int mode)
{
  if (!task || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_chmod (&task->loop->uv, req, path, mode, cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_fchmod (struct cuv_task *task, struct cuv_file *file, int mode)
{
  int error = cuv_fs_check_file_owner (task, file);
  if (error < 0)
    return error;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  error
      = uv_fs_fchmod (&task->loop->uv, req, file->fd, mode, cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_chown (struct cuv_task *task, char const *path, uv_uid_t uid,
              uv_gid_t gid)
{
  if (!task || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  int error
      = uv_fs_chown (&task->loop->uv, req, path, uid, gid, cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_lchown (struct cuv_task *task, char const *path, uv_uid_t uid,
               uv_gid_t gid)
{
  if (!task || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  int error
      = uv_fs_lchown (&task->loop->uv, req, path, uid, gid, cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_fchown (struct cuv_task *task, struct cuv_file *file, uv_uid_t uid,
               uv_gid_t gid)
{
  int error = cuv_fs_check_file_owner (task, file);
  if (error < 0)
    return error;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  error = uv_fs_fchown (&task->loop->uv, req, file->fd, uid, gid,
                        cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_fsync (struct cuv_task *task, struct cuv_file *file)
{
  int error = cuv_fs_check_file_owner (task, file);
  if (error < 0)
    return error;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  error = uv_fs_fsync (&task->loop->uv, req, file->fd, cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_fdatasync (struct cuv_task *task, struct cuv_file *file)
{
  int error = cuv_fs_check_file_owner (task, file);
  if (error < 0)
    return error;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  error = uv_fs_fdatasync (&task->loop->uv, req, file->fd, cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_ftruncate (struct cuv_task *task, struct cuv_file *file, i64 offset)
{
  int error = cuv_fs_check_file_owner (task, file);
  if (error < 0)
    return error;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  error = uv_fs_ftruncate (&task->loop->uv, req, file->fd, offset,
                           cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_sendfile (struct cuv_task *task, isz *size, struct cuv_file *output,
                 struct cuv_file *input, i64 offset, usz length)
{
  if (!size)
    return UV_EINVAL;

  int error = cuv_fs_check_file_owner (task, output);
  if (error < 0)
    return error;

  error = cuv_fs_check_file_owner (task, input);
  if (error < 0)
    return error;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.operation.fs_io.size = size;
  task->wait.cancel = cuv_cancel_req;

  error = uv_fs_sendfile (&task->loop->uv, req, output->fd, input->fd, offset,
                          length, cuv_fs_size_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_utime (struct cuv_task *task, char const *path, double atime,
              double mtime)
{
  if (!task || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;
  int error = uv_fs_utime (&task->loop->uv, req, path, atime, mtime,
                           cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_futime (struct cuv_task *task, struct cuv_file *file, double atime,
               double mtime)
{
  int error = cuv_fs_check_file_owner (task, file);
  if (error < 0)
    return error;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;
  error = uv_fs_futime (&task->loop->uv, req, file->fd, atime, mtime,
                        cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_lutime (struct cuv_task *task, char const *path, double atime,
               double mtime)
{
  if (!task || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;
  int error = uv_fs_lutime (&task->loop->uv, req, path, atime, mtime,
                            cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_readlink (struct cuv_task *task, char *buffer, usz capacity,
                 char const *path)
{
  if (!task || !buffer || !capacity || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.operation.fs_path.path = buffer;
  task->wait.operation.fs_path.capacity = capacity;
  task->wait.operation.fs_path.file = null;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_readlink (&task->loop->uv, req, path, cuv_fs_ptr_path_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_realpath (struct cuv_task *task, char *buffer, usz capacity,
                 char const *path)
{
  if (!task || !buffer || !capacity || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.operation.fs_path.path = buffer;
  task->wait.operation.fs_path.capacity = capacity;
  task->wait.operation.fs_path.file = null;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_realpath (&task->loop->uv, req, path, cuv_fs_ptr_path_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_link (struct cuv_task *task, char const *path, char const *new_path)
{
  if (!task || !path || !new_path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  int error
      = uv_fs_link (&task->loop->uv, req, path, new_path, cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_symlink (struct cuv_task *task, char const *path, char const *new_path,
                int flags)
{
  if (!task || !path || !new_path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_symlink (&task->loop->uv, req, path, new_path, flags,
                             cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_copyfile (struct cuv_task *task, char const *path, char const *new_path,
                 int flags)
{
  if (!task || !path || !new_path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_copyfile (&task->loop->uv, req, path, new_path, flags,
                              cuv_fs_status_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_mkdtemp (struct cuv_task *task, char *path, usz capacity,
                char const *template)
{
  if (!task || !path || !capacity || !template)
    return UV_EINVAL;

  usz template_length = strlen (template) + 1;
  if (template_length > capacity)
    return UV_ENOBUFS;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.operation.fs_path.path = path;
  task->wait.operation.fs_path.capacity = capacity;
  task->wait.operation.fs_path.file = null;
  task->wait.cancel = cuv_cancel_req;

  int error
      = uv_fs_mkdtemp (&task->loop->uv, req, template, cuv_fs_req_path_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_mkstemp (struct cuv_task *task, struct cuv_file *file, char *path,
                usz capacity, char const *template)
{
  if (!task || !file || !path || !capacity || !template)
    return UV_EINVAL;

  if (file->open || file->resource.owner)
    return UV_EBUSY;

  usz template_length = strlen (template) + 1;
  if (template_length > capacity)
    return UV_ENOBUFS;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.operation.fs_path.path = path;
  task->wait.operation.fs_path.capacity = capacity;
  task->wait.operation.fs_path.file = file;
  task->wait.cancel = cuv_cancel_req;

  int error
      = uv_fs_mkstemp (&task->loop->uv, req, template, cuv_fs_mkstemp_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_statfs (struct cuv_task *task, uv_statfs_t *statfs, char const *path)
{
  if (!task || !statfs || !path)
    return UV_EINVAL;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.operation.fs_statfs.statfs = statfs;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_statfs (&task->loop->uv, req, path, cuv_fs_statfs_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_opendir (struct cuv_task *task, struct cuv_dir *dir, char const *path)
{
  if (!task || !dir || !path)
    return UV_EINVAL;

  if (dir->open || dir->uv || dir->resource.owner)
    return UV_EBUSY;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.operation.fs_dir.dir = dir;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_opendir (&task->loop->uv, req, path, cuv_fs_opendir_cb);
  if (error < 0)
    task->wait.cancel = null;
  return error < 0 ? error : CUV_PENDING;
}

int
cuv_fs_readdir (struct cuv_task *task, struct cuv_dirlist *list,
                struct cuv_dir *dir, usz capacity)
{
  if (!task || !list || !dir || !capacity)
    return UV_EINVAL;

  if (!dir->open || !dir->uv)
    return UV_EBADF;

  if (dir->resource.owner != task)
    return UV_EPERM;

  if (list->active || list->resource.owner || list->entries)
    return UV_EBUSY;

  if (capacity > SIZE_MAX / sizeof (uv_dirent_t))
    return UV_ENOMEM;

  uv_dirent_t *entries = cuv_calloc (capacity, sizeof (uv_dirent_t));
  if (!entries)
    return UV_ENOMEM;

  dir->uv->dirents = entries;
  dir->uv->nentries = capacity;
  list->active = 1;
  list->count = 0;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.operation.fs_readdir.dir = dir;
  task->wait.operation.fs_readdir.list = list;
  task->wait.operation.fs_readdir.entries = entries;
  task->wait.operation.fs_readdir.capacity = capacity;
  task->wait.cancel = cuv_cancel_req;

  int error = uv_fs_readdir (&task->loop->uv, req, dir->uv, cuv_fs_readdir_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      list->active = 0;
      dir->uv->dirents = null;
      dir->uv->nentries = 0;
      free (entries);
      return error;
    }

  return CUV_PENDING;
}

int
cuv_fs_closedir (struct cuv_task *task, struct cuv_dir *dir)
{
  if (!task || !dir)
    return UV_EINVAL;

  if (!dir->open || !dir->uv)
    return CUV_READY;

  if (dir->resource.owner != task)
    return UV_EPERM;

  auto req = (uv_fs_t *)&task->wait.req;
  task->wait.operation.fs_dir.dir = dir;
  task->wait.cancel = cuv_cancel_req;

  int error
      = uv_fs_closedir (&task->loop->uv, req, dir->uv, cuv_fs_closedir_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}
