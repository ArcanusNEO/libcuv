<h1 align="center"><strong>libcuv</strong></h1>

`libcuv` adapts libuv operations to caller-owned `cmacs.h` coroutines. Its control-flow surface is intentionally small: awaitable operations are ordinary `cuv_*` C functions, and `await$` is the single coroutine control macro added by the library.

```c
struct app
{
  struct cuv_task task;
  struct cuv_file file;
  char buffer[4096];
  isz size;
};

static void
app_run (struct cuv_task *task) async$ ((struct app *)task)
{
  await$ (cuv_fs_open, &$.file, "input.txt", O_RDONLY, 0);
  await$ (cuv_fs_read, &$.size, &$.file, $.buffer, sizeof ($.buffer), -1);
  await$ (cuv_fs_close, &$.file);
  exit$ ();
}
```

A timeout is expressed by putting the timeout in milliseconds before the operation. `await$` uses `ipe$` to distinguish the two forms at compile time:

```c
await$ (5000, cuv_tcp_read, &$.size, &$.tcp, $.buffer, sizeof ($.buffer));
```
