#include "intern.h"

static void
cuv_work_cb (uv_work_t *req)
{
  struct cuv_task *task = cuv_task_from_req (req);

  task->wait.operation.work.function (task->wait.operation.work.argument);
}

static void
cuv_after_work_cb (uv_work_t *req, int status)
{
  struct cuv_task *task = cuv_task_from_req (req);
  cuv_task_wake (task, status);
}

int
cuv_work (struct cuv_task *task, void (*work) (void *), void *argument)
{
  if (!task || !work)
    return UV_EINVAL;

  auto req = (uv_work_t *)&task->wait.req;

  task->wait.operation.work.function = work;
  task->wait.operation.work.argument = argument;
  task->wait.cancel = cuv_cancel_req;

  int error
      = uv_queue_work (&task->loop->uv, req, cuv_work_cb, cuv_after_work_cb);
  if (error < 0)
    {
      task->wait.cancel = null;
      return error;
    }

  return CUV_PENDING;
}
