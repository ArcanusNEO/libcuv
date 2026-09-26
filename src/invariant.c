#include "intern.h"

#define CUV_DEBUG_MAX_NODES ((usz)1048576)
#define CUV_TIMER_HEAP_DEGREE 4

static int
cuv_debug_node_linked (struct lsnod const *node)
{
  return node && node->next && node->prev && node->next->prev == node
         && node->prev->next == node;
}

static int
cuv_debug_resource_list (struct cuv_task const *task, usz *count)
{
  if (!cuv_debug_node_linked (&task->resources))
    return UV_EINVAL;

  usz seen = 0;
  struct lsnod const *node = task->resources.next;
  while (node != &task->resources)
    {
      if (++seen > CUV_DEBUG_MAX_NODES || !cuv_debug_node_linked (node))
        return UV_EINVAL;

      auto resource = container_of (node, struct cuv_resource, node);
      if (resource->owner != task)
        return UV_EINVAL;

      node = node->next;
    }

  if (count)
    *count = seen;
  return 0;
}

int
cuv_debug_check_task (struct cuv_task const *task)
{
  if (!task || !task->loop || !task->run)
    return UV_EINVAL;

  if (cuv_debug_resource_list (task, null) < 0)
    return UV_EINVAL;

  if (task->timer.active)
    {
      if (task->state != CUV_TASK_WAITING
          || task->timer.kind == CUV_TIMER_NONE)
        return UV_EINVAL;
    }
  else if (task->timer.kind != CUV_TIMER_NONE)
    return UV_EINVAL;

  if (task->aborting && task->state == CUV_TASK_WAITING
      && task->wait.abort_error >= 0)
    return UV_EINVAL;

  if (!task->aborting && task->wait.abort_error < 0)
    return UV_EINVAL;

  if (task->cleanup
      && (task->state != CUV_TASK_CLOSING || task->cleanup->owner != task))
    return UV_EINVAL;

  if (task->group)
    {
      if (!task->group->initialized || task->group->loop != task->loop
          || !cuv_debug_node_linked (&task->group_node))
        return UV_EINVAL;
    }
  else if (task->group_node.next || task->group_node.prev)
    return UV_EINVAL;

  switch (task->state)
    {
    case CUV_TASK_IDLE:
      if (task->loop_node.next || task->loop_node.prev || task->ready_node.next
          || task->ready_node.prev || task->timer.active || task->cleanup
          || task->mbr$ (savedpc))
        return UV_EINVAL;
      break;

    case CUV_TASK_READY:
      if (!cuv_debug_node_linked (&task->loop_node)
          || !cuv_debug_node_linked (&task->ready_node) || task->timer.active)
        return UV_EINVAL;
      break;

    case CUV_TASK_RUNNING:
      if (!cuv_debug_node_linked (&task->loop_node) || task->ready_node.next
          || task->ready_node.prev || task->timer.active)
        return UV_EINVAL;
      break;

    case CUV_TASK_WAITING:
      if (!cuv_debug_node_linked (&task->loop_node) || task->ready_node.next
          || task->ready_node.prev || !task->mbr$ (savedpc)
          || !task->wait.operation_name)
        return UV_EINVAL;
      break;

    case CUV_TASK_CLOSING:
      if (!cuv_debug_node_linked (&task->loop_node) || task->ready_node.next
          || task->ready_node.prev || task->timer.active
          || task->mbr$ (savedpc) || task->wait.operation_name)
        return UV_EINVAL;
      break;

    case CUV_TASK_DONE:
      if (task->loop_node.next || task->loop_node.prev || task->ready_node.next
          || task->ready_node.prev || task->timer.active || task->cleanup
          || task->mbr$ (savedpc) || task->wait.operation_name
          || !cuv_list_empty (&task->resources))
        return UV_EINVAL;
      break;

    default:
      return UV_EINVAL;
    }

  return 0;
}

