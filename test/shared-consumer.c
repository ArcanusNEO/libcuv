#include <cuv.h>
#include <stdio.h>

int
main (void)
{
  struct cuv_loop loop = { 0 };

  int error = cuv_loop_init (&loop);
  if (error < 0)
    return 1;

  error = cuv_loop_close (&loop);
  if (error < 0)
    return 2;

  fputs ("libcuv shared consumer passed\n", stdout);
  return 0;
}
