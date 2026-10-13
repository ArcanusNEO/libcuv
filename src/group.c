#include "intern.h"

static int cuv_task_group_resource_close (struct cuv_resource *resource);

static void
cuv_task_group_reset (struct cuv_task_group *group)
{
  group->winner = null;
  group->winner_out = null;
  group->first_error = 0;
  group->winner_error = 0;
  group->wait_mode = CUV_TASK_GROUP_WAIT_NONE;
}

static int
cuv_task_group_cancel_children (struct cuv_task_group *group)
{
  int first_error = 0;

  for (struct lsnod *node = group->children.next; node != &group->children;)
    {
      struct lsnod *next = node->next;
      auto child = container_of (node, struct cuv_task, group_node);

      int error = 0;

      switch (child->state)
        {
        case CUV_TASK_READY:
        case CUV_TASK_WAITING:
          error = cuv_task_cancel (child);
          break;

        case CUV_TASK_RUNNING:
        case CUV_TASK_CLOSING:
          break;

        case CUV_TASK_IDLE:
        case CUV_TASK_DONE:
          error = UV_EINVAL;
          break;
        }

      if (error < 0 && error != UV_EALREADY && error != UV_EBUSY
          && first_error == 0)
        first_error = error;

      node = next;
    }

  return first_error;
}

static int
cuv_cancel_task_group_wait (struct cuv_task *task)
{
  struct cuv_task_group *group = task->wait.operation.task_group.group;

  if (!group || group->waiter != task)
    return UV_EBUSY;

  group->waiter = null;
  group->winner_out = null;
  group->wait_mode = CUV_TASK_GROUP_WAIT_NONE;

  cuv_task_wake (task, UV_ECANCELED);
  return 0;
}

static int
cuv_task_group_result (struct cuv_task_group *group,
                       enum cuv_task_group_wait mode, struct cuv_task **winner)
{
  int error;

  if (mode == CUV_TASK_GROUP_WAIT_RACE)
    {
      if (!group->winner)
        return UV_EINVAL;

      if (winner)
        *winner = group->winner;

      error = group->winner_error;
    }
  else
    error = group->first_error;

  cuv_task_group_reset (group);
  return error < 0 ? error : CUV_READY;
}

static void
cuv_task_group_wake_waiter (struct cuv_task_group *group)
{
  struct cuv_task *waiter = group->waiter;
  enum cuv_task_group_wait mode = group->wait_mode;
  struct cuv_task **winner = group->winner_out;
  int error;

  assert (waiter);
  assert (group->count == 0);

  if (mode == CUV_TASK_GROUP_WAIT_RACE)
    {
      assert (group->winner);
      if (winner)
        *winner = group->winner;
      error = group->winner_error;
    }
  else
    error = group->first_error;

  group->waiter = null;
  cuv_task_group_reset (group);

  cuv_task_wake (waiter, error);
}

void
cuv_task_group_child_complete (struct cuv_task *child)
{
  struct cuv_task_group *group = child->group;

  if (!group)
    return;

  assert (group->count);

  child->group = null;
  list$ (rem) (&child->group_node);
  child->group_node.prev = null;
  child->group_node.next = null;
  --group->count;

  if (!group->winner)
    {
      group->winner = child;
      group->winner_error = child->error;

      if (group->wait_mode == CUV_TASK_GROUP_WAIT_RACE)
        cuv_task_group_cancel_children (group);
    }

  if (group->first_error == 0 && child->error < 0)
    group->first_error = child->error;

  if (group->count)
    return;

  if (group->closing)
    {
      if (group->close_active)
        return;

      struct cuv_task *owner = group->resource.owner;
      cuv_resource_detach (&group->resource);
      group->closing = 0;

      if (owner && owner->state == CUV_TASK_CLOSING
          && owner->cleanup == &group->resource)
        {
          int error = group->close_error;
          group->close_error = 0;
          cuv_task_cleanup_resume (owner, error);
        }

      return;
    }

  if (group->waiter)
    cuv_task_group_wake_waiter (group);
}

