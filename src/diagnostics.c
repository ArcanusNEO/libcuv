#include "intern.h"

#define CUV_DEBUG_MAX_DEPTH 64

char const *
cuv_task_state_name (enum cuv_task_state state)
{
  switch (state)
    {
    case CUV_TASK_IDLE:
      return "idle";
    case CUV_TASK_READY:
      return "ready";
    case CUV_TASK_RUNNING:
      return "running";
    case CUV_TASK_WAITING:
      return "waiting";
    case CUV_TASK_CLOSING:
      return "closing";
    case CUV_TASK_DONE:
      return "done";
    }
  return "unknown";
}

char const *
cuv_timer_kind_name (enum cuv_timer_kind kind)
{
  switch (kind)
    {
    case CUV_TIMER_NONE:
      return "none";
    case CUV_TIMER_SLEEP:
      return "sleep";
    case CUV_TIMER_TIMEOUT:
      return "timeout";
    }
  return "unknown";
}

char const *
cuv_resource_kind_name (enum cuv_resource_kind kind)
{
  switch (kind)
    {
    case CUV_RESOURCE_CUSTOM:
      return "custom";
    case CUV_RESOURCE_FILE:
      return "file";
    case CUV_RESOURCE_DIRLIST:
      return "dirlist";
    case CUV_RESOURCE_DIR:
      return "dir";
    case CUV_RESOURCE_TCP:
      return "tcp";
    case CUV_RESOURCE_UDP:
      return "udp";
    case CUV_RESOURCE_PIPE:
      return "pipe";
    case CUV_RESOURCE_SIGNAL:
      return "signal";
    case CUV_RESOURCE_FS_EVENT:
      return "fs_event";
    case CUV_RESOURCE_FS_POLL:
      return "fs_poll";
    case CUV_RESOURCE_POLL:
      return "poll";
    case CUV_RESOURCE_TTY:
      return "tty";
    case CUV_RESOURCE_PROCESS:
      return "process";
    case CUV_RESOURCE_TASK_GROUP:
      return "task_group";
    case CUV_RESOURCE_ADDRLIST:
      return "addrlist";
    case CUV_RESOURCE_IDLE:
      return "idle";
    case CUV_RESOURCE_PREPARE:
      return "prepare";
    case CUV_RESOURCE_CHECK:
      return "check";
    case CUV_RESOURCE_TIMER:
      return "timer";
    }
  return "unknown";
}

static usz
cuv_task_resource_count (struct cuv_task const *task)
{
  usz count = 0;
  struct lsnod const *node = task->resources.next;
  while (node != &task->resources)
    {
      ++count;
      node = node->next;
    }
  return count;
}

int
cuv_task_inspect (struct cuv_task const *task,
                  struct cuv_task_diagnostics *diagnostics)
{
  if (!task || !diagnostics)
    return UV_EINVAL;

  diagnostics->state = task->state;
  diagnostics->timer_kind = task->timer.kind;
  diagnostics->operation_name = task->wait.operation_name;
  diagnostics->error = task->error;
  diagnostics->abort_error = task->wait.abort_error;
  diagnostics->timer_deadline = task->timer.deadline;
  diagnostics->resource_count = cuv_task_resource_count (task);
  diagnostics->joiner = task->joiner;
  diagnostics->group = task->group;
  diagnostics->cleanup = task->cleanup;
  diagnostics->aborting = task->aborting;
  diagnostics->timer_active = task->timer.active;
  diagnostics->suspended = task->mbr$ (savedpc) != null;
  return 0;
}

int
cuv_loop_inspect (struct cuv_loop const *loop,
                  struct cuv_loop_diagnostics *diagnostics)
{
  if (!loop || !diagnostics)
    return UV_EINVAL;

  diagnostics->ready_count = loop->ready_count;
  diagnostics->task_count = loop->task_count;
  diagnostics->timer_count = loop->timer_count;
  diagnostics->initialized = loop->initialized;
  diagnostics->uv_alive = loop->initialized ? !!uv_loop_alive (&loop->uv) : 0;
  return 0;
}

int
cuv_task_group_inspect (struct cuv_task_group const *group,
                        struct cuv_task_group_diagnostics *diagnostics)
{
  if (!group || !diagnostics)
    return UV_EINVAL;

  diagnostics->owner = group->resource.owner;
  diagnostics->waiter = group->waiter;
  diagnostics->winner = group->winner;
  diagnostics->count = group->count;
  diagnostics->first_error = group->first_error;
  diagnostics->winner_error = group->winner_error;
  diagnostics->close_error = group->close_error;
  diagnostics->wait_mode = group->wait_mode;
  diagnostics->initialized = group->initialized;
  diagnostics->closing = group->closing;
  return 0;
}

static int
cuv_debug_indent (FILE *stream, unsigned depth)
{
  while (depth--)
    if (fputs ("  ", stream) == EOF)
      return UV_EIO;

  return 0;
}

