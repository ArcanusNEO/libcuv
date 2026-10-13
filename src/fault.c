#include "intern.h"

#ifdef CUV_ENABLE_FAULT_INJECTION

struct cuv_fault_state
{
  usz remaining;
  int error;
  unsigned armed;
};

static struct cuv_fault_state cuv_faults[CUV_FAULT_COUNT];

void
cuv_fault_reset ()
{
  memset (cuv_faults, 0, sizeof (cuv_faults));
}

void
cuv_fault_fail_after (enum cuv_fault_point point, usz successful_hits,
                      int error)
{
  assert ((unsigned)point < CUV_FAULT_COUNT);
  assert (error < 0);

  cuv_faults[point].remaining = successful_hits;
  cuv_faults[point].error = error;
  cuv_faults[point].armed = 1;
}

int
cuv_fault_check (enum cuv_fault_point point)
{
  assert ((unsigned)point < CUV_FAULT_COUNT);

  struct cuv_fault_state *fault = &cuv_faults[point];
  if (!fault->armed)
    return 0;

  if (fault->remaining)
    {
      --fault->remaining;
      return 0;
    }

  fault->armed = 0;
  return fault->error;
}

void *
cuv_malloc (usz size)
{
  if (cuv_fault_check (CUV_FAULT_ALLOC) < 0)
    return null;

  return malloc (size);
}

void *
cuv_calloc (usz count, usz size)
{
  if (cuv_fault_check (CUV_FAULT_ALLOC) < 0)
    return null;

  return calloc (count, size);
}

void *
cuv_realloc (void *memory, usz size)
{
  if (cuv_fault_check (CUV_FAULT_ALLOC) < 0)
    return null;

  return realloc (memory, size);
}

#endif /* CUV_ENABLE_FAULT_INJECTION */