static int
cuv_task_group_resource_close (struct cuv_resource *resource)
{
  auto group = container_of (resource, struct cuv_task_group, resource);

  if (!resource->owner)
    return CUV_READY;

  if (!group->count)
    {
      cuv_resource_detach (resource);
      group->closing = 0;
      return CUV_READY;
    }

  group->closing = 1;
  group->waiter = null;
  group->winner_out = null;
  group->wait_mode = CUV_TASK_GROUP_WAIT_NONE;

  group->close_active = 1;
  int error = cuv_task_group_cancel_children (group);
  group->close_active = 0;

  if (error < 0 && group->close_error == 0)
    group->close_error = error;

  if (!group->count)
    {
      cuv_resource_detach (resource);
      group->closing = 0;
      error = group->close_error;
      group->close_error = 0;
      return error < 0 ? error : CUV_READY;
    }

  return CUV_PENDING;
}

int
cuv_task_group_init (struct cuv_task_group *group, struct cuv_task *owner)
{
  if (!group || !owner || !owner->loop)
    return UV_EINVAL;

  if (group->initialized)
    return UV_EALREADY;

  memset (group, 0, sizeof (*group));
  group->loop = owner->loop;
  group->resource.close = cuv_task_group_resource_close;
  group->resource.kind = CUV_RESOURCE_TASK_GROUP;
  cuv_list_init (&group->children);
  group->initialized = 1;

  cuv_resource_attach (&group->resource, owner);
  return 0;
}

int
cuv_task_group_start (struct cuv_task_group *group, struct cuv_task *child)
{
  if (!group || !child || !group->initialized || !group->resource.owner)
    return UV_EINVAL;

  if (group->closing || group->waiter)
    return UV_EBUSY;

  if (child == group->resource.owner || child->loop != group->loop
      || child->state != CUV_TASK_IDLE || child->group || child->joiner)
    return UV_EINVAL;

  if (!group->count && group->winner)
    return UV_EBUSY;

  if (!group->count)
    cuv_task_group_reset (group);

  child->group = group;
  list$ (ins) (&child->group_node, group->children.prev, &group->children);
  ++group->count;

  cuv_task_start (child);
  return 0;
}

int
cuv_task_group_wait (struct cuv_task *task, struct cuv_task_group *group)
{
  if (!task || !group || !group->initialized)
    return UV_EINVAL;

  if (group->resource.owner != task || group->closing)
    return UV_EPERM;

  if (group->waiter)
    return UV_EBUSY;

  if (!group->count)
    return cuv_task_group_result (group, CUV_TASK_GROUP_WAIT_ALL, null);

  group->waiter = task;
  group->winner_out = null;
  group->wait_mode = CUV_TASK_GROUP_WAIT_ALL;

  task->wait.operation.task_group.group = group;
  task->wait.operation.task_group.winner = null;
  task->wait.cancel = cuv_cancel_task_group_wait;

  return CUV_PENDING;
}

int
cuv_task_group_race (struct cuv_task *task, struct cuv_task_group *group,
                     struct cuv_task **winner)
{
  if (!task || !group || !winner || !group->initialized)
    return UV_EINVAL;

  if (group->resource.owner != task || group->closing)
    return UV_EPERM;

  if (group->waiter)
    return UV_EBUSY;

  if (!group->count)
    return cuv_task_group_result (group, CUV_TASK_GROUP_WAIT_RACE, winner);

  group->wait_mode = CUV_TASK_GROUP_WAIT_RACE;
  group->winner_out = winner;

  if (group->winner)
    {
      int error = cuv_task_group_cancel_children (group);
      if (error < 0)
        return error;

      if (!group->count)
        return cuv_task_group_result (group, CUV_TASK_GROUP_WAIT_RACE, winner);
    }

  group->waiter = task;

  task->wait.operation.task_group.group = group;
  task->wait.operation.task_group.winner = winner;
  task->wait.cancel = cuv_cancel_task_group_wait;

  return CUV_PENDING;
}

int
cuv_task_group_cancel (struct cuv_task_group *group)
{
  if (!group || !group->initialized)
    return UV_EINVAL;

  if (group->closing)
    return UV_EBUSY;

  return cuv_task_group_cancel_children (group);
}