static int cuv_debug_dump_task_impl (FILE *stream, struct cuv_task const *task,
                                     unsigned depth);

static int
cuv_debug_dump_group_impl (FILE *stream, struct cuv_task_group const *group,
                           unsigned depth)
{
  if (depth > CUV_DEBUG_MAX_DEPTH)
    return UV_EOVERFLOW;

  struct cuv_task_group_diagnostics diagnostics;
  int error = cuv_task_group_inspect (group, &diagnostics);
  if (error < 0)
    return error;

  error = cuv_debug_indent (stream, depth);
  if (error < 0)
    return error;

  if (fprintf (stream,
               "group %p owner=%p count=%zu wait=%u waiter=%p winner=%p "
               "first_error=%d winner_error=%d closing=%u\n",
               (void const *)group, (void *)diagnostics.owner,
               diagnostics.count, (unsigned)diagnostics.wait_mode,
               (void *)diagnostics.waiter, (void *)diagnostics.winner,
               diagnostics.first_error, diagnostics.winner_error,
               diagnostics.closing)
      < 0)
    return UV_EIO;

  struct lsnod const *node = group->children.next;
  while (node != &group->children)
    {
      auto child = container_of (node, struct cuv_task, group_node);
      error = cuv_debug_dump_task_impl (stream, child, depth + 1);
      if (error < 0)
        return error;
      node = node->next;
    }

  return 0;
}

static int
cuv_debug_dump_task_impl (FILE *stream, struct cuv_task const *task,
                          unsigned depth)
{
  if (depth > CUV_DEBUG_MAX_DEPTH)
    return UV_EOVERFLOW;

  struct cuv_task_diagnostics diagnostics;
  int error = cuv_task_inspect (task, &diagnostics);
  if (error < 0)
    return error;

  error = cuv_debug_indent (stream, depth);
  if (error < 0)
    return error;

  char const *operation
      = diagnostics.operation_name ? diagnostics.operation_name : "none";

  if (fprintf (stream,
               "task %p state=%s error=%d aborting=%u wait=%s "
               "abort_error=%d timer=%s timer_active=%u resources=%zu "
               "joiner=%p group=%p cleanup=%p\n",
               (void const *)task, cuv_task_state_name (diagnostics.state),
               diagnostics.error, diagnostics.aborting, operation,
               diagnostics.abort_error,
               cuv_timer_kind_name (diagnostics.timer_kind),
               diagnostics.timer_active, diagnostics.resource_count,
               (void *)diagnostics.joiner, (void *)diagnostics.group,
               (void *)diagnostics.cleanup)
      < 0)
    return UV_EIO;

  struct lsnod const *node = task->resources.next;
  while (node != &task->resources)
    {
      auto resource = container_of (node, struct cuv_resource, node);

      error = cuv_debug_indent (stream, depth + 1);
      if (error < 0)
        return error;

      if (fprintf (stream, "resource %p kind=%s owner=%p cleanup=%u\n",
                   (void const *)resource,
                   cuv_resource_kind_name (resource->kind),
                   (void *)resource->owner, resource == diagnostics.cleanup)
          < 0)
        return UV_EIO;

      if (resource->kind == CUV_RESOURCE_TASK_GROUP)
        {
          auto group
              = container_of (resource, struct cuv_task_group, resource);
          error = cuv_debug_dump_group_impl (stream, group, depth + 2);
          if (error < 0)
            return error;
        }

      node = node->next;
    }

  return 0;
}

int
cuv_debug_dump_task (FILE *stream, struct cuv_task const *task)
{
  if (!stream || !task)
    return UV_EINVAL;

  return cuv_debug_dump_task_impl (stream, task, 0);
}

int
cuv_debug_dump_group (FILE *stream, struct cuv_task_group const *group)
{
  if (!stream || !group)
    return UV_EINVAL;

  return cuv_debug_dump_group_impl (stream, group, 0);
}

int
cuv_debug_dump_loop (FILE *stream, struct cuv_loop const *loop)
{
  if (!stream || !loop)
    return UV_EINVAL;

  struct cuv_loop_diagnostics diagnostics;
  int error = cuv_loop_inspect (loop, &diagnostics);
  if (error < 0)
    return error;

  if (fprintf (stream,
               "loop %p initialized=%u stop=%u tasks=%zu ready=%zu timers=%zu "
               "uv_alive=%u\n",
               (void const *)loop, diagnostics.initialized,
               loop->stop_requested, diagnostics.task_count,
               diagnostics.ready_count, diagnostics.timer_count,
               diagnostics.uv_alive)
      < 0)
    return UV_EIO;

  if (!loop->initialized)
    return 0;

  struct lsnod const *node = loop->tasks.next;
  while (node != &loop->tasks)
    {
      auto task = container_of (node, struct cuv_task, loop_node);
      error = cuv_debug_dump_task_impl (stream, task, 1);
      if (error < 0)
        return error;
      node = node->next;
    }

  return 0;
}
