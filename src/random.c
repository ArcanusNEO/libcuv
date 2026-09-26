#include "intern.h"

static void
cuv_random_cb (uv_random_t *req, int status, void *buffer, size_t length)
{
  (void)buffer;
  (void)length;

  struct cuv_task *task = cuv_task_from_req (req);
  cuv_task_wake (task, status);
}

int
cuv_random (struct cuv_task *task, void *buffer, usz length, unsigned flags)
{
  if (!task || (!buffer && length))
    return UV_EINVAL;

  auto req = (uv_random_t *)&task->wait.req;
  task->wait.cancel = cuv_cancel_req;

  int error
      = uv_random (&task->loop->uv, req, buffer, length, flags, cuv_random_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}