int
cuv_debug_check_group (struct cuv_task_group const *group)
{
  if (!group)
    return UV_EINVAL;

  if (!group->initialized)
    return 0;

  if (!group->loop || group->resource.kind != CUV_RESOURCE_TASK_GROUP
      || !cuv_debug_node_linked (&group->children))
    return UV_EINVAL;

  if (group->resource.owner && group->resource.owner->loop != group->loop)
    return UV_EINVAL;

  if (group->waiter)
    {
      if (group->waiter->state != CUV_TASK_WAITING
          || group->waiter->wait.operation.task_group.group != group)
        return UV_EINVAL;
    }

  usz count = 0;
  struct lsnod const *node = group->children.next;
  while (node != &group->children)
    {
      if (++count > CUV_DEBUG_MAX_NODES || !cuv_debug_node_linked (node))
        return UV_EINVAL;

      auto child = container_of (node, struct cuv_task, group_node);
      if (child->group != group || child->loop != group->loop
          || child->state == CUV_TASK_IDLE || child->state == CUV_TASK_DONE)
        return UV_EINVAL;

      node = node->next;
    }

  if (count != group->count)
    return UV_EINVAL;

  if (group->closing && !group->resource.owner && group->count)
    return UV_EINVAL;

  return 0;
}

static int
cuv_debug_task_in_ready (struct cuv_loop const *loop,
                         struct cuv_task const *task)
{
  struct lsnod const *node = loop->ready.next;
  usz seen = 0;

  while (node != &loop->ready)
    {
      if (++seen > CUV_DEBUG_MAX_NODES)
        return 0;
      if (node == &task->ready_node)
        return 1;
      node = node->next;
    }

  return 0;
}

int
cuv_debug_check_loop (struct cuv_loop const *loop)
{
  if (!loop)
    return UV_EINVAL;

  if (!cuv_debug_node_linked (&loop->ready)
      || !cuv_debug_node_linked (&loop->tasks) || loop->stop_requested > 1)
    return UV_EINVAL;

  if (!loop->initialized)
    return loop->ready_count || loop->task_count || loop->timer_count
                   || loop->stop_requested
               ? UV_EINVAL
               : 0;

  usz task_count = 0;
  usz ready_states = 0;
  usz timer_states = 0;
  struct lsnod const *node = loop->tasks.next;

  while (node != &loop->tasks)
    {
      if (++task_count > CUV_DEBUG_MAX_NODES || !cuv_debug_node_linked (node))
        return UV_EINVAL;

      auto task = container_of (node, struct cuv_task, loop_node);
      if (task->loop != loop || task->state == CUV_TASK_IDLE
          || task->state == CUV_TASK_DONE || cuv_debug_check_task (task) < 0)
        return UV_EINVAL;

      if (task->state == CUV_TASK_READY)
        {
          ++ready_states;
          if (!cuv_debug_task_in_ready (loop, task))
            return UV_EINVAL;
        }

      if (task->timer.active)
        ++timer_states;

      struct lsnod const *resource_node = task->resources.next;
      while (resource_node != &task->resources)
        {
          auto resource
              = container_of (resource_node, struct cuv_resource, node);
          if (resource->kind == CUV_RESOURCE_TASK_GROUP)
            {
              auto group
                  = container_of (resource, struct cuv_task_group, resource);
              if (cuv_debug_check_group (group) < 0)
                return UV_EINVAL;
            }
          resource_node = resource_node->next;
        }

      node = node->next;
    }

  if (task_count != loop->task_count || ready_states != loop->ready_count
      || timer_states != loop->timer_count)
    return UV_EINVAL;

  usz ready_count = 0;
  node = loop->ready.next;
  while (node != &loop->ready)
    {
      if (++ready_count > CUV_DEBUG_MAX_NODES || !cuv_debug_node_linked (node))
        return UV_EINVAL;

      auto task = container_of (node, struct cuv_task, ready_node);
      if (task->loop != loop || task->state != CUV_TASK_READY)
        return UV_EINVAL;
      node = node->next;
    }

  if (ready_count != loop->ready_count
      || loop->timer_count > loop->timer_capacity
      || (loop->timer_count && !loop->timer_heap))
    return UV_EINVAL;

  for (usz index = 0; index < loop->timer_count; ++index)
    {
      struct cuv_task const *task = loop->timer_heap[index];
      if (!task || task->loop != loop || !task->timer.active
          || task->state != CUV_TASK_WAITING)
        return UV_EINVAL;

      if (index)
        {
          usz parent = (index - 1) / CUV_TIMER_HEAP_DEGREE;
          if (loop->timer_heap[parent]->timer.deadline > task->timer.deadline)
            return UV_EINVAL;
        }
    }

  return 0;
}
