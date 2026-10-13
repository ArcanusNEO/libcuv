#include "intern.h"

#define CUV_TIMER_HEAP_DEGREE 4

static u64
cuv_timer_deadline (struct cuv_loop *loop, u64 milliseconds)
{
  uv_update_time (&loop->uv);
  u64 now = uv_now (&loop->uv);

  return UINT64_MAX - now < milliseconds ? UINT64_MAX : now + milliseconds;
}

static int
cuv_timer_compare (void const *lhs, void const *rhs)
{
  struct cuv_task const *const *left = lhs;
  struct cuv_task const *const *right = rhs;

  if ((*left)->timer.deadline < (*right)->timer.deadline)
    return -1;
  if ((*left)->timer.deadline > (*right)->timer.deadline)
    return 1;
  return 0;
}

static void cuv_timer_cb (uv_timer_t *timer);

static int
cuv_timer_reserve (struct cuv_loop *loop, usz capacity)
{
  if (capacity <= loop->timer_capacity)
    return 0;

  usz new_capacity = loop->timer_capacity ? loop->timer_capacity * 2 : 16;
  if (new_capacity < capacity)
    new_capacity = capacity;
  if (new_capacity > SIZE_MAX / sizeof (*loop->timer_heap))
    return UV_ENOMEM;

  void *memory = cuv_realloc (loop->timer_heap,
                              new_capacity * sizeof (*loop->timer_heap));
  if (!memory)
    return UV_ENOMEM;

  loop->timer_heap = memory;
  loop->timer_capacity = new_capacity;
  return 0;
}

static struct cuv_task *
cuv_timer_pop (struct cuv_loop *loop)
{
  assert (loop->timer_count);

  struct cuv_task *task = loop->timer_heap[0];
  --loop->timer_count;

  if (loop->timer_count)
    {
      loop->timer_heap[0] = loop->timer_heap[loop->timer_count];
      heap$ (siftdown) (loop->timer_heap, loop->timer_count,
                        sizeof (*loop->timer_heap), cuv_timer_compare,
                        CUV_TIMER_HEAP_DEGREE, 0);
    }

  task->timer.active = 0;
  return task;
}

static int cuv_timer_rearm (struct cuv_loop *loop);

static void
cuv_timer_fail_task (struct cuv_task *task, int error)
{
  enum cuv_timer_kind kind = task->timer.kind;

  task->timer.kind = CUV_TIMER_NONE;
  task->timer.error = 0;

  if (kind == CUV_TIMER_SLEEP)
    cuv_task_wake (task, error);
  else
    cuv_task_abort (task, error);
}

static void
cuv_timer_fail_all (struct cuv_loop *loop, int error)
{
  while (loop->timer_count)
    cuv_timer_fail_task (cuv_timer_pop (loop), error);
}

static int
cuv_timer_rearm (struct cuv_loop *loop)
{
  int error = uv_timer_stop (&loop->timer);
  if (error < 0)
    return error;

  if (!loop->timer_count)
    return 0;

  int injected = cuv_fault_check (CUV_FAULT_TIMER_REARM);
  if (injected < 0)
    return injected;

  uv_update_time (&loop->uv);
  u64 now = uv_now (&loop->uv);
  u64 deadline = loop->timer_heap[0]->timer.deadline;
  u64 delay = deadline > now ? deadline - now : 0;

  return uv_timer_start (&loop->timer, cuv_timer_cb, delay, 0);
}

static int
cuv_timer_find (struct cuv_loop *loop, struct cuv_task *task, usz *index)
{
  for (usz i = 0; i < loop->timer_count; ++i)
    if (loop->timer_heap[i] == task)
      {
        *index = i;
        return 0;
      }

  return UV_EBUSY;
}

static int
cuv_timer_remove (struct cuv_task *task)
{
  if (!task->timer.active)
    return UV_EBUSY;

  struct cuv_loop *loop = task->loop;
  usz index;
  int error = cuv_timer_find (loop, task, &index);
  if (error < 0)
    return error;

  --loop->timer_count;
  if (index != loop->timer_count)
    loop->timer_heap[index] = loop->timer_heap[loop->timer_count];

  if (loop->timer_count > 1)
    heap$ (heapify) (loop->timer_heap, loop->timer_count,
                     sizeof (*loop->timer_heap), cuv_timer_compare,
                     CUV_TIMER_HEAP_DEGREE);

  task->timer.active = 0;
  task->timer.kind = CUV_TIMER_NONE;
  task->timer.error = 0;

  error = cuv_timer_rearm (loop);
  if (error < 0)
    cuv_timer_fail_all (loop, error);

  return error;
}

static int
cuv_timer_insert (struct cuv_task *task, enum cuv_timer_kind kind,
                  u64 deadline, int timer_error)
{
  struct cuv_loop *loop = task->loop;
  int error = cuv_timer_reserve (loop, loop->timer_count + 1);
  if (error < 0)
    return error;

  task->timer.deadline = deadline;
  task->timer.error = timer_error;
  task->timer.kind = kind;
  task->timer.active = 1;

  loop->timer_heap[loop->timer_count] = task;
  ++loop->timer_count;

  heap$ (siftup) (loop->timer_heap, sizeof (*loop->timer_heap),
                  cuv_timer_compare, CUV_TIMER_HEAP_DEGREE,
                  loop->timer_count - 1);

  error = cuv_timer_rearm (loop);
  if (error < 0)
    {
      usz index;
      if (cuv_timer_find (loop, task, &index) == 0)
        {
          --loop->timer_count;
          if (index != loop->timer_count)
            loop->timer_heap[index] = loop->timer_heap[loop->timer_count];
          if (loop->timer_count > 1)
            heap$ (heapify) (loop->timer_heap, loop->timer_count,
                             sizeof (*loop->timer_heap), cuv_timer_compare,
                             CUV_TIMER_HEAP_DEGREE);
        }

      task->timer.active = 0;
      task->timer.kind = CUV_TIMER_NONE;
      task->timer.error = 0;
      cuv_timer_fail_all (loop, error);
    }

  return error;
}

static void
cuv_timer_cb (uv_timer_t *timer)
{
  auto loop = container_of (timer, struct cuv_loop, timer);

  uv_update_time (&loop->uv);
  u64 now = uv_now (&loop->uv);

  while (loop->timer_count && loop->timer_heap[0]->timer.deadline <= now)
    {
      struct cuv_task *task = cuv_timer_pop (loop);
      enum cuv_timer_kind kind = task->timer.kind;
      int error = task->timer.error;

      task->timer.kind = CUV_TIMER_NONE;
      task->timer.error = 0;

      if (kind == CUV_TIMER_SLEEP)
        {
          if (error == UV_ETIMEDOUT)
            task->aborting = 1;
          cuv_task_wake (task, error);
        }
      else
        cuv_task_abort (task, error);
    }

  int error = cuv_timer_rearm (loop);
  if (error < 0)
    cuv_timer_fail_all (loop, error);
}

static int
cuv_cancel_timer (struct cuv_task *task)
{
  int error = cuv_timer_remove (task);
  if (error < 0)
    return error;

  cuv_task_wake (task, 0);
  return 0;
}

static int
cuv_cancel_task_wait (struct cuv_task *task)
{
  struct cuv_task *target = task->wait.operation.task_wait.target;

  if (!target || target->joiner != task)
    return UV_EBUSY;

  target->joiner = null;
  cuv_task_wake (task, 0);
  return 0;
}

static void
cuv_ready_remove (struct cuv_task *task)
{
  assert (task);
  assert (task->state == CUV_TASK_READY);
  assert (task->loop->ready_count);

  list$ (rem) (&task->ready_node);
  --task->loop->ready_count;

  task->ready_node.prev = null;
  task->ready_node.next = null;
}

static struct cuv_task *
cuv_ready_pop (struct cuv_loop *loop)
{
  assert (loop->ready_count);
  assert (!cuv_list_empty (&loop->ready));

  auto task = container_of (loop->ready.next, struct cuv_task, ready_node);

  cuv_ready_remove (task);
  return task;
}

static void
cuv_task_complete (struct cuv_task *task)
{
  assert (task->state == CUV_TASK_CLOSING);
  assert (cuv_list_empty (&task->resources));
  assert (!task->cleanup);

  task->state = CUV_TASK_DONE;
  list$ (rem) (&task->loop_node);
  task->loop_node.prev = null;
  task->loop_node.next = null;
  assert (task->loop->task_count);
  --task->loop->task_count;

  if (task->group)
    cuv_task_group_child_complete (task);

  if (task->joiner)
    {
      struct cuv_task *joiner = task->joiner;
      task->joiner = null;
      cuv_task_wake (joiner, task->error);
    }
}

static void
cuv_task_cleanup_next (struct cuv_task *task)
{
  assert (task);
  assert (task->state == CUV_TASK_CLOSING);

  while (!cuv_list_empty (&task->resources))
    {
      auto resource
          = container_of (task->resources.next, struct cuv_resource, node);

      if (!resource->close)
        {
          cuv_resource_detach (resource);
          continue;
        }

      task->cleanup = resource;
      int status = resource->close (resource);
      if (status == CUV_PENDING)
        return;

      task->cleanup = null;

      if (status < 0 && task->error == 0)
        task->error = status;

      if (resource->owner == task)
        cuv_resource_detach (resource);
    }

  cuv_task_complete (task);
}

void
cuv_task_schedule (struct cuv_task *task)
{
  assert (task);
  assert (task->loop);
  assert (task->state != CUV_TASK_DONE);

  if (task->state == CUV_TASK_READY)
    return;

  struct cuv_loop *loop = task->loop;
  task->state = CUV_TASK_READY;

  list$ (ins) (&task->ready_node, loop->ready.prev, &loop->ready);
  ++loop->ready_count;
}

void
cuv_task_wake (struct cuv_task *task, int error)
{
  /*
   * Completion callbacks may race logically with cancellation/timeout.
   * libcuv is single-threaded, so the first completion that moves the task
   * out of WAITING wins; any late duplicate completion must not enqueue the
   * task a second time.
   */
  if (!task || task->state != CUV_TASK_WAITING)
    return;

  if (task->timer.active && task->timer.kind == CUV_TIMER_TIMEOUT)
    cuv_timer_remove (task);

  if (task->wait.abort_error < 0)
    error = task->wait.abort_error;

  task->wait.cancel = null;
  task->wait.abort_error = 0;
  task->wait.operation_name = null;
  task->error = error;
  cuv_task_schedule (task);
}

int
cuv_task_abort (struct cuv_task *task, int error)
{
  if (!task || error >= 0)
    return UV_EINVAL;

  if (task->aborting)
    return UV_EALREADY;

  if (task->state != CUV_TASK_WAITING)
    return UV_EBUSY;

  /* The first accepted timeout/cancellation reason is terminal. */
  task->aborting = 1;
  task->wait.abort_error = error;

  if (task->timer.active && task->timer.kind == CUV_TIMER_TIMEOUT)
    cuv_timer_remove (task);

  /*
   * Cancellation of the underlying libuv operation is best-effort.  Some
   * requests cannot be cancelled once running; in that case the logical
   * abort remains recorded and the eventual completion callback wakes the
   * task with the original abort reason.
   */
  if (task->wait.cancel)
    task->wait.cancel (task);

  return 0;
}

void
cuv_task_timeout_start (struct cuv_task *task, u64 milliseconds)
{
  assert (task);
  assert (task->state == CUV_TASK_WAITING);

  u64 deadline = cuv_timer_deadline (task->loop, milliseconds);

  if (task->timer.active)
    {
      assert (task->timer.kind == CUV_TIMER_SLEEP);

      if (deadline >= task->timer.deadline)
        return;

      task->timer.deadline = deadline;
      task->timer.error = UV_ETIMEDOUT;

      if (task->loop->timer_count > 1)
        heap$ (heapify) (task->loop->timer_heap, task->loop->timer_count,
                         sizeof (*task->loop->timer_heap), cuv_timer_compare,
                         CUV_TIMER_HEAP_DEGREE);

      int error = cuv_timer_rearm (task->loop);
      if (error < 0)
        cuv_task_abort (task, error);
      return;
    }

  int error
      = cuv_timer_insert (task, CUV_TIMER_TIMEOUT, deadline, UV_ETIMEDOUT);
  if (error < 0)
    cuv_task_abort (task, error);
}

void
cuv_resource_attach (struct cuv_resource *resource, struct cuv_task *owner)
{
  assert (resource);
  assert (owner);

  if (resource->owner == owner)
    return;

  if (resource->owner)
    cuv_resource_detach (resource);

  resource->owner = owner;
  list$ (ins) (&resource->node, owner->resources.prev, &owner->resources);
}

void
cuv_resource_detach (struct cuv_resource *resource)
{
  if (!resource || !resource->owner)
    return;

  list$ (rem) (&resource->node);
  resource->node.prev = null;
  resource->node.next = null;
  resource->owner = null;
}

void
cuv_resource_transfer (struct cuv_resource *resource, struct cuv_task *owner)
{
  assert (resource);

  if (!owner)
    {
      cuv_resource_detach (resource);
      return;
    }

  cuv_resource_attach (resource, owner);
}

void
cuv_task_cleanup_resume (struct cuv_task *task, int error)
{
  assert (task);
  assert (task->state == CUV_TASK_CLOSING);
  assert (task->cleanup);

  task->cleanup = null;

  if (error < 0 && task->error == 0)
    task->error = error;

  cuv_task_cleanup_next (task);
}

void
cuv_task_finish (struct cuv_task *task)
{
  assert (task);
  assert (task->state == CUV_TASK_RUNNING);
  assert (!task->timer.active);

  task->wait.operation_name = null;
  task->state = CUV_TASK_CLOSING;
  cuv_task_cleanup_next (task);
}

static void
cuv_task_dispatch (struct cuv_task *task)
{
  task->state = CUV_TASK_RUNNING;
  task->run (task);

  if (!task->mbr$ (savedpc))
    {
      cuv_task_finish (task);
      return;
    }

  if (task->state == CUV_TASK_RUNNING)
    {
      cuv_task_schedule (task);
      return;
    }

  assert (task->state == CUV_TASK_WAITING || task->state == CUV_TASK_READY);
}

static void
cuv_dispatch_ready (struct cuv_loop *loop)
{
  usz count = loop->ready_count;

  while (count-- && loop->ready_count && !loop->stop_requested)
    cuv_task_dispatch (cuv_ready_pop (loop));
}

static int
cuv_loop_take_stop (struct cuv_loop *loop)
{
  if (!loop->stop_requested)
    return 0;

  loop->stop_requested = 0;

  /*
   * uv_stop() may have been called while libcuv was dispatching a ready task
   * rather than from inside uv_run().  In that case libuv has not yet had a
   * chance to clear its stop flag.  A stopped UV_RUN_NOWAIT consumes that
   * flag without running callbacks, so a later cuv_loop_run() starts cleanly.
   */
  uv_stop (&loop->uv);
  uv_run (&loop->uv, UV_RUN_NOWAIT);
  return 1;
}

int
cuv_loop_init (struct cuv_loop *loop)
{
  if (!loop)
    return UV_EINVAL;

  memset (loop, 0, sizeof (*loop));
  cuv_list_init (&loop->ready);
  cuv_list_init (&loop->tasks);

  int error = uv_loop_init (&loop->uv);
  if (error < 0)
    return error;

  error = uv_timer_init (&loop->uv, &loop->timer);
  if (error < 0)
    {
      uv_loop_close (&loop->uv);
      return error;
    }

  loop->initialized = 1;
  return 0;
}

int
cuv_loop_run (struct cuv_loop *loop)
{
  if (!loop || !loop->initialized)
    return UV_EINVAL;

  for (;;)
    {
      if (cuv_loop_take_stop (loop))
        return 0;

      if (loop->ready_count)
        cuv_dispatch_ready (loop);

      if (cuv_loop_take_stop (loop))
        return 0;

      if (loop->ready_count)
        {
          uv_run (&loop->uv, UV_RUN_NOWAIT);
          if (cuv_loop_take_stop (loop))
            return 0;
          continue;
        }

      if (uv_loop_alive (&loop->uv))
        {
          uv_run (&loop->uv, UV_RUN_ONCE);
          if (cuv_loop_take_stop (loop))
            return 0;
          continue;
        }

      if (loop->task_count)
        return UV_EBUSY;

      return 0;
    }
}

int
cuv_loop_stop (struct cuv_loop *loop)
{
  if (!loop || !loop->initialized)
    return UV_EINVAL;

  loop->stop_requested = 1;
  uv_stop (&loop->uv);
  return 0;
}

int
cuv_loop_alive (struct cuv_loop const *loop)
{
  if (!loop || !loop->initialized)
    return UV_EINVAL;

  return !!(loop->task_count || uv_loop_alive (&loop->uv));
}

int
cuv_loop_now (struct cuv_loop const *loop, u64 *milliseconds)
{
  if (!loop || !loop->initialized || !milliseconds)
    return UV_EINVAL;

  *milliseconds = uv_now (&loop->uv);
  return 0;
}

int
cuv_loop_update_time (struct cuv_loop *loop)
{
  if (!loop || !loop->initialized)
    return UV_EINVAL;

  uv_update_time (&loop->uv);
  return 0;
}

int
cuv_loop_backend_fd (struct cuv_loop const *loop, int *fd)
{
  if (!loop || !loop->initialized || !fd)
    return UV_EINVAL;

  *fd = uv_backend_fd (&loop->uv);
  return 0;
}

int
cuv_loop_backend_timeout (struct cuv_loop const *loop, int *milliseconds)
{
  if (!loop || !loop->initialized || !milliseconds)
    return UV_EINVAL;

  *milliseconds = uv_backend_timeout (&loop->uv);
  return 0;
}

int
cuv_loop_fork (struct cuv_loop *loop)
{
  if (!loop || !loop->initialized)
    return UV_EINVAL;

  int error = uv_loop_fork (&loop->uv);
  if (error < 0)
    return error;

  uv_update_time (&loop->uv);
  error = cuv_timer_rearm (loop);
  if (error < 0)
    cuv_timer_fail_all (loop, error);

  return error;
}

int
cuv_loop_enable_idle_metrics (struct cuv_loop *loop)
{
  if (!loop || !loop->initialized)
    return UV_EINVAL;

  return uv_loop_configure (&loop->uv, UV_METRICS_IDLE_TIME);
}

int
cuv_loop_metrics (struct cuv_loop *loop, uv_metrics_t *metrics)
{
  if (!loop || !loop->initialized || !metrics)
    return UV_EINVAL;

  return uv_metrics_info (&loop->uv, metrics);
}

int
cuv_loop_idle_time (struct cuv_loop *loop, u64 *nanoseconds)
{
  if (!loop || !loop->initialized || !nanoseconds)
    return UV_EINVAL;

  *nanoseconds = uv_metrics_idle_time (&loop->uv);
  return 0;
}

int
cuv_loop_close (struct cuv_loop *loop)
{
  if (!loop || !loop->initialized)
    return UV_EINVAL;

  if (loop->task_count || loop->ready_count || loop->timer_count)
    return UV_EBUSY;

  assert (cuv_list_empty (&loop->tasks));

  cuv_loop_take_stop (loop);

  if (uv_loop_alive (&loop->uv))
    return UV_EBUSY;

  if (!uv_is_closing ((uv_handle_t *)&loop->timer))
    uv_close ((uv_handle_t *)&loop->timer, null);

  uv_run (&loop->uv, UV_RUN_DEFAULT);

  int error = uv_loop_close (&loop->uv);
  if (error < 0)
    return error;

  free (loop->timer_heap);
  loop->timer_heap = null;
  loop->timer_count = 0;
  loop->timer_capacity = 0;
  loop->stop_requested = 0;
  loop->initialized = 0;

  return 0;
}

void
cuv_task_init (struct cuv_task *task, struct cuv_loop *loop,
               void (*run) (struct cuv_task *))
{
  assert (task);
  assert (loop);
  assert (run);

  memset (task, 0, sizeof (*task));
  task->loop = loop;
  task->run = run;
  task->state = CUV_TASK_IDLE;
  cuv_list_init (&task->resources);
}

void
cuv_task_start (struct cuv_task *task)
{
  assert (task);
  assert (task->state == CUV_TASK_IDLE);

  list$ (ins) (&task->loop_node, task->loop->tasks.prev, &task->loop->tasks);
  ++task->loop->task_count;
  cuv_task_schedule (task);
}

int
cuv_task_wait (struct cuv_task *task, struct cuv_task *target)
{
  if (!task || !target || task == target)
    return UV_EINVAL;

  if (target->state == CUV_TASK_DONE)
    return target->error < 0 ? target->error : CUV_READY;

  if (target->state == CUV_TASK_IDLE)
    return UV_EINVAL;

  if (target->group || target->joiner)
    return UV_EBUSY;

  target->joiner = task;
  task->wait.operation.task_wait.target = target;
  task->wait.cancel = cuv_cancel_task_wait;

  return CUV_PENDING;
}

int
cuv_task_cancel (struct cuv_task *task)
{
  if (!task)
    return UV_EINVAL;

  switch (task->state)
    {
    case CUV_TASK_IDLE:
      return UV_EINVAL;

    case CUV_TASK_READY:
      if (task->aborting)
        return UV_EALREADY;

      assert (!task->timer.active);

      task->aborting = 1;
      cuv_ready_remove (task);
      task->error = UV_ECANCELED;
      task->mbr$ (savedpc) = null;
      task->state = CUV_TASK_RUNNING;
      cuv_task_finish (task);
      return 0;

    case CUV_TASK_RUNNING:
      return UV_EBUSY;

    case CUV_TASK_WAITING:
      return cuv_task_abort (task, UV_ECANCELED);

    case CUV_TASK_CLOSING:
    case CUV_TASK_DONE:
      return UV_EALREADY;
    }

  return UV_EINVAL;
}

int
cuv_task_error (struct cuv_task const *task)
{
  return task ? task->error : UV_EINVAL;
}

int
cuv_cancel_req (struct cuv_task *task)
{
  return uv_cancel ((uv_req_t *)&task->wait.req);
}

int
cuv_sleep (struct cuv_task *task, u64 milliseconds)
{
  if (!task || !task->loop)
    return UV_EINVAL;

  if (task->timer.active)
    return UV_EBUSY;

  int error = cuv_timer_insert (
      task, CUV_TIMER_SLEEP, cuv_timer_deadline (task->loop, milliseconds), 0);
  if (error < 0)
    return error;

  task->wait.cancel = cuv_cancel_timer;
  return CUV_PENDING;
}
