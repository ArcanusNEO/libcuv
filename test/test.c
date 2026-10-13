#include "cuv.h"

#include <assert.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/un.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

struct sleep_test
{
  struct cuv_task task;
  int step;
};

static void
sleep_test_run (struct cuv_task *task) async$ ((struct sleep_test *)task)
{
  $.step = 1;
  await$ (cuv_sleep, 1);
  $.step = 2;
  yield$ ();
  $.step = 3;
  exit$ ();
}

struct timeout_sleep_test
{
  struct cuv_task task;
  int reached;
};

static void
timeout_sleep_test_run (struct cuv_task *task)
    async$ ((struct timeout_sleep_test *)task)
{
  await$ (5, cuv_sleep, 1000);
  $.reached = 1;
  exit$ ();
}

struct timeout_success_test
{
  struct cuv_task task;
  int reached;
};

static void
timeout_success_test_run (struct cuv_task *task)
    async$ ((struct timeout_success_test *)task)
{
  await$ (1000, cuv_sleep, 1);
  $.reached = 1;
  exit$ ();
}

struct fs_test
{
  struct cuv_task task;
  struct cuv_file file;
  char const *path;
  char output[32];
  char input[32];
  isz size;
};

static void
fs_test_run (struct cuv_task *task) async$ ((struct fs_test *)task)
{
  await$ (cuv_fs_open, &$.file, $.path, O_CREAT | O_TRUNC | O_RDWR, 0600);
  await$ (cuv_fs_write, &$.size, &$.file, $.output, strlen ($.output), 0);
  await$ (cuv_fs_read, &$.size, &$.file, $.input, sizeof ($.input) - 1, 0);
  $.input[$.size] = 0;
  await$ (cuv_fs_close, &$.file);
  exit$ ();
}

struct cleanup_test
{
  struct cuv_task task;
  struct cuv_file file;
  char const *path;
  isz size;
};

static void
cleanup_test_run (struct cuv_task *task) async$ ((struct cleanup_test *)task)
{
  await$ (cuv_fs_open, &$.file, $.path, O_RDONLY, 0);
  await$ (cuv_fs_read, &$.size, &$.file, null, 1, 0);
  exit$ ();
}

struct multi_cleanup_test
{
  struct cuv_task task;
  struct cuv_file first;
  struct cuv_file second;
  char const *first_path;
  char const *second_path;
  isz size;
};

static void
multi_cleanup_test_run (struct cuv_task *task)
    async$ ((struct multi_cleanup_test *)task)
{
  await$ (cuv_fs_open, &$.first, $.first_path, O_RDONLY, 0);
  await$ (cuv_fs_open, &$.second, $.second_path, O_RDONLY, 0);
  await$ (cuv_fs_read, &$.size, &$.first, null, 1, 0);
  exit$ ();
}

struct child_test
{
  struct cuv_task task;
  int done;
};

static void
child_test_run (struct cuv_task *task) async$ ((struct child_test *)task)
{
  await$ (cuv_sleep, 1);
  $.done = 1;
  exit$ ();
}

struct parent_test
{
  struct cuv_task task;
  struct child_test *child;
  int done;
};

static void
parent_test_run (struct cuv_task *task) async$ ((struct parent_test *)task)
{
  cuv_task_start (&$.child->task);
  await$ (cuv_task_wait, &$.child->task);
  $.done = 1;
  exit$ ();
}

struct tcp_test
{
  struct cuv_task task;
  struct cuv_tcp tcp;
  struct sockaddr_storage address;
  char message[16];
};

static void
tcp_test_run (struct cuv_task *task) async$ ((struct tcp_test *)task)
{
  await$ (cuv_tcp_connect, &$.tcp, (struct sockaddr const *)&$.address);
  await$ (cuv_tcp_write, &$.tcp, $.message, strlen ($.message));
  await$ (cuv_tcp_close, &$.tcp);
  exit$ ();
}

struct dns_test
{
  struct cuv_task task;
  struct sockaddr_storage address;
};

static void
dns_test_run (struct cuv_task *task) async$ ((struct dns_test *)task)
{
  await$ (cuv_resolve, &$.address, "127.0.0.1", null);
  exit$ ();
}

struct cancel_child
{
  struct cuv_task task;
};

static void
cancel_child_run (struct cuv_task *task) async$ ((struct cancel_child *)task)
{
  await$ (cuv_sleep, 10000);
  exit$ ();
}

struct cancel_parent
{
  struct cuv_task task;
  struct cancel_child *child;
  int cancel_error;
};

static void
cancel_parent_run (struct cuv_task *task) async$ ((struct cancel_parent *)task)
{
  cuv_task_start (&$.child->task);
  yield$ ();
  $.cancel_error = cuv_task_cancel (&$.child->task);
  await$ (cuv_task_wait, &$.child->task);
  exit$ ();
}

struct tcp_read_test
{
  struct cuv_task task;
  struct cuv_tcp tcp;
  struct sockaddr_storage address;
  char buffer[32];
  isz size;
};

static void
tcp_read_test_run (struct cuv_task *task) async$ ((struct tcp_read_test *)task)
{
  await$ (cuv_tcp_connect, &$.tcp, (struct sockaddr const *)&$.address);
  await$ (1000, cuv_tcp_read, &$.size, &$.tcp, $.buffer, sizeof ($.buffer));
  await$ (cuv_tcp_close, &$.tcp);
  exit$ ();
}

struct tcp_timeout_test
{
  struct cuv_task task;
  struct cuv_tcp tcp;
  struct sockaddr_storage address;
  char buffer[8];
  isz size;
};

static void
tcp_timeout_test_run (struct cuv_task *task)
    async$ ((struct tcp_timeout_test *)task)
{
  await$ (cuv_tcp_connect, &$.tcp, (struct sockaddr const *)&$.address);
  await$ (5, cuv_tcp_read, &$.size, &$.tcp, $.buffer, sizeof ($.buffer));
  exit$ ();
}

struct tcp_cleanup_test
{
  struct cuv_task task;
  struct cuv_tcp tcp;
  struct sockaddr_storage address;
  isz size;
};

static void
tcp_cleanup_test_run (struct cuv_task *task)
    async$ ((struct tcp_cleanup_test *)task)
{
  await$ (cuv_tcp_connect, &$.tcp, (struct sockaddr const *)&$.address);
  await$ (cuv_tcp_read, &$.size, &$.tcp, null, 1);
  exit$ ();
}

struct server_thread_arg
{
  int listener;
  char const *message;
  useconds_t delay;
};

static void *
server_thread (void *opaque)
{
  struct server_thread_arg *arg = opaque;
  int peer = accept (arg->listener, null, null);
  assert (peer >= 0);

  if (arg->delay)
    usleep (arg->delay);

  if (arg->message)
    {
      usz length = strlen (arg->message);
      assert (write (peer, arg->message, length) == (isz)length);
    }
  close (peer);
  return null;
}

static void
test_sleep (void)
{
  struct cuv_loop loop;
  struct sleep_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, sleep_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (test.step == 3);
  assert (test.task.state == CUV_TASK_DONE);
  assert (cuv_task_error (&test.task) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_timeout_sleep (void)
{
  struct cuv_loop loop;
  struct timeout_sleep_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, timeout_sleep_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_timeout_success (void)
{
  struct cuv_loop loop;
  struct timeout_success_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, timeout_success_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (test.reached);
  assert (cuv_task_error (&test.task) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_fs (void)
{
  char path[] = "/tmp/libcuv-fs-XXXXXX";
  int fd = mkstemp (path);
  assert (fd >= 0);
  close (fd);

  struct cuv_loop loop;
  struct fs_test test = {
    .path = path,
  };
  strcpy (test.output, "libcuv filesystem");

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, fs_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (!strcmp (test.input, test.output));
  assert (!test.file.open);
  assert (cuv_loop_close (&loop) == 0);

  unlink (path);
}

static void
test_cleanup_on_error (void)
{
  char path[] = "/tmp/libcuv-cleanup-XXXXXX";
  int fd = mkstemp (path);
  assert (fd >= 0);
  close (fd);

  struct cuv_loop loop;
  struct cleanup_test test = {
    .path = path,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, cleanup_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == UV_EINVAL);
  assert (!test.file.open);
  assert (!test.file.resource.owner);
  assert (cuv_loop_close (&loop) == 0);

  unlink (path);
}

static void
test_multi_cleanup_on_error (void)
{
  char first_path[] = "/tmp/libcuv-cleanup-a-XXXXXX";
  char second_path[] = "/tmp/libcuv-cleanup-b-XXXXXX";
  int first_fd = mkstemp (first_path);
  int second_fd = mkstemp (second_path);
  assert (first_fd >= 0);
  assert (second_fd >= 0);
  close (first_fd);
  close (second_fd);

  struct cuv_loop loop;
  struct multi_cleanup_test test = {
    .first_path = first_path,
    .second_path = second_path,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, multi_cleanup_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == UV_EINVAL);
  assert (!test.first.open);
  assert (!test.second.open);
  assert (!test.first.resource.owner);
  assert (!test.second.resource.owner);
  assert (cuv_loop_close (&loop) == 0);

  unlink (first_path);
  unlink (second_path);
}

static void
test_task_wait (void)
{
  struct cuv_loop loop;
  struct child_test child = { 0 };
  struct parent_test parent = {
    .child = &child,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&child.task, &loop, child_test_run);
  cuv_task_init (&parent.task, &loop, parent_test_run);
  cuv_task_start (&parent.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (child.done);
  assert (parent.done);
  assert (cuv_task_error (&parent.task) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

static int
make_listener (struct sockaddr_in *address)
{
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  assert (fd >= 0);

  memset (address, 0, sizeof (*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl (INADDR_LOOPBACK);
  address->sin_port = 0;

  assert (bind (fd, (struct sockaddr *)address, sizeof (*address)) == 0);
  assert (listen (fd, 1) == 0);

  socklen_t size = sizeof (*address);
  assert (getsockname (fd, (struct sockaddr *)address, &size) == 0);
  return fd;
}

static void
test_tcp (void)
{
  struct sockaddr_in address;
  int listener = make_listener (&address);

  struct cuv_loop loop;
  struct tcp_test test = { 0 };
  memcpy (&test.address, &address, sizeof (address));
  strcpy (test.message, "hello");

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, tcp_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.tcp.state == CUV_TCP_CLOSED);
  assert (cuv_loop_close (&loop) == 0);

  int peer = accept (listener, null, null);
  assert (peer >= 0);
  char buffer[16] = { 0 };
  assert (read (peer, buffer, sizeof (buffer)) == 5);
  assert (!memcmp (buffer, "hello", 5));

  close (peer);
  close (listener);
}

static void
test_dns (void)
{
  struct cuv_loop loop;
  struct dns_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, dns_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.address.ss_family == AF_INET);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_cancel_sleep (void)
{
  struct cuv_loop loop;
  struct cancel_child child = { 0 };
  struct cancel_parent parent = {
    .child = &child,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&child.task, &loop, cancel_child_run);
  cuv_task_init (&parent.task, &loop, cancel_parent_run);
  cuv_task_start (&parent.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (parent.cancel_error == 0);
  assert (cuv_task_error (&child.task) == UV_ECANCELED);
  assert (cuv_task_error (&parent.task) == UV_ECANCELED);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_tcp_read (void)
{
  struct sockaddr_in address;
  int listener = make_listener (&address);
  struct server_thread_arg arg = {
    .listener = listener,
    .message = "from server",
  };
  pthread_t thread;
  assert (pthread_create (&thread, null, server_thread, &arg) == 0);

  struct cuv_loop loop;
  struct tcp_read_test test = { 0 };
  memcpy (&test.address, &address, sizeof (address));

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, tcp_read_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.size == (isz)strlen (arg.message));
  assert (!memcmp (test.buffer, arg.message, test.size));
  assert (test.tcp.state == CUV_TCP_CLOSED);
  assert (cuv_loop_close (&loop) == 0);

  assert (pthread_join (thread, null) == 0);
  close (listener);
}

static void
test_tcp_timeout (void)
{
  struct sockaddr_in address;
  int listener = make_listener (&address);
  struct server_thread_arg arg = {
    .listener = listener,
    .message = null,
    .delay = 50000,
  };
  pthread_t thread;
  assert (pthread_create (&thread, null, server_thread, &arg) == 0);

  struct cuv_loop loop;
  struct tcp_timeout_test test = { 0 };
  memcpy (&test.address, &address, sizeof (address));

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, tcp_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.tcp.state == CUV_TCP_CLOSED);
  assert (!test.tcp.resource.owner);
  assert (cuv_loop_close (&loop) == 0);

  assert (pthread_join (thread, null) == 0);
  close (listener);
}

static void
test_tcp_cleanup_on_error (void)
{
  struct sockaddr_in address;
  int listener = make_listener (&address);

  struct cuv_loop loop;
  struct tcp_cleanup_test test = { 0 };
  memcpy (&test.address, &address, sizeof (address));

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, tcp_cleanup_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == UV_EINVAL);
  assert (test.tcp.state == CUV_TCP_CLOSED);
  assert (!test.tcp.resource.owner);
  assert (cuv_loop_close (&loop) == 0);

  int peer = accept (listener, null, null);
  assert (peer >= 0);
  close (peer);
  close (listener);
}

struct work_test
{
  struct cuv_task task;
  pthread_t loop_thread;
  pthread_t worker_thread;
  int value;
};

static void
work_test_function (void *opaque)
{
  struct work_test *test = opaque;

  test->worker_thread = pthread_self ();
  test->value = 42;
}

static void
work_test_run (struct cuv_task *task) async$ ((struct work_test *)task)
{
  await$ (cuv_work, work_test_function, &$);
  exit$ ();
}

struct signal_test
{
  struct cuv_task task;
  struct cuv_signal signal;
  int received;
};

static void
signal_test_run (struct cuv_task *task) async$ ((struct signal_test *)task)
{
  await$ (cuv_signal_wait, &$.signal, SIGUSR1);
  $.received = $.signal.signum;
  await$ (cuv_signal_close, &$.signal);
  exit$ ();
}

struct signal_timeout_test
{
  struct cuv_task task;
  struct cuv_signal signal;
  int reached;
};

static void
signal_timeout_test_run (struct cuv_task *task)
    async$ ((struct signal_timeout_test *)task)
{
  await$ (5, cuv_signal_wait, &$.signal, SIGUSR2);
  $.reached = 1;
  exit$ ();
}

struct signal_thread_arg
{
  int signum;
  useconds_t delay;
};

static void *
signal_thread (void *opaque)
{
  struct signal_thread_arg *arg = opaque;

  usleep (arg->delay);
  assert (kill (getpid (), arg->signum) == 0);
  return null;
}

struct udp_send_test
{
  struct cuv_task task;
  struct cuv_udp udp;
  struct sockaddr_storage address;
  char const *message;
};

static void
udp_send_test_run (struct cuv_task *task) async$ ((struct udp_send_test *)task)
{
  await$ (cuv_udp_send, &$.udp, $.message, strlen ($.message),
          (struct sockaddr const *)&$.address);
  await$ (cuv_udp_close, &$.udp);
  exit$ ();
}

struct udp_recv_test
{
  struct cuv_task task;
  struct cuv_udp udp;
  struct sockaddr_storage bind_address;
  struct sockaddr_storage peer_address;
  char buffer[32];
  isz size;
  isz empty_size;
  unsigned flags;
};

static void
udp_recv_test_run (struct cuv_task *task) async$ ((struct udp_recv_test *)task)
{
  await$ (cuv_udp_bind, &$.udp, (struct sockaddr const *)&$.bind_address, 0);
  await$ (cuv_udp_recv, &$.size, &$.udp, $.buffer, sizeof ($.buffer),
          &$.peer_address, &$.flags);
  await$ (cuv_udp_recv, &$.empty_size, &$.udp, $.buffer, sizeof ($.buffer),
          &$.peer_address, &$.flags);
  await$ (cuv_udp_close, &$.udp);
  exit$ ();
}

struct udp_sender_arg
{
  struct sockaddr_in address;
  useconds_t delay;
};

static void *
udp_sender_thread (void *opaque)
{
  struct udp_sender_arg *arg = opaque;

  int fd = socket (AF_INET, SOCK_DGRAM, 0);
  assert (fd >= 0);

  usleep (arg->delay);
  assert (sendto (fd, "udp data", 8, 0, (struct sockaddr const *)&arg->address,
                  sizeof (arg->address))
          == 8);
  usleep (10000);
  assert (sendto (fd, "", 0, 0, (struct sockaddr const *)&arg->address,
                  sizeof (arg->address))
          == 0);

  close (fd);
  return null;
}

struct pipe_open_test
{
  struct cuv_task task;
  struct cuv_pipe pipe;
  int fd;
  char input[32];
  char const *output;
  isz size;
  isz try_size;
  usz queue_size;
  int readable;
  int writable;
};

static void
pipe_open_test_run (struct cuv_task *task)
    async$ ((struct pipe_open_test *)task)
{
  await$ (cuv_pipe_open, &$.pipe, $.fd, 0);
  await$ (cuv_pipe_is_readable, &$.pipe, &$.readable);
  await$ (cuv_pipe_is_writable, &$.pipe, &$.writable);
  await$ (cuv_pipe_write_queue, &$.pipe, &$.queue_size);
  await$ (cuv_pipe_set_blocking, &$.pipe, 0);
  await$ (cuv_pipe_try_write, &$.try_size, &$.pipe, "try ", 4);
  await$ (cuv_pipe_write, &$.pipe, $.output, strlen ($.output));
  await$ (cuv_pipe_read, &$.size, &$.pipe, $.input, sizeof ($.input));
  await$ (cuv_pipe_close, &$.pipe);
  exit$ ();
}

struct pipe_connect_test
{
  struct cuv_task task;
  struct cuv_pipe pipe;
  char const *path;
  char const *message;
};

static void
pipe_connect_test_run (struct cuv_task *task)
    async$ ((struct pipe_connect_test *)task)
{
  await$ (cuv_pipe_connect, &$.pipe, $.path, 0);
  await$ (cuv_pipe_write, &$.pipe, $.message, strlen ($.message));
  await$ (cuv_pipe_shutdown, &$.pipe);
  await$ (cuv_pipe_close, &$.pipe);
  exit$ ();
}

static int
make_udp_address (struct sockaddr_in *address)
{
  int fd = socket (AF_INET, SOCK_DGRAM, 0);
  assert (fd >= 0);

  memset (address, 0, sizeof (*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl (INADDR_LOOPBACK);
  address->sin_port = 0;

  assert (bind (fd, (struct sockaddr *)address, sizeof (*address)) == 0);

  socklen_t size = sizeof (*address);
  assert (getsockname (fd, (struct sockaddr *)address, &size) == 0);
  return fd;
}

static void
test_work (void)
{
  struct cuv_loop loop;
  struct work_test test = { 0 };

  test.loop_thread = pthread_self ();

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, work_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.value == 42);
  assert (!pthread_equal (test.loop_thread, test.worker_thread));
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_signal (void)
{
  struct cuv_loop loop;
  struct signal_test test = { 0 };
  struct signal_thread_arg arg = {
    .signum = SIGUSR1,
    .delay = 50000,
  };
  pthread_t thread;

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, signal_test_run);
  cuv_task_start (&test.task);
  assert (pthread_create (&thread, null, signal_thread, &arg) == 0);
  assert (cuv_loop_run (&loop) == 0);
  assert (pthread_join (thread, null) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.received == SIGUSR1);
  assert (test.signal.state == CUV_SIGNAL_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_signal_timeout (void)
{
  struct cuv_loop loop;
  struct signal_timeout_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, signal_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.signal.state == CUV_SIGNAL_CLOSED);
  assert (!test.signal.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_udp_send (void)
{
  struct sockaddr_in address;
  int receiver = make_udp_address (&address);

  struct cuv_loop loop;
  struct udp_send_test test = {
    .message = "hello udp",
  };
  memcpy (&test.address, &address, sizeof (address));

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, udp_send_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.udp.state == CUV_UDP_CLOSED);
  assert (cuv_loop_close (&loop) == 0);

  char buffer[32] = { 0 };
  assert (recv (receiver, buffer, sizeof (buffer), 0)
          == (isz)strlen (test.message));
  assert (!memcmp (buffer, test.message, strlen (test.message)));
  close (receiver);
}

static void
test_udp_recv (void)
{
  struct sockaddr_in address;
  int reservation = make_udp_address (&address);
  close (reservation);

  struct udp_sender_arg arg = {
    .address = address,
    .delay = 50000,
  };
  pthread_t thread;

  struct cuv_loop loop;
  struct udp_recv_test test = { 0 };
  memcpy (&test.bind_address, &address, sizeof (address));

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, udp_recv_test_run);
  cuv_task_start (&test.task);
  assert (pthread_create (&thread, null, udp_sender_thread, &arg) == 0);
  assert (cuv_loop_run (&loop) == 0);
  assert (pthread_join (thread, null) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.size == 8);
  assert (!memcmp (test.buffer, "udp data", 8));
  assert (test.empty_size == 0);
  assert (test.peer_address.ss_family == AF_INET);
  assert (test.udp.state == CUV_UDP_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_pipe_open (void)
{
  int sockets[2];
  assert (socketpair (AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  assert (write (sockets[1], "from peer", 9) == 9);

  struct cuv_loop loop;
  struct pipe_open_test test = {
    .fd = sockets[0],
    .output = "to peer",
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, pipe_open_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.size == 9);
  assert (!memcmp (test.input, "from peer", 9));
  assert (test.try_size == 4);
  assert (test.queue_size == 0);
  assert (test.readable);
  assert (test.writable);
  assert (test.pipe.state == CUV_PIPE_CLOSED);
  assert (cuv_loop_close (&loop) == 0);

  char buffer[16] = { 0 };
  assert (read (sockets[1], buffer, sizeof (buffer)) == 11);
  assert (!memcmp (buffer, "try to peer", 11));
  close (sockets[1]);
}

static void
test_pipe_connect (void)
{
  char path[] = "/tmp/libcuv-pipe-XXXXXX";
  int temporary = mkstemp (path);
  assert (temporary >= 0);
  close (temporary);
  unlink (path);

  int listener = socket (AF_UNIX, SOCK_STREAM, 0);
  assert (listener >= 0);

  struct sockaddr_un address = { 0 };
  address.sun_family = AF_UNIX;
  assert (strlen (path) < sizeof (address.sun_path));
  strcpy (address.sun_path, path);

  assert (bind (listener, (struct sockaddr *)&address, sizeof (address)) == 0);
  assert (listen (listener, 1) == 0);

  struct cuv_loop loop;
  struct pipe_connect_test test = {
    .path = path,
    .message = "pipe connect",
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, pipe_connect_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.pipe.state == CUV_PIPE_CLOSED);
  assert (cuv_loop_close (&loop) == 0);

  int peer = accept (listener, null, null);
  assert (peer >= 0);
  char buffer[32] = { 0 };
  assert (read (peer, buffer, sizeof (buffer)) == (isz)strlen (test.message));
  assert (!memcmp (buffer, test.message, strlen (test.message)));

  close (peer);
  close (listener);
  unlink (path);
}

struct work_timeout_test
{
  struct cuv_task task;
  int reached;
};

static void
work_timeout_function (void *opaque)
{
  (void)opaque;
  usleep (20000);
}

static void
work_timeout_test_run (struct cuv_task *task)
    async$ ((struct work_timeout_test *)task)
{
  await$ (5, cuv_work, work_timeout_function, &$);
  $.reached = 1;
  exit$ ();
}

struct udp_timeout_test
{
  struct cuv_task task;
  struct cuv_udp udp;
  struct sockaddr_storage address;
  char buffer[8];
  isz size;
};

static void
udp_timeout_test_run (struct cuv_task *task)
    async$ ((struct udp_timeout_test *)task)
{
  await$ (cuv_udp_bind, &$.udp, (struct sockaddr const *)&$.address, 0);
  await$ (5, cuv_udp_recv, &$.size, &$.udp, $.buffer, sizeof ($.buffer), null,
          null);
  exit$ ();
}

struct pipe_timeout_test
{
  struct cuv_task task;
  struct cuv_pipe pipe;
  int fd;
  char buffer[8];
  isz size;
};

static void
pipe_timeout_test_run (struct cuv_task *task)
    async$ ((struct pipe_timeout_test *)task)
{
  await$ (cuv_pipe_open, &$.pipe, $.fd, 0);
  await$ (5, cuv_pipe_read, &$.size, &$.pipe, $.buffer, sizeof ($.buffer));
  exit$ ();
}

static void
test_work_timeout (void)
{
  struct cuv_loop loop;
  struct work_timeout_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, work_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_udp_timeout (void)
{
  struct sockaddr_in address = { 0 };
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
  address.sin_port = 0;

  struct cuv_loop loop;
  struct udp_timeout_test test = { 0 };
  memcpy (&test.address, &address, sizeof (address));

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, udp_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.udp.state == CUV_UDP_CLOSED);
  assert (!test.udp.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_pipe_timeout (void)
{
  int sockets[2];
  assert (socketpair (AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

  struct cuv_loop loop;
  struct pipe_timeout_test test = {
    .fd = sockets[0],
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, pipe_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.pipe.state == CUV_PIPE_CLOSED);
  assert (!test.pipe.resource.owner);
  assert (cuv_loop_close (&loop) == 0);

  close (sockets[1]);
}

struct process_test
{
  struct cuv_task task;
  struct cuv_process process;
  i64 exit_status;
  int term_signal;
};

static void
process_test_run (struct cuv_task *task) async$ ((struct process_test *)task)
{
  char *args[] = { "/bin/sh", "-c", "exit 7", null };
  uv_process_options_t options = {
    .file = args[0],
    .args = args,
  };

  await$ (cuv_process_spawn, &$.process, &options);
  await$ (cuv_process_wait, &$.process, &$.exit_status, &$.term_signal);
  exit$ ();
}

struct process_failure_test
{
  struct cuv_task task;
  struct cuv_process process;
  int reached;
};

static void
process_failure_test_run (struct cuv_task *task)
    async$ ((struct process_failure_test *)task)
{
  char *args[] = { "/definitely/not/a/libcuv-program", null };
  uv_process_options_t options = {
    .file = args[0],
    .args = args,
  };

  await$ (cuv_process_spawn, &$.process, &options);
  $.reached = 1;
  exit$ ();
}

struct process_cleanup_test
{
  struct cuv_task task;
  struct cuv_process process;
};

static void
process_cleanup_test_run (struct cuv_task *task)
    async$ ((struct process_cleanup_test *)task)
{
  char *args[] = { "/bin/sh", "-c", "sleep 0.02", null };
  uv_process_options_t options = {
    .file = args[0],
    .args = args,
  };

  await$ (cuv_process_spawn, &$.process, &options);
  exit$ ();
}

struct process_error_cleanup_test
{
  struct cuv_task task;
  struct cuv_process process;
  struct cuv_file file;
  char const *path;
  isz size;
};

static void
process_error_cleanup_test_run (struct cuv_task *task)
    async$ ((struct process_error_cleanup_test *)task)
{
  await$ (cuv_fs_open, &$.file, $.path, O_RDONLY, 0);

  char *args[] = { "/bin/sh", "-c", "sleep 0.02", null };
  uv_process_options_t options = {
    .file = args[0],
    .args = args,
  };

  await$ (cuv_process_spawn, &$.process, &options);
  await$ (cuv_fs_read, &$.size, &$.file, null, 1, 0);
  exit$ ();
}

struct process_timeout_test
{
  struct cuv_task task;
  struct cuv_process process;
  i64 exit_status;
  int term_signal;
  int reached;
};

static void
process_timeout_test_run (struct cuv_task *task)
    async$ ((struct process_timeout_test *)task)
{
  char *args[] = { "/bin/sh", "-c", "sleep 0.02", null };
  uv_process_options_t options = {
    .file = args[0],
    .args = args,
  };

  await$ (cuv_process_spawn, &$.process, &options);
  await$ (1, cuv_process_wait, &$.process, &$.exit_status, &$.term_signal);
  $.reached = 1;
  exit$ ();
}

struct process_kill_test
{
  struct cuv_task task;
  struct cuv_process process;
  i64 exit_status;
  int term_signal;
  int kill_error;
};

static void
process_kill_test_run (struct cuv_task *task)
    async$ ((struct process_kill_test *)task)
{
  char *args[] = { "/bin/sleep", "10", null };
  uv_process_options_t options = {
    .file = args[0],
    .args = args,
  };

  await$ (cuv_process_spawn, &$.process, &options);
  $.kill_error = cuv_process_kill (&$.process, SIGTERM);
  await$ (cuv_process_wait, &$.process, &$.exit_status, &$.term_signal);
  exit$ ();
}

struct process_pid_kill_test
{
  struct cuv_task task;
  struct cuv_process process;
  uv_pid_t pid;
  i64 exit_status;
  int term_signal;
  int pid_error;
  int kill_error;
};

static void
process_pid_kill_test_run (struct cuv_task *task)
    async$ ((struct process_pid_kill_test *)task)
{
  char *args[] = { "/bin/sleep", "10", null };
  uv_process_options_t options = {
    .file = args[0],
    .args = args,
  };

  await$ (cuv_process_spawn, &$.process, &options);
  $.pid_error = cuv_process_pid (&$.process, &$.pid);
  $.kill_error = cuv_kill ($.pid, SIGTERM);
  await$ (cuv_process_wait, &$.process, &$.exit_status, &$.term_signal);
  exit$ ();
}

static void
test_process_pid_kill (void)
{
  struct cuv_loop loop;
  struct process_pid_kill_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_pid_kill_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.pid_error == CUV_READY);
  assert (test.pid > 0);
  assert (test.kill_error == 0);
  assert (test.term_signal == SIGTERM);
  assert (test.process.state == CUV_PROCESS_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_process_global_helpers (void)
{
  assert (cuv_disable_stdio_inheritance () == CUV_READY);
  assert (cuv_kill (0, SIGTERM) == UV_EINVAL);
  assert (cuv_kill (-1, SIGTERM) == UV_EINVAL);
  assert (cuv_kill (1, -1) == UV_EINVAL);

  struct cuv_process process = { 0 };
  uv_pid_t pid;
  pid = 0;
  assert (cuv_process_pid (&process, &pid) == UV_ESRCH);
  assert (cuv_process_pid (null, &pid) == UV_EINVAL);
  assert (cuv_process_pid (&process, null) == UV_EINVAL);
}

struct process_stdio_test
{
  struct cuv_task task;
  struct cuv_process process;
  struct cuv_pipe output;
  char buffer[64];
  isz size;
  i64 exit_status;
  int term_signal;
};

static void
process_stdio_test_run (struct cuv_task *task)
    async$ ((struct process_stdio_test *)task)
{
  char *args[] = { "/bin/sh", "-c", "printf phase-c", null };

  await$ (cuv_pipe_init, &$.output, 0);

  uv_stdio_container_t stdio[3] = {
    { .flags = UV_IGNORE },
    {
        .flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE,
        .data.stream = (uv_stream_t *)&$.output.uv,
    },
    { .flags = UV_IGNORE },
  };
  uv_process_options_t options = {
    .file = args[0],
    .args = args,
    .stdio_count = 3,
    .stdio = stdio,
  };

  await$ (cuv_process_spawn, &$.process, &options);
  await$ (cuv_pipe_read, &$.size, &$.output, $.buffer, sizeof ($.buffer));
  await$ (cuv_pipe_close, &$.output);
  await$ (cuv_process_wait, &$.process, &$.exit_status, &$.term_signal);
  exit$ ();
}

static void
test_process (void)
{
  struct cuv_loop loop;
  struct process_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.exit_status == 7);
  assert (test.term_signal == 0);
  assert (test.process.state == CUV_PROCESS_CLOSED);
  assert (!test.process.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_process_failure (void)
{
  struct cuv_loop loop;
  struct process_failure_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_failure_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ENOENT);
  assert (test.process.state == CUV_PROCESS_CLOSED);
  assert (!test.process.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_process_cleanup (void)
{
  struct cuv_loop loop;
  struct process_cleanup_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_cleanup_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.process.state == CUV_PROCESS_CLOSED);
  assert (!test.process.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_process_error_cleanup (void)
{
  char path[] = "/tmp/libcuv-process-cleanup-XXXXXX";
  int fd = mkstemp (path);
  assert (fd >= 0);
  close (fd);

  struct cuv_loop loop;
  struct process_error_cleanup_test test = {
    .path = path,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_error_cleanup_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == UV_EINVAL);
  assert (!test.file.open);
  assert (!test.file.resource.owner);
  assert (test.process.state == CUV_PROCESS_CLOSED);
  assert (!test.process.resource.owner);
  assert (cuv_loop_close (&loop) == 0);

  unlink (path);
}

static void
test_process_timeout (void)
{
  struct cuv_loop loop;
  struct process_timeout_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.process.state == CUV_PROCESS_CLOSED);
  assert (!test.process.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_process_kill (void)
{
  struct cuv_loop loop;
  struct process_kill_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_kill_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.kill_error == 0);
  assert (test.term_signal == SIGTERM);
  assert (test.process.state == CUV_PROCESS_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_process_stdio (void)
{
  struct cuv_loop loop;
  struct process_stdio_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_stdio_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.size == 7);
  assert (!memcmp (test.buffer, "phase-c", 7));
  assert (test.exit_status == 0);
  assert (test.term_signal == 0);
  assert (test.output.state == CUV_PIPE_CLOSED);
  assert (test.process.state == CUV_PROCESS_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
}

struct process_run_capture_test
{
  struct cuv_task task;
  struct cuv_process_run run;
  char output[64];
  char error_output[64];
  usz output_size;
  usz error_output_size;
  i64 exit_status;
  int term_signal;
  int clear_error;
};

static void
process_run_capture_test_run (struct cuv_task *task)
    async$ ((struct process_run_capture_test *)task)
{
  char *args[] = {
    "/bin/sh",
    "-c",
    "printf phase-l-out; printf phase-l-err >&2; exit 7",
    null,
  };
  struct cuv_process_run_options options = {
    .process = {
      .file = args[0],
      .args = args,
    },
    .output = $.output,
    .output_capacity = sizeof ($.output),
    .error_output = $.error_output,
    .error_output_capacity = sizeof ($.error_output),
    .kill_signal = SIGTERM,
  };

  await$ (cuv_process_run, &$.run, &options);

  $.output_size = $.run.output_size;
  $.error_output_size = $.run.error_output_size;
  $.exit_status = $.run.exit_status;
  $.term_signal = $.run.term_signal;
  $.clear_error = cuv_process_run_clear (&$.run);
  exit$ ();
}

static void
test_process_run_capture (void)
{
  struct cuv_loop loop;
  struct process_run_capture_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_run_capture_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.output_size == 11);
  assert (!memcmp (test.output, "phase-l-out", 11));
  assert (test.error_output_size == 11);
  assert (!memcmp (test.error_output, "phase-l-err", 11));
  assert (test.exit_status == 7);
  assert (test.term_signal == 0);
  assert (test.clear_error == 0);
  assert (!test.run.initialized);
  assert (cuv_loop_close (&loop) == 0);
}

struct process_run_input_test
{
  struct cuv_task task;
  struct cuv_process_run run;
  char output[128];
};

static void
process_run_input_test_run (struct cuv_task *task)
    async$ ((struct process_run_input_test *)task)
{
  static char const input[] = "phase-l-input\n";
  char *args[] = { "/bin/cat", null };
  struct cuv_process_run_options options = {
    .process = {
      .file = args[0],
      .args = args,
    },
    .input = input,
    .input_size = sizeof (input) - 1,
    .output = $.output,
    .output_capacity = sizeof ($.output),
    .kill_signal = SIGTERM,
  };

  await$ (cuv_process_run, &$.run, &options);
  exit$ ();
}

static void
test_process_run_input (void)
{
  struct cuv_loop loop;
  struct process_run_input_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_run_input_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.run.output_size == 14);
  assert (!memcmp (test.output, "phase-l-input\n", 14));
  assert (!test.run.output_truncated);
  assert (test.run.exit_status == 0);
  assert (test.run.process.state == CUV_PROCESS_CLOSED);
  assert (test.run.input_pipe.state == CUV_PIPE_CLOSED);
  assert (test.run.output_pipe.state == CUV_PIPE_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
}

struct process_run_truncate_test
{
  struct cuv_task task;
  struct cuv_process_run run;
  char output[32];
  char error_output[32];
};

static void
process_run_truncate_test_run (struct cuv_task *task)
    async$ ((struct process_run_truncate_test *)task)
{
  char *args[] = {
    "/bin/sh",
    "-c",
    "i=0; while [ $i -lt 10000 ]; do "
    "printf 0123456789; printf abcdefghij >&2; i=$((i+1)); done",
    null,
  };
  struct cuv_process_run_options options = {
    .process = {
      .file = args[0],
      .args = args,
    },
    .output = $.output,
    .output_capacity = sizeof ($.output),
    .error_output = $.error_output,
    .error_output_capacity = sizeof ($.error_output),
    .kill_signal = SIGTERM,
  };

  await$ (cuv_process_run, &$.run, &options);
  exit$ ();
}

static void
test_process_run_truncate (void)
{
  struct cuv_loop loop;
  struct process_run_truncate_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_run_truncate_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.run.output_size == sizeof (test.output));
  assert (test.run.error_output_size == sizeof (test.error_output));
  assert (test.run.output_truncated);
  assert (test.run.error_output_truncated);
  assert (test.run.exit_status == 0);
  assert (test.run.process.state == CUV_PROCESS_CLOSED);
  assert (test.run.output_pipe.state == CUV_PIPE_CLOSED);
  assert (test.run.error_pipe.state == CUV_PIPE_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
}

struct process_run_timeout_test
{
  struct cuv_task task;
  struct cuv_process_run run;
  int reached;
};

static void
process_run_timeout_test_run (struct cuv_task *task)
    async$ ((struct process_run_timeout_test *)task)
{
  char *args[] = { "/bin/sleep", "10", null };
  struct cuv_process_run_options options = {
    .process = {
      .file = args[0],
      .args = args,
    },
    .kill_signal = SIGTERM,
  };

  await$ (10, cuv_process_run, &$.run, &options);
  $.reached = 1;
  exit$ ();
}

static void
test_process_run_timeout (void)
{
  struct cuv_loop loop;
  struct process_run_timeout_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_run_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.run.kill_error == 0);
  assert (test.run.process.state == CUV_PROCESS_CLOSED);
  assert (!test.run.process.resource.owner);
  assert (!test.run.group.resource.owner);
  assert (test.run.group.count == 0);
  assert (cuv_loop_close (&loop) == 0);
}

struct process_run_logical_timeout_test
{
  struct cuv_task task;
  struct cuv_process_run run;
  char output[32];
  int reached;
};

static void
process_run_logical_timeout_test_run (struct cuv_task *task)
    async$ ((struct process_run_logical_timeout_test *)task)
{
  char *args[] = { "/bin/sh", "-c", "sleep 0.02; printf survived", null };
  struct cuv_process_run_options options = {
    .process = {
      .file = args[0],
      .args = args,
    },
    .output = $.output,
    .output_capacity = sizeof ($.output),
    .kill_signal = 0,
  };

  await$ (1, cuv_process_run, &$.run, &options);
  $.reached = 1;
  exit$ ();
}

static void
test_process_run_logical_timeout (void)
{
  struct cuv_loop loop;
  struct process_run_logical_timeout_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_run_logical_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.run.kill_error == 0);
  assert (test.run.output_size == 8);
  assert (!memcmp (test.output, "survived", 8));
  assert (test.run.process.state == CUV_PROCESS_CLOSED);
  assert (test.run.output_pipe.state == CUV_PIPE_CLOSED);
  assert (!test.run.process.resource.owner);
  assert (!test.run.group.resource.owner);
  assert (test.run.group.count == 0);
  assert (cuv_loop_close (&loop) == 0);
}

struct process_run_failure_test
{
  struct cuv_task task;
  struct cuv_process_run run;
  int reached;
};

static void
process_run_failure_test_run (struct cuv_task *task)
    async$ ((struct process_run_failure_test *)task)
{
  char *args[] = { "/definitely/not/a/libcuv-phase-l-program", null };
  struct cuv_process_run_options options = {
    .process = {
      .file = args[0],
      .args = args,
    },
    .kill_signal = SIGTERM,
  };

  await$ (cuv_process_run, &$.run, &options);
  $.reached = 1;
  exit$ ();
}

static void
test_process_run_failure (void)
{
  struct cuv_loop loop;
  struct process_run_failure_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, process_run_failure_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ENOENT);
  assert (test.run.process.state == CUV_PROCESS_CLOSED);
  assert (!test.run.process.resource.owner);
  assert (!test.run.group.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static int
test_error_operation (struct cuv_task *task, int error)
{
  (void)task;
  return error;
}

struct group_child_test
{
  struct cuv_task task;
  u64 delay;
  int value;
  int reached;
};

static void
group_child_test_run (struct cuv_task *task)
    async$ ((struct group_child_test *)task)
{
  await$ (cuv_sleep, $.delay);
  $.reached = $.value;
  exit$ ();
}

struct group_fail_child_test
{
  struct cuv_task task;
  int error;
  int reached;
};

static void
group_fail_child_test_run (struct cuv_task *task)
    async$ ((struct group_fail_child_test *)task)
{
  await$ (test_error_operation, $.error);
  $.reached = 1;
  exit$ ();
}

struct group_wait_test
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct group_child_test *first;
  struct group_child_test *second;
  struct group_child_test *third;
  int done;
};

static void
group_wait_test_run (struct cuv_task *task)
    async$ ((struct group_wait_test *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);
  assert (cuv_task_group_start (&$.group, &$.first->task) == 0);
  assert (cuv_task_group_start (&$.group, &$.second->task) == 0);
  assert (cuv_task_group_start (&$.group, &$.third->task) == 0);

  await$ (cuv_task_group_wait, &$.group);
  $.done = 1;
  exit$ ();
}

struct group_error_test
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct group_fail_child_test *failed;
  struct group_child_test *other;
  int reached;
};

static void
group_error_test_run (struct cuv_task *task)
    async$ ((struct group_error_test *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);
  assert (cuv_task_group_start (&$.group, &$.failed->task) == 0);
  assert (cuv_task_group_start (&$.group, &$.other->task) == 0);

  await$ (cuv_task_group_wait, &$.group);
  $.reached = 1;
  exit$ ();
}

struct group_race_test
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct group_child_test *first;
  struct group_child_test *second;
  struct group_child_test *third;
  struct cuv_task *winner;
  int done;
};

static void
group_race_test_run (struct cuv_task *task)
    async$ ((struct group_race_test *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);
  assert (cuv_task_group_start (&$.group, &$.first->task) == 0);
  assert (cuv_task_group_start (&$.group, &$.second->task) == 0);
  assert (cuv_task_group_start (&$.group, &$.third->task) == 0);

  await$ (cuv_task_group_race, &$.group, &$.winner);
  $.done = 1;
  exit$ ();
}

struct group_cleanup_test
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct group_child_test *first;
  struct group_child_test *second;
  int reached;
};

static void
group_cleanup_test_run (struct cuv_task *task)
    async$ ((struct group_cleanup_test *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);
  assert (cuv_task_group_start (&$.group, &$.first->task) == 0);
  assert (cuv_task_group_start (&$.group, &$.second->task) == 0);

  await$ (test_error_operation, UV_EINVAL);
  $.reached = 1;
  exit$ ();
}

struct group_timeout_test
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct group_child_test *child;
  int reached;
};

static void
group_timeout_test_run (struct cuv_task *task)
    async$ ((struct group_timeout_test *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);
  assert (cuv_task_group_start (&$.group, &$.child->task) == 0);

  await$ (1, cuv_task_group_wait, &$.group);
  $.reached = 1;
  exit$ ();
}

struct group_reuse_test
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct group_child_test *first;
  struct group_child_test *second;
  int batches;
};

static void
group_reuse_test_run (struct cuv_task *task)
    async$ ((struct group_reuse_test *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);

  assert (cuv_task_group_start (&$.group, &$.first->task) == 0);
  await$ (cuv_task_group_wait, &$.group);
  ++$.batches;

  assert (cuv_task_group_start (&$.group, &$.second->task) == 0);
  await$ (cuv_task_group_wait, &$.group);
  ++$.batches;

  exit$ ();
}

struct group_cancel_test
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct group_child_test *first;
  struct group_child_test *second;
  int cancel_error;
  int reached;
};

static void
group_cancel_test_run (struct cuv_task *task)
    async$ ((struct group_cancel_test *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);
  assert (cuv_task_group_start (&$.group, &$.first->task) == 0);
  assert (cuv_task_group_start (&$.group, &$.second->task) == 0);

  $.cancel_error = cuv_task_group_cancel (&$.group);
  await$ (cuv_task_group_wait, &$.group);
  $.reached = 1;
  exit$ ();
}

static void
test_task_group_wait (void)
{
  struct cuv_loop loop;
  struct group_child_test first = { .delay = 3, .value = 1 };
  struct group_child_test second = { .delay = 1, .value = 2 };
  struct group_child_test third = { .delay = 2, .value = 3 };
  struct group_wait_test test = {
    .first = &first,
    .second = &second,
    .third = &third,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&first.task, &loop, group_child_test_run);
  cuv_task_init (&second.task, &loop, group_child_test_run);
  cuv_task_init (&third.task, &loop, group_child_test_run);
  cuv_task_init (&test.task, &loop, group_wait_test_run);
  cuv_task_start (&test.task);

  assert (cuv_loop_run (&loop) == 0);
  assert (test.done);
  assert (first.reached == 1);
  assert (second.reached == 2);
  assert (third.reached == 3);
  assert (cuv_task_error (&test.task) == 0);
  assert (!test.group.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_task_group_error (void)
{
  struct cuv_loop loop;
  struct group_fail_child_test failed = { .error = UV_EINVAL };
  struct group_child_test other = { .delay = 1, .value = 7 };
  struct group_error_test test = {
    .failed = &failed,
    .other = &other,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&failed.task, &loop, group_fail_child_test_run);
  cuv_task_init (&other.task, &loop, group_child_test_run);
  cuv_task_init (&test.task, &loop, group_error_test_run);
  cuv_task_start (&test.task);

  assert (cuv_loop_run (&loop) == 0);
  assert (!test.reached);
  assert (!failed.reached);
  assert (other.reached == 7);
  assert (cuv_task_error (&failed.task) == UV_EINVAL);
  assert (cuv_task_error (&test.task) == UV_EINVAL);
  assert (!test.group.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_task_group_race (void)
{
  struct cuv_loop loop;
  struct group_child_test first = { .delay = 1, .value = 11 };
  struct group_child_test second = { .delay = 1000, .value = 22 };
  struct group_child_test third = { .delay = 1000, .value = 33 };
  struct group_race_test test = {
    .first = &first,
    .second = &second,
    .third = &third,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&first.task, &loop, group_child_test_run);
  cuv_task_init (&second.task, &loop, group_child_test_run);
  cuv_task_init (&third.task, &loop, group_child_test_run);
  cuv_task_init (&test.task, &loop, group_race_test_run);
  cuv_task_start (&test.task);

  assert (cuv_loop_run (&loop) == 0);
  assert (test.done);
  assert (test.winner == &first.task);
  assert (first.reached == 11);
  assert (!second.reached);
  assert (!third.reached);
  assert (cuv_task_error (&second.task) == UV_ECANCELED);
  assert (cuv_task_error (&third.task) == UV_ECANCELED);
  assert (cuv_task_error (&test.task) == 0);
  assert (!test.group.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_task_group_cleanup (void)
{
  struct cuv_loop loop;
  struct group_child_test first = { .delay = 1000, .value = 1 };
  struct group_child_test second = { .delay = 1000, .value = 2 };
  struct group_cleanup_test test = {
    .first = &first,
    .second = &second,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&first.task, &loop, group_child_test_run);
  cuv_task_init (&second.task, &loop, group_child_test_run);
  cuv_task_init (&test.task, &loop, group_cleanup_test_run);
  cuv_task_start (&test.task);

  assert (cuv_loop_run (&loop) == 0);
  assert (!test.reached);
  assert (!first.reached);
  assert (!second.reached);
  assert (cuv_task_error (&test.task) == UV_EINVAL);
  assert (cuv_task_error (&first.task) == UV_ECANCELED);
  assert (cuv_task_error (&second.task) == UV_ECANCELED);
  assert (!test.group.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_task_group_timeout (void)
{
  struct cuv_loop loop;
  struct group_child_test child = { .delay = 1000, .value = 1 };
  struct group_timeout_test test = { .child = &child };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&child.task, &loop, group_child_test_run);
  cuv_task_init (&test.task, &loop, group_timeout_test_run);
  cuv_task_start (&test.task);

  assert (cuv_loop_run (&loop) == 0);
  assert (!test.reached);
  assert (!child.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (cuv_task_error (&child.task) == UV_ECANCELED);
  assert (!test.group.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_task_group_reuse (void)
{
  struct cuv_loop loop;
  struct group_child_test first = { .delay = 1, .value = 1 };
  struct group_child_test second = { .delay = 1, .value = 2 };
  struct group_reuse_test test = {
    .first = &first,
    .second = &second,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&first.task, &loop, group_child_test_run);
  cuv_task_init (&second.task, &loop, group_child_test_run);
  cuv_task_init (&test.task, &loop, group_reuse_test_run);
  cuv_task_start (&test.task);

  assert (cuv_loop_run (&loop) == 0);
  assert (test.batches == 2);
  assert (first.reached == 1);
  assert (second.reached == 2);
  assert (cuv_task_error (&test.task) == 0);
  assert (!test.group.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

static void
test_task_group_cancel (void)
{
  struct cuv_loop loop;
  struct group_child_test first = { .delay = 1000, .value = 1 };
  struct group_child_test second = { .delay = 1000, .value = 2 };
  struct group_cancel_test test = {
    .first = &first,
    .second = &second,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&first.task, &loop, group_child_test_run);
  cuv_task_init (&second.task, &loop, group_child_test_run);
  cuv_task_init (&test.task, &loop, group_cancel_test_run);
  cuv_task_start (&test.task);

  assert (cuv_loop_run (&loop) == 0);
  assert (test.cancel_error == 0);
  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ECANCELED);
  assert (cuv_task_error (&first.task) == UV_ECANCELED);
  assert (cuv_task_error (&second.task) == UV_ECANCELED);
  assert (!test.group.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

struct tcp_server_test
{
  struct cuv_task task;
  struct cuv_tcp server;
  struct cuv_tcp client;
  struct sockaddr_in address;
  char buffer[32];
  isz size;
};

static void
tcp_server_test_run (struct cuv_task *task)
    async$ ((struct tcp_server_test *)task)
{
  await$ (cuv_tcp_bind, &$.server, (struct sockaddr const *)&$.address, 0);
  await$ (cuv_tcp_listen, &$.server, 16);
  await$ (1000, cuv_tcp_accept, &$.server, &$.client);
  await$ (cuv_tcp_read, &$.size, &$.client, $.buffer, sizeof ($.buffer));
  await$ (cuv_tcp_write, &$.client, "server reply", 12);
  await$ (cuv_tcp_close, &$.client);
  await$ (cuv_tcp_close, &$.server);
  exit$ ();
}

struct tcp_server_client_arg
{
  struct sockaddr_in address;
  useconds_t delay;
  char response[32];
  isz size;
};

static void *
tcp_server_client_thread (void *opaque)
{
  struct tcp_server_client_arg *arg = opaque;

  usleep (arg->delay);

  int fd = socket (AF_INET, SOCK_STREAM, 0);
  assert (fd >= 0);
  assert (connect (fd, (struct sockaddr const *)&arg->address,
                   sizeof (arg->address))
          == 0);
  assert (write (fd, "client data", 11) == 11);

  arg->size = read (fd, arg->response, sizeof (arg->response));
  assert (arg->size == 12);

  close (fd);
  return null;
}

static void
test_tcp_server (void)
{
  struct sockaddr_in address;
  int reservation = make_listener (&address);
  close (reservation);

  struct cuv_loop loop;
  struct tcp_server_test test = { 0 };
  test.address = address;

  struct tcp_server_client_arg arg = {
    .address = address,
    .delay = 20000,
  };
  pthread_t thread;

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, tcp_server_test_run);
  cuv_task_start (&test.task);
  assert (pthread_create (&thread, null, tcp_server_client_thread, &arg) == 0);
  assert (cuv_loop_run (&loop) == 0);
  assert (pthread_join (thread, null) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (test.size == 11);
  assert (!memcmp (test.buffer, "client data", 11));
  assert (!memcmp (arg.response, "server reply", 12));
  assert (test.client.state == CUV_TCP_CLOSED);
  assert (test.server.state == CUV_TCP_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
}

struct tcp_transfer_child
{
  struct cuv_task task;
  struct cuv_tcp tcp;
  char buffer[32];
  isz size;
};

static void
tcp_transfer_child_run (struct cuv_task *task)
    async$ ((struct tcp_transfer_child *)task)
{
  await$ (cuv_tcp_read, &$.size, &$.tcp, $.buffer, sizeof ($.buffer));
  await$ (cuv_tcp_close, &$.tcp);
  exit$ ();
}

struct tcp_transfer_server
{
  struct cuv_task task;
  struct cuv_tcp server;
  struct tcp_transfer_child *child;
  struct sockaddr_in address;
};

static void
tcp_transfer_server_run (struct cuv_task *task)
    async$ ((struct tcp_transfer_server *)task)
{
  await$ (cuv_tcp_bind, &$.server, (struct sockaddr const *)&$.address, 0);
  await$ (cuv_tcp_listen, &$.server, 16);
  await$ (1000, cuv_tcp_accept, &$.server, &$.child->tcp);

  cuv_resource_transfer (&$.child->tcp.resource, &$.child->task);
  cuv_task_start (&$.child->task);

  await$ (cuv_tcp_close, &$.server);
  await$ (cuv_task_wait, &$.child->task);
  exit$ ();
}

struct tcp_transfer_client_arg
{
  struct sockaddr_in address;
  useconds_t delay;
};

static void *
tcp_transfer_client_thread (void *opaque)
{
  struct tcp_transfer_client_arg *arg = opaque;

  usleep (arg->delay);

  int fd = socket (AF_INET, SOCK_STREAM, 0);
  assert (fd >= 0);
  assert (connect (fd, (struct sockaddr const *)&arg->address,
                   sizeof (arg->address))
          == 0);
  assert (write (fd, "transferred", 11) == 11);
  close (fd);
  return null;
}

static void
test_tcp_accept_transfer (void)
{
  struct sockaddr_in address;
  int reservation = make_listener (&address);
  close (reservation);

  struct cuv_loop loop;
  struct tcp_transfer_child child = { 0 };
  struct tcp_transfer_server server = {
    .child = &child,
    .address = address,
  };
  struct tcp_transfer_client_arg arg = {
    .address = address,
    .delay = 20000,
  };
  pthread_t thread;

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&child.task, &loop, tcp_transfer_child_run);
  cuv_task_init (&server.task, &loop, tcp_transfer_server_run);
  cuv_task_start (&server.task);
  assert (pthread_create (&thread, null, tcp_transfer_client_thread, &arg)
          == 0);
  assert (cuv_loop_run (&loop) == 0);
  assert (pthread_join (thread, null) == 0);

  assert (cuv_task_error (&server.task) == 0);
  assert (cuv_task_error (&child.task) == 0);
  assert (child.size == 11);
  assert (!memcmp (child.buffer, "transferred", 11));
  assert (child.tcp.state == CUV_TCP_CLOSED);
  assert (server.server.state == CUV_TCP_CLOSED);
  assert (!child.tcp.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

struct tcp_accept_timeout_test
{
  struct cuv_task task;
  struct cuv_tcp server;
  struct cuv_tcp client;
  struct sockaddr_in address;
  int reached;
};

static void
tcp_accept_timeout_test_run (struct cuv_task *task)
    async$ ((struct tcp_accept_timeout_test *)task)
{
  await$ (cuv_tcp_bind, &$.server, (struct sockaddr const *)&$.address, 0);
  await$ (cuv_tcp_listen, &$.server, 16);
  await$ (5, cuv_tcp_accept, &$.server, &$.client);
  $.reached = 1;
  exit$ ();
}

static void
test_tcp_accept_timeout (void)
{
  struct sockaddr_in address;
  int reservation = make_listener (&address);
  close (reservation);

  struct cuv_loop loop;
  struct tcp_accept_timeout_test test = { 0 };
  test.address = address;

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, tcp_accept_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.server.state == CUV_TCP_CLOSED);
  assert (test.client.state == CUV_TCP_NEW);
  assert (!test.server.resource.owner);
  assert (!test.client.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

struct pipe_server_test
{
  struct cuv_task task;
  struct cuv_pipe server;
  struct cuv_pipe client;
  char const *path;
  char buffer[32];
  isz size;
  int saw_pending;
};

static void
pipe_server_test_run (struct cuv_task *task)
    async$ ((struct pipe_server_test *)task)
{
  await$ (cuv_pipe_bind, &$.server, $.path, 0);
  await$ (cuv_pipe_listen, &$.server, 16);

  /* Let the connection callback run before accept to test queued accept. */
  await$ (cuv_sleep, 50);
  $.saw_pending = $.server.connection_pending;

  await$ (cuv_pipe_accept, &$.server, &$.client, 0);
  await$ (cuv_pipe_read, &$.size, &$.client, $.buffer, sizeof ($.buffer));
  await$ (cuv_pipe_write, &$.client, "pipe reply", 10);
  await$ (cuv_pipe_close, &$.client);
  await$ (cuv_pipe_close, &$.server);
  exit$ ();
}

struct pipe_server_client_arg
{
  char const *path;
  useconds_t delay;
  char response[32];
  isz size;
};

static void *
pipe_server_client_thread (void *opaque)
{
  struct pipe_server_client_arg *arg = opaque;

  usleep (arg->delay);

  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  assert (fd >= 0);

  struct sockaddr_un address = { 0 };
  address.sun_family = AF_UNIX;
  assert (strlen (arg->path) < sizeof (address.sun_path));
  strcpy (address.sun_path, arg->path);

  assert (connect (fd, (struct sockaddr const *)&address, sizeof (address))
          == 0);
  assert (write (fd, "pipe client", 11) == 11);

  arg->size = read (fd, arg->response, sizeof (arg->response));
  assert (arg->size == 10);

  close (fd);
  return null;
}

static void
test_pipe_server (void)
{
  char path[] = "/tmp/libcuv-server-pipe-XXXXXX";
  int temporary = mkstemp (path);
  assert (temporary >= 0);
  close (temporary);
  unlink (path);

  struct cuv_loop loop;
  struct pipe_server_test test = {
    .path = path,
  };
  struct pipe_server_client_arg arg = {
    .path = path,
    .delay = 5000,
  };
  pthread_t thread;

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, pipe_server_test_run);
  cuv_task_start (&test.task);
  assert (pthread_create (&thread, null, pipe_server_client_thread, &arg)
          == 0);
  assert (cuv_loop_run (&loop) == 0);
  assert (pthread_join (thread, null) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (test.saw_pending);
  assert (test.size == 11);
  assert (!memcmp (test.buffer, "pipe client", 11));
  assert (!memcmp (arg.response, "pipe reply", 10));
  assert (test.client.state == CUV_PIPE_CLOSED);
  assert (test.server.state == CUV_PIPE_CLOSED);
  assert (cuv_loop_close (&loop) == 0);

  unlink (path);
}

struct pipe_accept_timeout_test
{
  struct cuv_task task;
  struct cuv_pipe server;
  struct cuv_pipe client;
  char const *path;
  int reached;
};

static void
pipe_accept_timeout_test_run (struct cuv_task *task)
    async$ ((struct pipe_accept_timeout_test *)task)
{
  await$ (cuv_pipe_bind, &$.server, $.path, 0);
  await$ (cuv_pipe_listen, &$.server, 16);
  await$ (5, cuv_pipe_accept, &$.server, &$.client, 0);
  $.reached = 1;
  exit$ ();
}

static void
test_pipe_accept_timeout (void)
{
  char path[] = "/tmp/libcuv-server-timeout-XXXXXX";
  int temporary = mkstemp (path);
  assert (temporary >= 0);
  close (temporary);
  unlink (path);

  struct cuv_loop loop;
  struct pipe_accept_timeout_test test = {
    .path = path,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, pipe_accept_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.server.state == CUV_PIPE_CLOSED);
  assert (test.client.state == CUV_PIPE_NEW);
  assert (!test.server.resource.owner);
  assert (!test.client.resource.owner);
  assert (cuv_loop_close (&loop) == 0);

  unlink (path);
}

struct fs_adapter_test
{
  struct cuv_task task;
  struct cuv_file file;
  struct cuv_dirlist list;
  char const *directory;
  char const *path;
  char const *renamed;
  uv_stat_t stat;
  uv_stat_t fstat;
  isz size;
  int saw_entry;
};

static void
fs_adapter_test_run (struct cuv_task *task)
    async$ ((struct fs_adapter_test *)task)
{
  await$ (cuv_fs_mkdir, $.directory, 0700);
  await$ (cuv_fs_open, &$.file, $.path, O_CREAT | O_TRUNC | O_RDWR, 0600);
  await$ (cuv_fs_write, &$.size, &$.file, "phase-f", 7, 0);
  await$ (cuv_fs_fstat, &$.fstat, &$.file);
  await$ (cuv_fs_close, &$.file);
  await$ (cuv_fs_stat, &$.stat, $.path);
  await$ (cuv_fs_rename, $.path, $.renamed);
  await$ (cuv_fs_scandir, &$.list, $.directory, 0);

  for (usz index = 0; index < $.list.count; ++index)
    if (!strcmp ($.list.entries[index].name, "renamed.txt"))
      $.saw_entry = 1;

  cuv_dirlist_clear (&$.list);
  await$ (cuv_fs_unlink, $.renamed);
  exit$ ();
}

static void
test_fs_adapters (void)
{
  char root[] = "/tmp/libcuv-fs-phase-f-XXXXXX";
  assert (mkdtemp (root));

  char directory[512];
  char path[512];
  char renamed[512];
  assert (snprintf (directory, sizeof (directory), "%s/sub", root)
          < (int)sizeof (directory));
  assert (snprintf (path, sizeof (path), "%s/original.txt", directory)
          < (int)sizeof (path));
  assert (snprintf (renamed, sizeof (renamed), "%s/renamed.txt", directory)
          < (int)sizeof (renamed));

  struct cuv_loop loop;
  struct fs_adapter_test test = {
    .directory = directory,
    .path = path,
    .renamed = renamed,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, fs_adapter_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (test.fstat.st_size == 7);
  assert (test.stat.st_size == 7);
  assert (test.saw_entry);
  assert (!test.list.entries);
  assert (!test.list.resource.owner);
  assert (access (renamed, F_OK) < 0);
  assert (cuv_loop_close (&loop) == 0);

  assert (rmdir (directory) == 0);
  assert (rmdir (root) == 0);
}

struct dirlist_cleanup_test
{
  struct cuv_task task;
  struct cuv_dirlist list;
  char const *directory;
  char const *missing;
  uv_stat_t stat;
};

static void
dirlist_cleanup_test_run (struct cuv_task *task)
    async$ ((struct dirlist_cleanup_test *)task)
{
  await$ (cuv_fs_scandir, &$.list, $.directory, 0);
  await$ (cuv_fs_stat, &$.stat, $.missing);
  exit$ ();
}

static void
test_dirlist_cleanup (void)
{
  char root[] = "/tmp/libcuv-dirlist-cleanup-XXXXXX";
  assert (mkdtemp (root));

  char file[512];
  char missing[512];
  assert (snprintf (file, sizeof (file), "%s/entry", root)
          < (int)sizeof (file));
  assert (snprintf (missing, sizeof (missing), "%s/missing", root)
          < (int)sizeof (missing));

  int fd = open (file, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  assert (fd >= 0);
  assert (write (fd, "x", 1) == 1);
  close (fd);

  struct cuv_loop loop;
  struct dirlist_cleanup_test test = {
    .directory = root,
    .missing = missing,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, dirlist_cleanup_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == UV_ENOENT);
  assert (!test.list.entries);
  assert (!test.list.count);
  assert (!test.list.resource.owner);
  assert (cuv_loop_close (&loop) == 0);

  assert (unlink (file) == 0);
  assert (rmdir (root) == 0);
}

struct utility_adapter_test
{
  struct cuv_task task;
  struct sockaddr_in address;
  unsigned char random[32];
  char host[128];
  char service[32];
};

static void
utility_adapter_test_run (struct cuv_task *task)
    async$ ((struct utility_adapter_test *)task)
{
  await$ (cuv_random, $.random, sizeof ($.random), 0);
  await$ (cuv_getnameinfo, $.host, sizeof ($.host), $.service,
          sizeof ($.service), (struct sockaddr const *)&$.address,
          NI_NUMERICHOST | NI_NUMERICSERV);
  exit$ ();
}

static void
test_utility_adapters (void)
{
  struct cuv_loop loop;
  struct utility_adapter_test test = { 0 };
  test.address.sin_family = AF_INET;
  test.address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
  test.address.sin_port = htons (8080);

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, utility_adapter_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (!strcmp (test.host, "127.0.0.1"));
  assert (!strcmp (test.service, "8080"));
  assert (cuv_loop_close (&loop) == 0);
}

struct udp_connect_test
{
  struct cuv_task task;
  struct cuv_udp udp;
  struct sockaddr_storage address;
  int connected;
  int disconnected;
};

static void
udp_connect_test_run (struct cuv_task *task)
    async$ ((struct udp_connect_test *)task)
{
  await$ (cuv_udp_connect, &$.udp, (struct sockaddr const *)&$.address);
  $.connected = $.udp.connected;
  await$ (cuv_udp_send, &$.udp, "connected", 9, null);
  await$ (cuv_udp_connect, &$.udp, null);
  $.disconnected = !$.udp.connected;
  await$ (cuv_udp_send, &$.udp, "unconnected", 11,
          (struct sockaddr const *)&$.address);
  await$ (cuv_udp_close, &$.udp);
  exit$ ();
}

static void
test_udp_connect (void)
{
  struct sockaddr_in address;
  int receiver = make_udp_address (&address);

  struct cuv_loop loop;
  struct udp_connect_test test = { 0 };
  memcpy (&test.address, &address, sizeof (address));

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, udp_connect_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (test.connected);
  assert (test.disconnected);
  assert (test.udp.state == CUV_UDP_CLOSED);
  assert (cuv_loop_close (&loop) == 0);

  char first[32] = { 0 };
  char second[32] = { 0 };
  assert (recv (receiver, first, sizeof (first), 0) == 9);
  assert (recv (receiver, second, sizeof (second), 0) == 11);
  assert (!memcmp (first, "connected", 9));
  assert (!memcmp (second, "unconnected", 11));
  close (receiver);
}

struct watcher_mutator
{
  struct cuv_task task;
  struct cuv_file file;
  char const *path;
  char const *data;
  u64 delay;
  isz size;
};

static void
watcher_mutator_run (struct cuv_task *task)
    async$ ((struct watcher_mutator *)task)
{
  await$ (cuv_sleep, $.delay);
  await$ (cuv_fs_open, &$.file, $.path, O_WRONLY | O_APPEND, 0);
  await$ (cuv_fs_write, &$.size, &$.file, $.data, strlen ($.data), -1);
  await$ (cuv_fs_close, &$.file);
  exit$ ();
}

struct watcher_path_probe
{
  struct cuv_task task;
  struct cuv_fs_event *event;
  struct cuv_fs_poll *poll;
  char path[512];
  usz size;
  int result;
};

static void
watcher_path_probe_run (struct cuv_task *task)
    async$ ((struct watcher_path_probe *)task)
{
  await$ (cuv_sleep, 5);
  $.size = sizeof ($.path);
  if ($.event)
    $.result = cuv_fs_event_getpath (task, $.event, $.path, &$.size);
  else
    $.result = cuv_fs_poll_getpath (task, $.poll, $.path, &$.size);
  exit$ ();
}

struct fs_event_test
{
  struct cuv_task task;
  struct cuv_fs_event watcher;
  char const *path;
  char filename[256];
  int events;
  int reached;
};

static void
fs_event_test_run (struct cuv_task *task) async$ ((struct fs_event_test *)task)
{
  await$ (1000, cuv_fs_event_wait, &$.watcher, $.filename, sizeof ($.filename),
          &$.events, $.path, 0);
  $.reached = 1;
  await$ (cuv_fs_event_close, &$.watcher);
  exit$ ();
}

static void
test_fs_event (void)
{
  char path[] = "/tmp/libcuv-fs-event-XXXXXX";
  int fd = mkstemp (path);
  assert (fd >= 0);
  assert (write (fd, "a", 1) == 1);
  close (fd);

  struct cuv_loop loop;
  struct fs_event_test watcher = {
    .path = path,
  };
  struct watcher_mutator mutator = {
    .path = path,
    .data = "b",
    .delay = 30,
  };
  struct watcher_path_probe probe = {
    .event = &watcher.watcher,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&watcher.task, &loop, fs_event_test_run);
  cuv_task_init (&mutator.task, &loop, watcher_mutator_run);
  cuv_task_init (&probe.task, &loop, watcher_path_probe_run);
  cuv_task_start (&watcher.task);
  cuv_task_start (&probe.task);
  cuv_task_start (&mutator.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&watcher.task) == 0);
  assert (cuv_task_error (&mutator.task) == 0);
  assert (cuv_task_error (&probe.task) == 0);
  assert (probe.result == CUV_READY);
  assert (strcmp (probe.path, path) == 0);
  assert (watcher.reached);
  assert (watcher.events & (UV_CHANGE | UV_RENAME));
  assert (watcher.watcher.state == CUV_FS_EVENT_CLOSED);
  assert (!watcher.watcher.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
  assert (unlink (path) == 0);
}

struct fs_event_timeout_test
{
  struct cuv_task task;
  struct cuv_fs_event watcher;
  char const *path;
  int events;
  int reached;
};

static void
fs_event_timeout_test_run (struct cuv_task *task)
    async$ ((struct fs_event_timeout_test *)task)
{
  await$ (20, cuv_fs_event_wait, &$.watcher, null, 0, &$.events, $.path, 0);
  $.reached = 1;
  exit$ ();
}

static void
test_fs_event_timeout (void)
{
  char path[] = "/tmp/libcuv-fs-event-timeout-XXXXXX";
  int fd = mkstemp (path);
  assert (fd >= 0);
  close (fd);

  struct cuv_loop loop;
  struct fs_event_timeout_test test = {
    .path = path,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, fs_event_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.watcher.state == CUV_FS_EVENT_CLOSED);
  assert (!test.watcher.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
  assert (unlink (path) == 0);
}

struct fs_poll_test
{
  struct cuv_task task;
  struct cuv_fs_poll watcher;
  char const *path;
  uv_stat_t previous;
  uv_stat_t current;
  int status;
  int reached;
};

static void
fs_poll_test_run (struct cuv_task *task) async$ ((struct fs_poll_test *)task)
{
  await$ (1500, cuv_fs_poll_wait, &$.watcher, &$.status, &$.previous,
          &$.current, $.path, 10);
  $.reached = 1;
  exit$ ();
}

static void
test_fs_poll (void)
{
  char path[] = "/tmp/libcuv-fs-poll-XXXXXX";
  int fd = mkstemp (path);
  assert (fd >= 0);
  assert (write (fd, "a", 1) == 1);
  close (fd);

  struct cuv_loop loop;
  struct fs_poll_test watcher = {
    .path = path,
  };
  struct watcher_mutator mutator = {
    .path = path,
    .data = "more",
    .delay = 50,
  };
  struct watcher_path_probe probe = {
    .poll = &watcher.watcher,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&watcher.task, &loop, fs_poll_test_run);
  cuv_task_init (&mutator.task, &loop, watcher_mutator_run);
  cuv_task_init (&probe.task, &loop, watcher_path_probe_run);
  cuv_task_start (&watcher.task);
  cuv_task_start (&probe.task);
  cuv_task_start (&mutator.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&watcher.task) == 0);
  assert (cuv_task_error (&mutator.task) == 0);
  assert (cuv_task_error (&probe.task) == 0);
  assert (probe.result == CUV_READY);
  assert (strcmp (probe.path, path) == 0);
  assert (watcher.reached);
  assert (watcher.status == 0);
  assert (watcher.current.st_size > watcher.previous.st_size);
  assert (watcher.watcher.state == CUV_FS_POLL_CLOSED);
  assert (!watcher.watcher.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
  assert (unlink (path) == 0);
}

struct fs_poll_missing_test
{
  struct cuv_task task;
  struct cuv_fs_poll watcher;
  char const *path;
  uv_stat_t previous;
  uv_stat_t current;
  int status;
  int reached;
};

static void
fs_poll_missing_test_run (struct cuv_task *task)
    async$ ((struct fs_poll_missing_test *)task)
{
  await$ (500, cuv_fs_poll_wait, &$.watcher, &$.status, &$.previous,
          &$.current, $.path, 10);
  $.reached = 1;
  exit$ ();
}

static void
test_fs_poll_missing (void)
{
  char root[] = "/tmp/libcuv-fs-poll-missing-XXXXXX";
  assert (mkdtemp (root));

  char path[512];
  assert (snprintf (path, sizeof (path), "%s/missing", root)
          < (int)sizeof (path));

  struct cuv_loop loop;
  struct fs_poll_missing_test test = {
    .path = path,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, fs_poll_missing_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (test.reached);
  assert (test.status == UV_ENOENT);
  assert (test.watcher.state == CUV_FS_POLL_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
  assert (rmdir (root) == 0);
}

struct fs_poll_timeout_test
{
  struct cuv_task task;
  struct cuv_fs_poll watcher;
  char const *path;
  uv_stat_t previous;
  uv_stat_t current;
  int status;
  int reached;
};

static void
fs_poll_timeout_test_run (struct cuv_task *task)
    async$ ((struct fs_poll_timeout_test *)task)
{
  await$ (20, cuv_fs_poll_wait, &$.watcher, &$.status, &$.previous, &$.current,
          $.path, 100);
  $.reached = 1;
  exit$ ();
}

static void
test_fs_poll_timeout (void)
{
  char path[] = "/tmp/libcuv-fs-poll-timeout-XXXXXX";
  int fd = mkstemp (path);
  assert (fd >= 0);
  close (fd);

  struct cuv_loop loop;
  struct fs_poll_timeout_test test = {
    .path = path,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, fs_poll_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.watcher.state == CUV_FS_POLL_CLOSED);
  assert (!test.watcher.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
  assert (unlink (path) == 0);
}

/* Phase H lifecycle/cancellation hardening tests. */

struct cancel_running_test
{
  struct cuv_task task;
  int result;
};

static void
cancel_running_test_run (struct cuv_task *task)
    async$ ((struct cancel_running_test *)task)
{
  $.result = cuv_task_cancel (task);
  exit$ ();
}

struct cancel_wait_target
{
  struct cuv_task task;
  u64 delay;
  int reached;
};

static void
cancel_wait_target_run (struct cuv_task *task)
    async$ ((struct cancel_wait_target *)task)
{
  await$ (cuv_sleep, $.delay);
  $.reached = 1;
  exit$ ();
}

struct cancel_wait_driver
{
  struct cuv_task task;
  struct cuv_task *target;
  u64 delay;
  int first;
  int second;
};

static void
cancel_wait_driver_run (struct cuv_task *task)
    async$ ((struct cancel_wait_driver *)task)
{
  await$ (cuv_sleep, $.delay);
  $.first = cuv_task_cancel ($.target);
  $.second = cuv_task_cancel ($.target);
  exit$ ();
}

struct cancel_closing_target
{
  struct cuv_task task;
  struct cuv_process process;
};

static void
cancel_closing_target_run (struct cuv_task *task)
    async$ ((struct cancel_closing_target *)task)
{
  char *args[] = { "/bin/sh", "-c", "sleep 0.05", null };
  uv_process_options_t options = {
    .file = args[0],
    .args = args,
  };

  await$ (cuv_process_spawn, &$.process, &options);
  exit$ ();
}

static void
test_task_cancel_states (void)
{
  struct cuv_loop loop;
  struct cancel_wait_target idle = { .delay = 1 };
  struct cancel_wait_target ready = { .delay = 1 };
  struct cancel_running_test running = { 0 };
  struct cancel_wait_target waiting = { .delay = 100 };
  struct cancel_wait_driver waiting_driver = {
    .target = &waiting.task,
    .delay = 5,
  };
  struct cancel_closing_target closing = { 0 };
  struct cancel_wait_driver closing_driver = {
    .target = &closing.task,
    .delay = 5,
  };

  assert (cuv_loop_init (&loop) == 0);

  cuv_task_init (&idle.task, &loop, cancel_wait_target_run);
  assert (cuv_task_cancel (&idle.task) == UV_EINVAL);
  assert (idle.task.state == CUV_TASK_IDLE);

  cuv_task_init (&ready.task, &loop, cancel_wait_target_run);
  cuv_task_start (&ready.task);
  assert (cuv_task_cancel (&ready.task) == 0);
  assert (cuv_task_cancel (&ready.task) == UV_EALREADY);
  assert (ready.task.state == CUV_TASK_DONE);
  assert (cuv_task_error (&ready.task) == UV_ECANCELED);

  cuv_task_init (&running.task, &loop, cancel_running_test_run);
  cuv_task_start (&running.task);

  cuv_task_init (&waiting.task, &loop, cancel_wait_target_run);
  cuv_task_init (&waiting_driver.task, &loop, cancel_wait_driver_run);
  cuv_task_start (&waiting.task);
  cuv_task_start (&waiting_driver.task);

  cuv_task_init (&closing.task, &loop, cancel_closing_target_run);
  cuv_task_init (&closing_driver.task, &loop, cancel_wait_driver_run);
  cuv_task_start (&closing.task);
  cuv_task_start (&closing_driver.task);

  assert (cuv_loop_run (&loop) == 0);

  assert (running.result == UV_EBUSY);
  assert (cuv_task_error (&running.task) == 0);

  assert (waiting_driver.first == 0);
  assert (waiting_driver.second == UV_EALREADY);
  assert (!waiting.reached);
  assert (waiting.task.state == CUV_TASK_DONE);
  assert (cuv_task_error (&waiting.task) == UV_ECANCELED);

  assert (closing_driver.first == UV_EALREADY);
  assert (closing_driver.second == UV_EALREADY);
  assert (closing.task.state == CUV_TASK_DONE);
  assert (cuv_task_error (&closing.task) == 0);
  assert (closing.process.state == CUV_PROCESS_CLOSED);

  assert (cuv_task_cancel (&running.task) == UV_EALREADY);
  assert (cuv_task_cancel (&waiting.task) == UV_EALREADY);
  assert (cuv_task_cancel (&closing.task) == UV_EALREADY);
  assert (cuv_loop_close (&loop) == 0);
}

struct cancel_timed_target
{
  struct cuv_task task;
  u64 timeout;
  u64 sleep;
  int reached;
};

static void
cancel_timed_target_run (struct cuv_task *task)
    async$ ((struct cancel_timed_target *)task)
{
  await$ ($.timeout, cuv_sleep, $.sleep);
  $.reached = 1;
  exit$ ();
}

static void
test_cancel_timeout_precedence (void)
{
  {
    struct cuv_loop loop;
    struct cancel_timed_target target = {
      .timeout = 100,
      .sleep = 500,
    };
    struct cancel_wait_driver driver = {
      .target = &target.task,
      .delay = 5,
    };

    assert (cuv_loop_init (&loop) == 0);
    cuv_task_init (&target.task, &loop, cancel_timed_target_run);
    cuv_task_init (&driver.task, &loop, cancel_wait_driver_run);
    cuv_task_start (&target.task);
    cuv_task_start (&driver.task);
    assert (cuv_loop_run (&loop) == 0);

    assert (driver.first == 0);
    assert (driver.second == UV_EALREADY);
    assert (!target.reached);
    assert (cuv_task_error (&target.task) == UV_ECANCELED);
    assert (cuv_loop_close (&loop) == 0);
  }

  {
    struct cuv_loop loop;
    struct cancel_timed_target target = {
      .timeout = 5,
      .sleep = 500,
    };
    struct cancel_wait_driver driver = {
      .target = &target.task,
      .delay = 30,
    };

    assert (cuv_loop_init (&loop) == 0);
    cuv_task_init (&target.task, &loop, cancel_timed_target_run);
    cuv_task_init (&driver.task, &loop, cancel_wait_driver_run);
    cuv_task_start (&target.task);
    cuv_task_start (&driver.task);
    assert (cuv_loop_run (&loop) == 0);

    assert (driver.first == UV_EALREADY);
    assert (driver.second == UV_EALREADY);
    assert (!target.reached);
    assert (cuv_task_error (&target.task) == UV_ETIMEDOUT);
    assert (cuv_loop_close (&loop) == 0);
  }
}

struct abort_work_target
{
  struct cuv_task task;
  u64 timeout;
  int worker_started;
  int worker_finished;
  int reached;
};

static void
abort_work_function (void *opaque)
{
  struct abort_work_target *target = opaque;
  target->worker_started = 1;
  usleep (60000);
  target->worker_finished = 1;
}

static void
abort_work_target_run (struct cuv_task *task)
    async$ ((struct abort_work_target *)task)
{
  await$ ($.timeout, cuv_work, abort_work_function, &$);
  $.reached = 1;
  exit$ ();
}

static void
test_cancel_timeout_running_work (void)
{
  {
    struct cuv_loop loop;
    struct abort_work_target target = { .timeout = 1000 };
    struct cancel_wait_driver driver = {
      .target = &target.task,
      .delay = 10,
    };

    assert (cuv_loop_init (&loop) == 0);
    cuv_task_init (&target.task, &loop, abort_work_target_run);
    cuv_task_init (&driver.task, &loop, cancel_wait_driver_run);
    cuv_task_start (&target.task);
    cuv_task_start (&driver.task);
    assert (cuv_loop_run (&loop) == 0);

    assert (target.worker_started);
    assert (target.worker_finished);
    assert (!target.reached);
    assert (driver.first == 0);
    assert (driver.second == UV_EALREADY);
    assert (cuv_task_error (&target.task) == UV_ECANCELED);
    assert (cuv_loop_close (&loop) == 0);
  }

  {
    struct cuv_loop loop;
    struct abort_work_target target = { .timeout = 5 };
    struct cancel_wait_driver driver = {
      .target = &target.task,
      .delay = 15,
    };

    assert (cuv_loop_init (&loop) == 0);
    cuv_task_init (&target.task, &loop, abort_work_target_run);
    cuv_task_init (&driver.task, &loop, cancel_wait_driver_run);
    cuv_task_start (&target.task);
    cuv_task_start (&driver.task);
    assert (cuv_loop_run (&loop) == 0);

    assert (target.worker_started);
    assert (target.worker_finished);
    assert (!target.reached);
    assert (driver.first == UV_EALREADY);
    assert (driver.second == UV_EALREADY);
    assert (cuv_task_error (&target.task) == UV_ETIMEDOUT);
    assert (cuv_loop_close (&loop) == 0);
  }
}

struct hard_work_child
{
  struct cuv_task task;
  int worker_started;
  int worker_finished;
  int reached;
};

static void
hard_work_function (void *opaque)
{
  struct hard_work_child *child = opaque;
  child->worker_started = 1;
  usleep (100000);
  child->worker_finished = 1;
}

static void
hard_work_child_run (struct cuv_task *task)
    async$ ((struct hard_work_child *)task)
{
  await$ (cuv_work, hard_work_function, &$);
  $.reached = 1;
  exit$ ();
}

struct hard_race_parent
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct hard_work_child *work;
  struct group_child_test *fast;
  struct cuv_task *winner;
  int done;
};

static void
hard_race_parent_run (struct cuv_task *task)
    async$ ((struct hard_race_parent *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);
  assert (cuv_task_group_start (&$.group, &$.work->task) == 0);

  /* Give the threadpool work enough time to become non-cancellable. */
  await$ (cuv_sleep, 15);

  assert (cuv_task_group_start (&$.group, &$.fast->task) == 0);
  await$ (cuv_task_group_race, &$.group, &$.winner);
  $.done = 1;
  exit$ ();
}

static void
test_group_race_running_work (void)
{
  struct cuv_loop loop;
  struct hard_work_child work = { 0 };
  struct group_child_test fast = { .delay = 1, .value = 9 };
  struct hard_race_parent parent = {
    .work = &work,
    .fast = &fast,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&work.task, &loop, hard_work_child_run);
  cuv_task_init (&fast.task, &loop, group_child_test_run);
  cuv_task_init (&parent.task, &loop, hard_race_parent_run);
  cuv_task_start (&parent.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (parent.done);
  assert (parent.winner == &fast.task);
  assert (fast.reached == 9);
  assert (work.worker_started);
  assert (work.worker_finished);
  assert (!work.reached);
  assert (cuv_task_error (&work.task) == UV_ECANCELED);
  assert (cuv_task_error (&parent.task) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

struct nested_group_child
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct group_child_test *first;
  struct group_child_test *second;
  int done;
};

static void
nested_group_child_run (struct cuv_task *task)
    async$ ((struct nested_group_child *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);
  assert (cuv_task_group_start (&$.group, &$.first->task) == 0);
  assert (cuv_task_group_start (&$.group, &$.second->task) == 0);
  await$ (cuv_task_group_wait, &$.group);
  $.done = 1;
  exit$ ();
}

struct nested_race_parent
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct nested_group_child *nested;
  struct group_child_test *fast;
  struct cuv_task *winner;
  int done;
};

static void
nested_race_parent_run (struct cuv_task *task)
    async$ ((struct nested_race_parent *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);
  assert (cuv_task_group_start (&$.group, &$.nested->task) == 0);
  assert (cuv_task_group_start (&$.group, &$.fast->task) == 0);
  await$ (cuv_task_group_race, &$.group, &$.winner);
  $.done = 1;
  exit$ ();
}

static void
test_nested_group_cancellation (void)
{
  struct cuv_loop loop;
  struct group_child_test grand_first = { .delay = 100, .value = 1 };
  struct group_child_test grand_second = { .delay = 100, .value = 2 };
  struct nested_group_child nested = {
    .first = &grand_first,
    .second = &grand_second,
  };
  struct group_child_test fast = { .delay = 5, .value = 3 };
  struct nested_race_parent parent = {
    .nested = &nested,
    .fast = &fast,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&grand_first.task, &loop, group_child_test_run);
  cuv_task_init (&grand_second.task, &loop, group_child_test_run);
  cuv_task_init (&nested.task, &loop, nested_group_child_run);
  cuv_task_init (&fast.task, &loop, group_child_test_run);
  cuv_task_init (&parent.task, &loop, nested_race_parent_run);
  cuv_task_start (&parent.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (parent.done);
  assert (parent.winner == &fast.task);
  assert (!nested.done);
  assert (!grand_first.reached);
  assert (!grand_second.reached);
  assert (cuv_task_error (&nested.task) == UV_ECANCELED);
  assert (cuv_task_error (&grand_first.task) == UV_ECANCELED);
  assert (cuv_task_error (&grand_second.task) == UV_ECANCELED);
  assert (!nested.group.resource.owner);
  assert (!parent.group.resource.owner);
  assert (cuv_task_error (&parent.task) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

struct partial_group_cancel_parent
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct group_child_test *fast;
  struct group_child_test *slow;
  int cancel_error;
  int reached;
};

static void
partial_group_cancel_parent_run (struct cuv_task *task)
    async$ ((struct partial_group_cancel_parent *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);
  assert (cuv_task_group_start (&$.group, &$.fast->task) == 0);
  assert (cuv_task_group_start (&$.group, &$.slow->task) == 0);

  await$ (cuv_sleep, 10);
  $.cancel_error = cuv_task_group_cancel (&$.group);
  await$ (cuv_task_group_wait, &$.group);
  $.reached = 1;
  exit$ ();
}

static void
test_group_cancel_after_completion (void)
{
  struct cuv_loop loop;
  struct group_child_test fast = { .delay = 1, .value = 1 };
  struct group_child_test slow = { .delay = 100, .value = 2 };
  struct partial_group_cancel_parent parent = {
    .fast = &fast,
    .slow = &slow,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&fast.task, &loop, group_child_test_run);
  cuv_task_init (&slow.task, &loop, group_child_test_run);
  cuv_task_init (&parent.task, &loop, partial_group_cancel_parent_run);
  cuv_task_start (&parent.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (parent.cancel_error == 0);
  assert (!parent.reached);
  assert (fast.reached == 1);
  assert (!slow.reached);
  assert (cuv_task_error (&slow.task) == UV_ECANCELED);
  assert (cuv_task_error (&parent.task) == UV_ECANCELED);
  assert (cuv_loop_close (&loop) == 0);
}

struct process_cancel_target
{
  struct cuv_task task;
  struct cuv_process process;
  int reached;
};

static void
process_cancel_target_run (struct cuv_task *task)
    async$ ((struct process_cancel_target *)task)
{
  char *args[] = { "/bin/sh", "-c", "sleep 0.05", null };
  uv_process_options_t options = {
    .file = args[0],
    .args = args,
  };

  await$ (cuv_process_spawn, &$.process, &options);
  await$ (cuv_process_wait, &$.process, null, null);
  $.reached = 1;
  exit$ ();
}

static void
test_process_task_cancel (void)
{
  struct cuv_loop loop;
  struct process_cancel_target target = { 0 };
  struct cancel_wait_driver driver = {
    .target = &target.task,
    .delay = 5,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&target.task, &loop, process_cancel_target_run);
  cuv_task_init (&driver.task, &loop, cancel_wait_driver_run);
  cuv_task_start (&target.task);
  cuv_task_start (&driver.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (driver.first == 0);
  assert (driver.second == UV_EALREADY);
  assert (!target.reached);
  assert (cuv_task_error (&target.task) == UV_ECANCELED);
  assert (target.process.state == CUV_PROCESS_CLOSED);
  assert (!target.process.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

struct failing_resource
{
  struct cuv_resource resource;
  int error;
  int closes;
};

static int
failing_resource_close (struct cuv_resource *resource)
{
  auto failing = container_of (resource, struct failing_resource, resource);

  ++failing->closes;
  return failing->error;
}

struct cleanup_error_test
{
  struct cuv_task task;
  struct failing_resource first;
  struct failing_resource second;
  int primary_error;
};

static void
cleanup_error_test_run (struct cuv_task *task)
    async$ ((struct cleanup_error_test *)task)
{
  $.first.resource.close = failing_resource_close;
  $.second.resource.close = failing_resource_close;
  cuv_resource_transfer (&$.first.resource, task);
  cuv_resource_transfer (&$.second.resource, task);

  if ($.primary_error < 0)
    await$ (test_error_operation, $.primary_error);

  exit$ ();
}

static void
test_cleanup_error_precedence (void)
{
  {
    struct cuv_loop loop;
    struct cleanup_error_test test = {
      .first.error = UV_EIO,
      .second.error = UV_ENOSPC,
    };

    assert (cuv_loop_init (&loop) == 0);
    cuv_task_init (&test.task, &loop, cleanup_error_test_run);
    cuv_task_start (&test.task);
    assert (cuv_loop_run (&loop) == 0);

    assert (test.first.closes == 1);
    assert (test.second.closes == 1);
    assert (cuv_task_error (&test.task) == UV_EIO);
    assert (cuv_loop_close (&loop) == 0);
  }

  {
    struct cuv_loop loop;
    struct cleanup_error_test test = {
      .first.error = UV_EIO,
      .second.error = UV_ENOSPC,
      .primary_error = UV_EINVAL,
    };

    assert (cuv_loop_init (&loop) == 0);
    cuv_task_init (&test.task, &loop, cleanup_error_test_run);
    cuv_task_start (&test.task);
    assert (cuv_loop_run (&loop) == 0);

    assert (test.first.closes == 1);
    assert (test.second.closes == 1);
    assert (cuv_task_error (&test.task) == UV_EINVAL);
    assert (cuv_loop_close (&loop) == 0);
  }
}

struct poll_fd_test
{
  struct cuv_task task;
  struct cuv_poll poll;
  int fd;
  int events;
  int reached;
};

static void
poll_fd_test_run (struct cuv_task *task) async$ ((struct poll_fd_test *)task)
{
  await$ (cuv_poll_init, &$.poll, $.fd);
  await$ (cuv_poll_wait, &$.poll, &$.events, UV_READABLE);
  $.reached = 1;
  await$ (cuv_poll_close, &$.poll);
  exit$ ();
}

static void
test_poll_fd (void)
{
  int fds[2];
  assert (pipe (fds) == 0);
  assert (write (fds[1], "p", 1) == 1);

  struct cuv_loop loop;
  struct poll_fd_test test = {
    .fd = fds[0],
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, poll_fd_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (test.reached == 1);
  assert (test.events & UV_READABLE);
  assert (test.poll.state == CUV_POLL_CLOSED);
  assert (!test.poll.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
  close (fds[0]);
  close (fds[1]);
}

struct poll_socket_test
{
  struct cuv_task task;
  struct cuv_poll poll;
  uv_os_sock_t socket;
  int events;
  int reached;
};

static void
poll_socket_test_run (struct cuv_task *task)
    async$ ((struct poll_socket_test *)task)
{
  await$ (cuv_poll_init_socket, &$.poll, $.socket);
  await$ (cuv_poll_wait, &$.poll, &$.events, UV_READABLE);
  $.reached = 1;
  await$ (cuv_poll_close, &$.poll);
  exit$ ();
}

static void
test_poll_socket (void)
{
  int sockets[2];
  assert (socketpair (AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  assert (write (sockets[1], "s", 1) == 1);

  struct cuv_loop loop;
  struct poll_socket_test test = {
    .socket = sockets[0],
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, poll_socket_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (test.reached == 1);
  assert (test.events & UV_READABLE);
  assert (test.poll.state == CUV_POLL_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
  close (sockets[0]);
  close (sockets[1]);
}

struct poll_timeout_test
{
  struct cuv_task task;
  struct cuv_poll poll;
  int fd;
  int events;
  int reached;
};

static void
poll_timeout_test_run (struct cuv_task *task)
    async$ ((struct poll_timeout_test *)task)
{
  await$ (cuv_poll_init, &$.poll, $.fd);
  await$ (10, cuv_poll_wait, &$.poll, &$.events, UV_READABLE);
  $.reached = 1;
  exit$ ();
}

static void
test_poll_timeout (void)
{
  int fds[2];
  assert (pipe (fds) == 0);

  struct cuv_loop loop;
  struct poll_timeout_test test = {
    .fd = fds[0],
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, poll_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.poll.state == CUV_POLL_CLOSED);
  assert (!test.poll.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
  close (fds[0]);
  close (fds[1]);
}

struct tty_test
{
  struct cuv_task task;
  struct cuv_tty tty;
  int master;
  int slave;
  char input[32];
  isz size;
  int width;
  int height;
  isz injected;
  isz try_size;
  usz queue_size;
  int readable;
  int writable;
};

static void
tty_test_run (struct cuv_task *task) async$ ((struct tty_test *)task)
{
  await$ (cuv_tty_open, &$.tty, $.slave);
  await$ (cuv_tty_set_mode, &$.tty, UV_TTY_MODE_RAW);
  await$ (cuv_tty_get_winsize, &$.tty, &$.width, &$.height);
  await$ (cuv_tty_is_readable, &$.tty, &$.readable);
  await$ (cuv_tty_is_writable, &$.tty, &$.writable);
  await$ (cuv_tty_write_queue, &$.tty, &$.queue_size);
  await$ (cuv_tty_set_blocking, &$.tty, 0);
  $.injected = write ($.master, "tty input", 9);
  await$ (cuv_tty_read, &$.size, &$.tty, $.input, sizeof ($.input));
  await$ (cuv_tty_try_write, &$.try_size, &$.tty, "try ", 4);
  await$ (cuv_tty_write, &$.tty, "tty output", 10);
  await$ (cuv_tty_set_mode, &$.tty, UV_TTY_MODE_NORMAL);
  await$ (cuv_tty_close, &$.tty);
  exit$ ();
}

static int
open_test_pty (int *master, int *slave)
{
  *master = posix_openpt (O_RDWR | O_NOCTTY);
  if (*master < 0)
    return -1;

  if (grantpt (*master) < 0 || unlockpt (*master) < 0)
    {
      close (*master);
      return -1;
    }

  char *name = ptsname (*master);
  if (!name)
    {
      close (*master);
      return -1;
    }

  *slave = open (name, O_RDWR | O_NOCTTY);
  if (*slave < 0)
    {
      close (*master);
      return -1;
    }

  return 0;
}

static void
test_tty (void)
{
  int master;
  int slave;
  assert (open_test_pty (&master, &slave) == 0);

  struct cuv_loop loop;
  struct tty_test test = {
    .master = master,
    .slave = slave,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, tty_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (test.injected == 9);
  assert (test.size == 9);
  assert (!memcmp (test.input, "tty input", 9));
  assert (test.try_size == 4);
  assert (test.queue_size == 0);
  assert (test.readable);
  assert (test.writable);
  assert (test.tty.state == CUV_TTY_CLOSED);
  assert (!test.tty.resource.owner);
  assert (cuv_task_error (&test.task) == 0);

  char output[32] = { 0 };
  assert (read (master, output, sizeof (output)) == 14);
  assert (!memcmp (output, "try tty output", 14));
  assert (cuv_tty_reset_mode () == 0);
  assert (cuv_loop_close (&loop) == 0);
  close (master);
  close (slave);
}

static void
test_tty_vterm (void)
{
  uv_tty_vtermstate_t state;
  state = UV_TTY_UNSUPPORTED;
  int error = cuv_tty_get_vterm_state (&state);
  assert (error == CUV_READY || error == UV_ENOTSUP);

  if (error == CUV_READY)
    {
      uv_tty_vtermstate_t original = state;
      assert (cuv_tty_set_vterm_state (UV_TTY_SUPPORTED) == CUV_READY);
      assert (cuv_tty_set_vterm_state (original) == CUV_READY);
    }
  else
    assert (cuv_tty_set_vterm_state (UV_TTY_SUPPORTED) == CUV_READY);

  assert (cuv_tty_get_vterm_state (null) == UV_EINVAL);
  assert (cuv_tty_set_vterm_state ((uv_tty_vtermstate_t)-1) == UV_EINVAL);
}

struct tty_timeout_test
{
  struct cuv_task task;
  struct cuv_tty tty;
  int slave;
  char buffer[16];
  isz size;
  int reached;
};

static void
tty_timeout_test_run (struct cuv_task *task)
    async$ ((struct tty_timeout_test *)task)
{
  await$ (cuv_tty_open, &$.tty, $.slave);
  await$ (cuv_tty_set_mode, &$.tty, UV_TTY_MODE_RAW);
  await$ (10, cuv_tty_read, &$.size, &$.tty, $.buffer, sizeof ($.buffer));
  $.reached = 1;
  exit$ ();
}

static void
test_tty_timeout (void)
{
  int master;
  int slave;
  assert (open_test_pty (&master, &slave) == 0);

  struct cuv_loop loop;
  struct tty_timeout_test test = {
    .slave = slave,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, tty_timeout_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.tty.state == CUV_TTY_CLOSED);
  assert (!test.tty.resource.owner);
  assert (cuv_tty_reset_mode () == 0);
  assert (cuv_loop_close (&loop) == 0);
  close (master);
  close (slave);
}

struct tcp_options_test
{
  struct cuv_task task;
  struct cuv_tcp tcp;
  struct sockaddr_in address;
};

static void
tcp_options_test_run (struct cuv_task *task)
    async$ ((struct tcp_options_test *)task)
{
  await$ (cuv_tcp_bind, &$.tcp, (struct sockaddr const *)&$.address, 0);
  await$ (cuv_tcp_nodelay, &$.tcp, 1);
  await$ (cuv_tcp_keepalive, &$.tcp, 1, 1);
  await$ (cuv_tcp_keepalive_ex, &$.tcp, 1, 1, 1, 2);
  await$ (cuv_tcp_simultaneous_accepts, &$.tcp, 1);
  await$ (cuv_tcp_close, &$.tcp);
  exit$ ();
}

static void
test_tcp_options (void)
{
  struct cuv_loop loop;
  struct tcp_options_test test = { 0 };
  assert (uv_ip4_addr ("127.0.0.1", 0, &test.address) == 0);

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, tcp_options_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (test.tcp.state == CUV_TCP_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
}

struct udp_options_test
{
  struct cuv_task task;
  struct cuv_udp udp;
  struct sockaddr_in address;
  int join_error;
  int leave_error;
  int source_join_error;
  int source_leave_error;
};

static void
udp_options_test_run (struct cuv_task *task)
    async$ ((struct udp_options_test *)task)
{
  await$ (cuv_udp_bind, &$.udp, (struct sockaddr const *)&$.address, 0);
  await$ (cuv_udp_set_broadcast, &$.udp, 1);
  await$ (cuv_udp_set_ttl, &$.udp, 64);
  await$ (cuv_udp_set_multicast_loop, &$.udp, 1);
  await$ (cuv_udp_set_multicast_ttl, &$.udp, 8);
  await$ (cuv_udp_set_multicast_interface, &$.udp, "127.0.0.1");

  $.join_error = cuv_udp_set_membership (task, &$.udp, "239.255.0.1",
                                         "127.0.0.1", UV_JOIN_GROUP);
  if ($.join_error == CUV_READY)
    $.leave_error = cuv_udp_set_membership (task, &$.udp, "239.255.0.1",
                                            "127.0.0.1", UV_LEAVE_GROUP);

  $.source_join_error = cuv_udp_set_source_membership (
      task, &$.udp, "232.255.0.1", "127.0.0.1", "127.0.0.1", UV_JOIN_GROUP);
  if ($.source_join_error == CUV_READY)
    $.source_leave_error = cuv_udp_set_source_membership (
        task, &$.udp, "232.255.0.1", "127.0.0.1", "127.0.0.1", UV_LEAVE_GROUP);

  await$ (cuv_udp_close, &$.udp);
  exit$ ();
}

static void
test_udp_options (void)
{
  struct cuv_loop loop;
  struct udp_options_test test = { 0 };
  assert (uv_ip4_addr ("0.0.0.0", 0, &test.address) == 0);

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, udp_options_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (test.join_error == CUV_READY || test.join_error == UV_ENODEV
          || test.join_error == UV_ENOTSUP);
  if (test.join_error == CUV_READY)
    assert (test.leave_error == CUV_READY);
  assert (test.source_join_error == CUV_READY
          || test.source_join_error == UV_ENODEV
          || test.source_join_error == UV_ENOTSUP
          || test.source_join_error == UV_EINVAL);
  if (test.source_join_error == CUV_READY)
    assert (test.source_leave_error == CUV_READY);
  assert (test.udp.state == CUV_UDP_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
}

struct fs_coverage_test
{
  struct cuv_task task;
  struct cuv_file file;
  struct cuv_dir dir;
  struct cuv_dirlist list;
  uv_stat_t stat;
  uv_statfs_t statfs;
  char root_template[128];
  char root[128];
  char file_template[256];
  char file_path[256];
  char hard_path[256];
  char sym_path[256];
  char copy_path[256];
  char link_target[256];
  char real_path[256];
  isz size;
  usz dir_entries;
};

static void
fs_coverage_test_run (struct cuv_task *task)
    async$ ((struct fs_coverage_test *)task)
{
  await$ (cuv_fs_mkdtemp, $.root, sizeof ($.root), $.root_template);

  assert (snprintf ($.file_template, sizeof ($.file_template),
                    "%s/file-XXXXXX", $.root)
          < (int)sizeof ($.file_template));
  assert (snprintf ($.hard_path, sizeof ($.hard_path), "%s/hard", $.root)
          < (int)sizeof ($.hard_path));
  assert (snprintf ($.sym_path, sizeof ($.sym_path), "%s/sym", $.root)
          < (int)sizeof ($.sym_path));
  assert (snprintf ($.copy_path, sizeof ($.copy_path), "%s/copy", $.root)
          < (int)sizeof ($.copy_path));

  await$ (cuv_fs_mkstemp, &$.file, $.file_path, sizeof ($.file_path),
          $.file_template);
  await$ (cuv_fs_write, &$.size, &$.file, "phase-k-data", 12, 0);
  await$ (cuv_fs_fsync, &$.file);
  await$ (cuv_fs_fdatasync, &$.file);
  await$ (cuv_fs_ftruncate, &$.file, 7);
  await$ (cuv_fs_fchmod, &$.file, 0600);
  await$ (cuv_fs_fchown, &$.file, getuid (), getgid ());
  await$ (cuv_fs_close, &$.file);

  await$ (cuv_fs_access, $.file_path, F_OK | R_OK);
  await$ (cuv_fs_chmod, $.file_path, 0640);
  await$ (cuv_fs_chown, $.file_path, getuid (), getgid ());
  await$ (cuv_fs_stat, &$.stat, $.file_path);

  await$ (cuv_fs_link, $.file_path, $.hard_path);
  await$ (cuv_fs_symlink, $.file_path, $.sym_path, 0);
  await$ (cuv_fs_readlink, $.link_target, sizeof ($.link_target), $.sym_path);
  await$ (cuv_fs_realpath, $.real_path, sizeof ($.real_path), $.sym_path);
  await$ (cuv_fs_copyfile, $.file_path, $.copy_path, 0);
  await$ (cuv_fs_statfs, &$.statfs, $.root);

  await$ (cuv_fs_opendir, &$.dir, $.root);
  $.dir_entries = 0;
  for (;;)
    {
      await$ (cuv_fs_readdir, &$.list, &$.dir, 2);
      if (!$.list.count)
        {
          cuv_dirlist_clear (&$.list);
          break;
        }

      $.dir_entries += $.list.count;
      cuv_dirlist_clear (&$.list);
    }
  await$ (cuv_fs_closedir, &$.dir);

  await$ (cuv_fs_unlink, $.sym_path);
  await$ (cuv_fs_unlink, $.hard_path);
  await$ (cuv_fs_unlink, $.copy_path);
  await$ (cuv_fs_unlink, $.file_path);
  exit$ ();
}

static void
test_fs_coverage (void)
{
  struct cuv_loop loop;
  struct fs_coverage_test test = {
    .root_template = "/tmp/libcuv-fs-phase-k-XXXXXX",
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, fs_coverage_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (test.stat.st_size == 7);
  assert (test.statfs.f_bsize > 0);
  assert (!strcmp (test.link_target, test.file_path));
  assert (!strcmp (test.real_path, test.file_path));
  assert (test.dir_entries >= 4);
  assert (!test.file.open);
  assert (!test.file.resource.owner);
  assert (!test.dir.open);
  assert (!test.dir.uv);
  assert (!test.dir.resource.owner);
  assert (!test.list.resource.owner);
  assert (!test.list.entries);
  assert (cuv_loop_close (&loop) == 0);

  assert (rmdir (test.root) == 0);
}

struct dir_cleanup_phase_k_test
{
  struct cuv_task task;
  struct cuv_dir dir;
  uv_stat_t stat;
  char const *directory;
  char const *missing;
};

static void
dir_cleanup_phase_k_test_run (struct cuv_task *task)
    async$ ((struct dir_cleanup_phase_k_test *)task)
{
  await$ (cuv_fs_opendir, &$.dir, $.directory);
  await$ (cuv_fs_stat, &$.stat, $.missing);
  exit$ ();
}

static void
test_dir_cleanup_phase_k (void)
{
  char root[] = "/tmp/libcuv-dir-phase-k-XXXXXX";
  assert (mkdtemp (root));

  char missing[256];
  assert (snprintf (missing, sizeof (missing), "%s/missing", root)
          < (int)sizeof (missing));

  struct cuv_loop loop;
  struct dir_cleanup_phase_k_test test = {
    .directory = root,
    .missing = missing,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, dir_cleanup_phase_k_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == UV_ENOENT);
  assert (!test.dir.open);
  assert (!test.dir.uv);
  assert (!test.dir.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
  assert (rmdir (root) == 0);
}

struct udp_coverage_test
{
  struct cuv_task task;
  struct cuv_udp opened;
  struct cuv_udp opened_old;
  struct cuv_udp recvmmsg;
  uv_os_sock_t socket;
  uv_os_sock_t old_socket;
  struct sockaddr_storage peer;
  struct sockaddr_storage local;
  struct sockaddr_storage peer_result;
  isz sent;
  usz queue_size;
  usz queue_count;
  int using_recvmmsg;
  int send_buffer;
  int recv_buffer;
};

static void
udp_coverage_test_run (struct cuv_task *task)
    async$ ((struct udp_coverage_test *)task)
{
  await$ (cuv_udp_open_ex, &$.opened, $.socket, 0);
  await$ (cuv_udp_getsockname, &$.opened, &$.local);
  await$ (cuv_udp_connect, &$.opened, (struct sockaddr const *)&$.peer);
  await$ (cuv_udp_getpeername, &$.opened, &$.peer_result);
  await$ (cuv_udp_try_send, &$.sent, &$.opened, "phase-k", 7, null);
  await$ (cuv_udp_send_queue, &$.opened, &$.queue_size, &$.queue_count);
  $.send_buffer = 0;
  await$ (cuv_udp_send_buffer, &$.opened, &$.send_buffer);
  $.recv_buffer = 0;
  await$ (cuv_udp_recv_buffer, &$.opened, &$.recv_buffer);
  await$ (cuv_udp_close, &$.opened);

  await$ (cuv_udp_open, &$.opened_old, $.old_socket);
  await$ (cuv_udp_close, &$.opened_old);

  await$ (cuv_udp_init, &$.recvmmsg, UV_UDP_RECVMMSG);
  await$ (cuv_udp_using_recvmmsg, &$.recvmmsg, &$.using_recvmmsg);
  await$ (cuv_udp_close, &$.recvmmsg);
  exit$ ();
}

static int
make_bound_udp_socket (void)
{
  int fd = socket (AF_INET, SOCK_DGRAM, 0);
  assert (fd >= 0);

  struct sockaddr_in address;
  assert (uv_ip4_addr ("127.0.0.1", 0, &address) == 0);
  assert (bind (fd, (struct sockaddr *)&address, sizeof (address)) == 0);
  return fd;
}

static void
test_udp_coverage (void)
{
  struct sockaddr_in peer;
  int receiver = make_udp_address (&peer);
  int sender = make_bound_udp_socket ();
  int old_sender = make_bound_udp_socket ();

  struct cuv_loop loop;
  struct udp_coverage_test test = {
    .socket = sender,
    .old_socket = old_sender,
  };
  memcpy (&test.peer, &peer, sizeof (peer));

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, udp_coverage_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (test.sent == 7);
  assert (test.queue_size == 0);
  assert (test.queue_count == 0);
  assert (test.send_buffer > 0);
  assert (test.recv_buffer > 0);
  assert (test.using_recvmmsg == 0 || test.using_recvmmsg == 1);
  assert (((struct sockaddr *)&test.local)->sa_family == AF_INET);
  assert (((struct sockaddr *)&test.peer_result)->sa_family == AF_INET);

  auto actual_peer = (struct sockaddr_in *)&test.peer_result;
  assert (actual_peer->sin_port == peer.sin_port);
  assert (actual_peer->sin_addr.s_addr == peer.sin_addr.s_addr);
  assert (test.opened.state == CUV_UDP_CLOSED);
  assert (test.opened_old.state == CUV_UDP_CLOSED);
  assert (test.recvmmsg.state == CUV_UDP_CLOSED);
  assert (cuv_loop_close (&loop) == 0);

  char buffer[32] = { 0 };
  assert (recv (receiver, buffer, sizeof (buffer), 0) == 7);
  assert (!memcmp (buffer, "phase-k", 7));
  close (receiver);
}

enum
{
  UDP_BATCH_PHASE_V_MESSAGES = 3
};

struct udp_batch_recv_phase_v_test
{
  struct cuv_task task;
  struct cuv_udp udp;
  struct sockaddr_storage bind_address;
  struct cuv_udp_batch batch;
  struct cuv_udp_message messages[UDP_BATCH_PHASE_V_MESSAGES];
  char buffer[UDP_BATCH_PHASE_V_MESSAGES * CUV_UDP_BATCH_SLOT_SIZE];
  int using_recvmmsg;
};

static void
udp_batch_recv_phase_v_test_run (struct cuv_task *task)
    async$ ((struct udp_batch_recv_phase_v_test *)task)
{
  await$ (cuv_udp_init, &$.udp, AF_INET | UV_UDP_RECVMMSG);
  await$ (cuv_udp_bind, &$.udp, (struct sockaddr const *)&$.bind_address, 0);
  await$ (cuv_udp_using_recvmmsg, &$.udp, &$.using_recvmmsg);
  await$ (cuv_udp_recv_many, &$.udp, &$.batch);
  await$ (cuv_udp_close, &$.udp);
  exit$ ();
}

struct udp_batch_send_phase_v_test
{
  struct cuv_task task;
  struct cuv_udp udp;
  struct sockaddr_storage bind_address;
  struct sockaddr_storage target;
  uv_buf_t buffers[UDP_BATCH_PHASE_V_MESSAGES];
  uv_buf_t *vectors[UDP_BATCH_PHASE_V_MESSAGES];
  unsigned counts[UDP_BATCH_PHASE_V_MESSAGES];
  struct sockaddr *addresses[UDP_BATCH_PHASE_V_MESSAGES];
  char first[3];
  char third[5];
  int sent;
};

static void
udp_batch_send_phase_v_test_run (struct cuv_task *task)
    async$ ((struct udp_batch_send_phase_v_test *)task)
{
  await$ (cuv_udp_bind, &$.udp, (struct sockaddr const *)&$.bind_address, 0);
  await$ (cuv_udp_try_send2, &$.sent, &$.udp, UDP_BATCH_PHASE_V_MESSAGES,
          $.vectors, $.counts, $.addresses, 0);
  await$ (cuv_udp_close, &$.udp);
  exit$ ();
}

static void
test_udp_batch_phase_v (void)
{
  struct sockaddr_in target;
  int reservation = make_udp_address (&target);
  close (reservation);

  struct sockaddr_in sender_address;
  assert (uv_ip4_addr ("127.0.0.1", 0, &sender_address) == 0);

  struct cuv_loop loop;
  struct udp_batch_recv_phase_v_test receiver = { 0 };
  struct udp_batch_send_phase_v_test sender = { 0 };

  memcpy (&receiver.bind_address, &target, sizeof (target));
  receiver.batch.messages = receiver.messages;
  receiver.batch.capacity = UDP_BATCH_PHASE_V_MESSAGES;
  receiver.batch.buffer = receiver.buffer;
  receiver.batch.buffer_capacity = sizeof (receiver.buffer);

  memcpy (&sender.bind_address, &sender_address, sizeof (sender_address));
  memcpy (&sender.target, &target, sizeof (target));
  memcpy (sender.first, "one", sizeof (sender.first));
  memcpy (sender.third, "three", sizeof (sender.third));
  sender.buffers[0] = uv_buf_init (sender.first, sizeof (sender.first));
  sender.buffers[1] = uv_buf_init (sender.first, 0);
  sender.buffers[2] = uv_buf_init (sender.third, sizeof (sender.third));
  for (unsigned i = 0u; i < UDP_BATCH_PHASE_V_MESSAGES; ++i)
    {
      sender.vectors[i] = &sender.buffers[i];
      sender.counts[i] = 1;
      sender.addresses[i] = (struct sockaddr *)&sender.target;
    }

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&receiver.task, &loop, udp_batch_recv_phase_v_test_run);
  cuv_task_init (&sender.task, &loop, udp_batch_send_phase_v_test_run);
  cuv_task_start (&receiver.task);
  cuv_task_start (&sender.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&receiver.task) == 0);
  assert (cuv_task_error (&sender.task) == 0);
  assert (sender.sent == UDP_BATCH_PHASE_V_MESSAGES);
  assert (receiver.batch.count >= 1);
  assert (receiver.messages[0].size == 3);
  assert (!memcmp (receiver.messages[0].data, "one", 3));
  assert (receiver.messages[0].address.ss_family == AF_INET);

  if (receiver.using_recvmmsg)
    {
      assert (receiver.batch.count == UDP_BATCH_PHASE_V_MESSAGES);
      assert (receiver.messages[0].flags & UV_UDP_MMSG_CHUNK);
      assert (receiver.messages[1].size == 0);
      assert (receiver.messages[1].address.ss_family == AF_INET);
      assert (receiver.messages[2].size == 5);
      assert (!memcmp (receiver.messages[2].data, "three", 5));
    }
  else
    assert (receiver.batch.count == 1);

  assert (receiver.udp.state == CUV_UDP_CLOSED);
  assert (sender.udp.state == CUV_UDP_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
}

struct udp_batch_partial_phase_v_test
{
  struct cuv_task task;
  struct cuv_udp udp;
  struct sockaddr_storage bind_address;
  struct cuv_udp_batch batch;
  struct cuv_udp_message message;
  char buffer[4];
};

static void
udp_batch_partial_phase_v_test_run (struct cuv_task *task)
    async$ ((struct udp_batch_partial_phase_v_test *)task)
{
  await$ (cuv_udp_bind, &$.udp, (struct sockaddr const *)&$.bind_address, 0);
  await$ (cuv_udp_recv_many, &$.udp, &$.batch);
  await$ (cuv_udp_close, &$.udp);
  exit$ ();
}

static void
test_udp_batch_partial_phase_v (void)
{
  struct sockaddr_in address;
  int reservation = make_udp_address (&address);
  close (reservation);

  struct cuv_loop loop;
  struct udp_batch_partial_phase_v_test receiver = { 0 };
  struct udp_send_test sender = {
    .message = "12345678",
  };
  memcpy (&receiver.bind_address, &address, sizeof (address));
  receiver.batch.messages = &receiver.message;
  receiver.batch.capacity = 1;
  receiver.batch.buffer = receiver.buffer;
  receiver.batch.buffer_capacity = sizeof (receiver.buffer);
  memcpy (&sender.address, &address, sizeof (address));

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&receiver.task, &loop, udp_batch_partial_phase_v_test_run);
  cuv_task_init (&sender.task, &loop, udp_send_test_run);
  cuv_task_start (&receiver.task);
  cuv_task_start (&sender.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&receiver.task) == 0);
  assert (cuv_task_error (&sender.task) == 0);
  assert (receiver.batch.count == 1);
  assert (receiver.message.size == sizeof (receiver.buffer));
  assert (receiver.message.flags & UV_UDP_PARTIAL);
  assert (!memcmp (receiver.message.data, "1234", sizeof (receiver.buffer)));
  assert (cuv_loop_close (&loop) == 0);
}

struct udp_batch_timeout_phase_v_test
{
  struct cuv_task task;
  struct cuv_udp udp;
  struct sockaddr_storage bind_address;
  struct cuv_udp_batch batch;
  struct cuv_udp_message message;
  char buffer[CUV_UDP_BATCH_SLOT_SIZE];
  int reached;
};

static void
udp_batch_timeout_phase_v_test_run (struct cuv_task *task)
    async$ ((struct udp_batch_timeout_phase_v_test *)task)
{
  await$ (cuv_udp_init, &$.udp, AF_INET | UV_UDP_RECVMMSG);
  await$ (cuv_udp_bind, &$.udp, (struct sockaddr const *)&$.bind_address, 0);
  await$ (5, cuv_udp_recv_many, &$.udp, &$.batch);
  $.reached = 1;
  exit$ ();
}

static void
test_udp_batch_timeout_phase_v (void)
{
  struct sockaddr_in address;
  int reservation = make_udp_address (&address);
  close (reservation);

  struct cuv_loop loop;
  struct udp_batch_timeout_phase_v_test test = { 0 };
  memcpy (&test.bind_address, &address, sizeof (address));
  test.batch.messages = &test.message;
  test.batch.capacity = 1;
  test.batch.buffer = test.buffer;
  test.batch.buffer_capacity = sizeof (test.buffer);

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, udp_batch_timeout_phase_v_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (!test.reached);
  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (test.udp.state == CUV_UDP_CLOSED);
  assert (!test.udp.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
}

struct pipe_advanced_phase_q_test
{
  struct cuv_task task;
  struct cuv_pipe server;
  struct cuv_pipe client;
  struct cuv_pipe accepted;
  char const *path;
  char server_name[256];
  char peer_name[256];
  usz server_name_size;
  usz peer_name_size;
};

static void
pipe_advanced_phase_q_test_run (struct cuv_task *task)
    async$ ((struct pipe_advanced_phase_q_test *)task)
{
  await$ (cuv_pipe_init, &$.server, 0);
  await$ (cuv_pipe_pending_instances, &$.server, 2);
  await$ (cuv_pipe_bind2, &$.server, $.path, strlen ($.path),
          UV_PIPE_NO_TRUNCATE, 0);
  await$ (cuv_pipe_pending_instances, &$.server, 4);
  await$ (cuv_pipe_chmod, &$.server, UV_READABLE | UV_WRITABLE);
  await$ (cuv_pipe_listen, &$.server, 8);

  await$ (cuv_pipe_connect2, &$.client, $.path, strlen ($.path),
          UV_PIPE_NO_TRUNCATE, 0);
  await$ (cuv_pipe_accept, &$.server, &$.accepted, 0);

  $.server_name_size = sizeof ($.server_name);
  await$ (cuv_pipe_getsockname, &$.server, $.server_name, &$.server_name_size);
  $.peer_name_size = sizeof ($.peer_name);
  await$ (cuv_pipe_getpeername, &$.client, $.peer_name, &$.peer_name_size);

  await$ (cuv_pipe_close, &$.accepted);
  await$ (cuv_pipe_close, &$.client);
  await$ (cuv_pipe_close, &$.server);
  exit$ ();
}

static void
test_pipe_advanced_phase_q (void)
{
  char path[] = "/tmp/libcuv-pipe-q-XXXXXX";
  int temporary = mkstemp (path);
  assert (temporary >= 0);
  close (temporary);
  unlink (path);

  struct cuv_loop loop;
  struct pipe_advanced_phase_q_test test = {
    .path = path,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, pipe_advanced_phase_q_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.server_name_size == strlen (path));
  assert (test.peer_name_size == strlen (path));
  assert (!memcmp (test.server_name, path, strlen (path)));
  assert (!memcmp (test.peer_name, path, strlen (path)));
  assert (test.server.state == CUV_PIPE_CLOSED);
  assert (test.client.state == CUV_PIPE_CLOSED);
  assert (test.accepted.state == CUV_PIPE_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
  unlink (path);
}

struct pipe_ipc_phase_q_test
{
  struct cuv_task task;
  struct cuv_pipe sender;
  struct cuv_pipe receiver;
  struct cuv_tcp send_tcp;
  struct cuv_tcp received_tcp;
  struct cuv_tcp received_tcp_try;
  struct cuv_pipe send_pipe;
  struct cuv_pipe received_pipe;
  struct cuv_udp send_udp;
  struct cuv_udp received_udp;

  int sender_fd;
  int receiver_fd;
  int send_pipe_fd;
  int send_pipe_peer;

  struct sockaddr_storage tcp_bind;
  struct sockaddr_storage udp_bind;
  struct sockaddr_storage udp_received_name;

  char marker[8];
  char pipe_data[16];
  isz marker_size;
  isz pipe_size;
  isz try_size;
  int pending_count;
  uv_handle_type pending_type;
};

static void
pipe_ipc_phase_q_test_run (struct cuv_task *task)
    async$ ((struct pipe_ipc_phase_q_test *)task)
{
  await$ (cuv_pipe_open, &$.sender, $.sender_fd, 1);
  await$ (cuv_pipe_open, &$.receiver, $.receiver_fd, 1);

  await$ (cuv_tcp_bind, &$.send_tcp, (struct sockaddr *)&$.tcp_bind, 0);
  await$ (cuv_pipe_write2, &$.sender, "T", 1, &$.send_tcp.resource);
  await$ (cuv_pipe_read, &$.marker_size, &$.receiver, $.marker,
          sizeof ($.marker));
  await$ (cuv_pipe_pending_count, &$.receiver, &$.pending_count);
  assert ($.pending_count == 1);
  await$ (cuv_pipe_pending_type, &$.receiver, &$.pending_type);
  assert ($.pending_type == UV_TCP);
  await$ (cuv_pipe_receive_tcp, &$.receiver, &$.received_tcp);
  assert ($.received_tcp.state == CUV_TCP_BOUND);

  assert (cuv_pipe_try_write2 (task, &$.try_size, &$.sender, "t", 1,
                               &$.send_tcp.resource)
          == CUV_READY);
  await$ (cuv_pipe_read, &$.marker_size, &$.receiver, $.marker,
          sizeof ($.marker));
  await$ (cuv_pipe_pending_type, &$.receiver, &$.pending_type);
  assert ($.pending_type == UV_TCP);
  await$ (cuv_pipe_receive_tcp, &$.receiver, &$.received_tcp_try);

  await$ (cuv_pipe_open, &$.send_pipe, $.send_pipe_fd, 0);
  await$ (cuv_pipe_write2, &$.sender, "P", 1, &$.send_pipe.resource);
  await$ (cuv_pipe_read, &$.marker_size, &$.receiver, $.marker,
          sizeof ($.marker));
  await$ (cuv_pipe_pending_type, &$.receiver, &$.pending_type);
  assert ($.pending_type == UV_NAMED_PIPE);
  await$ (cuv_pipe_receive_pipe, &$.receiver, &$.received_pipe, 0);
  await$ (cuv_pipe_close, &$.send_pipe);

  assert (write ($.send_pipe_peer, "ipc pipe", 8) == 8);
  await$ (cuv_pipe_read, &$.pipe_size, &$.received_pipe, $.pipe_data,
          sizeof ($.pipe_data));
  assert ($.pipe_size == 8);

  await$ (cuv_udp_bind, &$.send_udp, (struct sockaddr *)&$.udp_bind, 0);
  await$ (cuv_pipe_write2, &$.sender, "U", 1, &$.send_udp.resource);
  await$ (cuv_pipe_read, &$.marker_size, &$.receiver, $.marker,
          sizeof ($.marker));
  await$ (cuv_pipe_pending_type, &$.receiver, &$.pending_type);
  assert ($.pending_type == UV_UDP);
  await$ (cuv_pipe_receive_udp, &$.receiver, &$.received_udp);
  await$ (cuv_udp_close, &$.send_udp);
  await$ (cuv_udp_getsockname, &$.received_udp, &$.udp_received_name);

  await$ (cuv_tcp_close, &$.send_tcp);
  await$ (cuv_tcp_close, &$.received_tcp);
  await$ (cuv_tcp_close, &$.received_tcp_try);
  await$ (cuv_pipe_close, &$.received_pipe);
  await$ (cuv_udp_close, &$.received_udp);
  await$ (cuv_pipe_close, &$.receiver);
  await$ (cuv_pipe_close, &$.sender);
  exit$ ();
}

static void
test_pipe_ipc_phase_q (void)
{
  int channels[2];
  int payload[2];
  assert (socketpair (AF_UNIX, SOCK_STREAM, 0, channels) == 0);
  assert (socketpair (AF_UNIX, SOCK_STREAM, 0, payload) == 0);

  struct sockaddr_in tcp_address;
  struct sockaddr_in udp_address;
  assert (uv_ip4_addr ("127.0.0.1", 0, &tcp_address) == 0);
  assert (uv_ip4_addr ("127.0.0.1", 0, &udp_address) == 0);

  struct cuv_loop loop;
  struct pipe_ipc_phase_q_test test = {
    .sender_fd = channels[0],
    .receiver_fd = channels[1],
    .send_pipe_fd = payload[0],
    .send_pipe_peer = payload[1],
  };
  memcpy (&test.tcp_bind, &tcp_address, sizeof (tcp_address));
  memcpy (&test.udp_bind, &udp_address, sizeof (udp_address));

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, pipe_ipc_phase_q_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.try_size == 1);
  assert (!memcmp (test.pipe_data, "ipc pipe", 8));
  assert (test.udp_received_name.ss_family == AF_INET);
  assert (test.sender.state == CUV_PIPE_CLOSED);
  assert (test.receiver.state == CUV_PIPE_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
  close (payload[1]);
}

struct diagnostics_child_test
{
  struct cuv_task task;
};

static void
diagnostics_child_test_run (struct cuv_task *task)
    async$ ((struct diagnostics_child_test *)task)
{
  await$ (cuv_sleep, 5);
  exit$ ();
}

struct diagnostics_owner_test
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct diagnostics_child_test child;
};

static void
diagnostics_owner_test_run (struct cuv_task *task)
    async$ ((struct diagnostics_owner_test *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);
  cuv_task_init (&$.child.task, task->loop, diagnostics_child_test_run);
  assert (cuv_task_group_start (&$.group, &$.child.task) == 0);
  await$ (cuv_sleep, 5);
  await$ (cuv_task_group_wait, &$.group);
  exit$ ();
}

struct diagnostics_observer_test
{
  struct cuv_task task;
  struct diagnostics_owner_test *owner;
  struct cuv_task_diagnostics task_diagnostics;
  struct cuv_loop_diagnostics loop_diagnostics;
  struct cuv_task_group_diagnostics group_diagnostics;
  char dump[8192];
  usz dump_size;
};

static void
diagnostics_observer_test_run (struct cuv_task *task)
    async$ ((struct diagnostics_observer_test *)task)
{
  assert (cuv_task_inspect (&$.owner->task, &$.task_diagnostics) == 0);
  assert (cuv_loop_inspect (task->loop, &$.loop_diagnostics) == 0);
  assert (cuv_task_group_inspect (&$.owner->group, &$.group_diagnostics) == 0);

  FILE *stream = tmpfile ();
  assert (stream);
  assert (cuv_debug_dump_loop (stream, task->loop) == 0);
  assert (cuv_debug_dump_group (stream, &$.owner->group) == 0);
  assert (fflush (stream) == 0);
  rewind (stream);
  $.dump_size = fread ($.dump, 1, sizeof ($.dump) - 1, stream);
  $.dump[$.dump_size] = 0;
  assert (!ferror (stream));
  assert (fclose (stream) == 0);
  exit$ ();
}

static void
test_diagnostics (void)
{
  struct cuv_loop loop;
  struct diagnostics_owner_test owner = { 0 };
  struct diagnostics_observer_test observer = {
    .owner = &owner,
  };

  assert (!strcmp (cuv_task_state_name (CUV_TASK_WAITING), "waiting"));
  assert (!strcmp (cuv_timer_kind_name (CUV_TIMER_TIMEOUT), "timeout"));
  assert (!strcmp (cuv_resource_kind_name (CUV_RESOURCE_TASK_GROUP),
                   "task_group"));
  assert (
      !strcmp (cuv_resource_kind_name (CUV_RESOURCE_ADDRLIST), "addrlist"));

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&owner.task, &loop, diagnostics_owner_test_run);
  cuv_task_init (&observer.task, &loop, diagnostics_observer_test_run);
  cuv_task_start (&owner.task);
  cuv_task_start (&observer.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&owner.task) == 0);
  assert (cuv_task_error (&observer.task) == 0);
  assert (observer.task_diagnostics.state == CUV_TASK_WAITING);
  assert (observer.task_diagnostics.operation_name);
  assert (!strcmp (observer.task_diagnostics.operation_name, "cuv_sleep"));
  assert (observer.task_diagnostics.timer_active);
  assert (observer.task_diagnostics.timer_kind == CUV_TIMER_SLEEP);
  assert (observer.task_diagnostics.resource_count == 1);
  assert (observer.loop_diagnostics.task_count == 3);
  assert (observer.group_diagnostics.count == 1);
  assert (observer.group_diagnostics.owner == &owner.task);
  assert (strstr (observer.dump, "wait=cuv_sleep"));
  assert (strstr (observer.dump, "kind=task_group"));
  assert (strstr (observer.dump, "state=waiting"));

  struct cuv_loop_diagnostics finished;
  assert (cuv_loop_inspect (&loop, &finished) == 0);
  assert (finished.task_count == 0);
  assert (finished.ready_count == 0);
  assert (finished.timer_count == 0);
  assert (cuv_loop_close (&loop) == 0);
}

struct ready_cancel_child
{
  struct cuv_task task;
  unsigned immediate;
};

static void
ready_cancel_child_run (struct cuv_task *task)
    async$ ((struct ready_cancel_child *)task)
{
  if (!$.immediate)
    await$ (cuv_sleep, 1000);
  exit$ ();
}

struct ready_cancel_batch_test
{
  struct cuv_task task;
  struct cuv_task_group group;
  struct ready_cancel_child children[16];
  struct cuv_task *winner;
};

static void
ready_cancel_batch_test_run (struct cuv_task *task)
    async$ ((struct ready_cancel_batch_test *)task)
{
  assert (cuv_task_group_init (&$.group, task) == 0);

  for (usz index = 0; index < 16; ++index)
    {
      $.children[index].immediate = index == 0;
      cuv_task_init (&$.children[index].task, task->loop,
                     ready_cancel_child_run);
      assert (cuv_task_group_start (&$.group, &$.children[index].task) == 0);
    }

  await$ (cuv_task_group_race, &$.group, &$.winner);
  assert ($.winner == &$.children[0].task);
  assert (cuv_debug_check_loop (task->loop) == 0);
  exit$ ();
}

static void
test_ready_cancel_batch (void)
{
  struct cuv_loop loop;
  struct ready_cancel_batch_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, ready_cancel_batch_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

static int
make_tcp_pair (int sockets[2])
{
  int listener = socket (AF_INET, SOCK_STREAM, 0);
  if (listener < 0)
    return -1;

  struct sockaddr_in address = { 0 };
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
  address.sin_port = 0;

  if (bind (listener, (struct sockaddr *)&address, sizeof (address)) < 0
      || listen (listener, 1) < 0)
    {
      close (listener);
      return -1;
    }

  socklen_t length = sizeof (address);
  if (getsockname (listener, (struct sockaddr *)&address, &length) < 0)
    {
      close (listener);
      return -1;
    }

  sockets[1] = socket (AF_INET, SOCK_STREAM, 0);
  if (sockets[1] < 0)
    {
      close (listener);
      return -1;
    }

  if (connect (sockets[1], (struct sockaddr *)&address, sizeof (address)) < 0)
    {
      close (sockets[1]);
      close (listener);
      return -1;
    }

  sockets[0] = accept (listener, null, null);
  close (listener);
  if (sockets[0] < 0)
    {
      close (sockets[1]);
      return -1;
    }

  return 0;
}

struct tcp_advanced_phase_r_test
{
  struct cuv_task task;
  struct cuv_tcp opened;
  struct cuv_tcp initialized;
  uv_os_sock_t socket;
  int peer_fd;
  struct sockaddr_storage local;
  struct sockaddr_storage peer;
  isz try_size;
  isz peer_size;
  usz queue_size;
  int readable;
  int writable;
  int send_buffer;
  int recv_buffer;
  char peer_data[16];
};

static void
tcp_advanced_phase_r_test_run (struct cuv_task *task)
    async$ ((struct tcp_advanced_phase_r_test *)task)
{
  await$ (cuv_tcp_open, &$.opened, $.socket);
  await$ (cuv_tcp_getsockname, &$.opened, &$.local);
  await$ (cuv_tcp_getpeername, &$.opened, &$.peer);
  await$ (cuv_tcp_is_readable, &$.opened, &$.readable);
  await$ (cuv_tcp_is_writable, &$.opened, &$.writable);
  await$ (cuv_tcp_write_queue, &$.opened, &$.queue_size);
  await$ (cuv_tcp_set_blocking, &$.opened, 0);

  $.send_buffer = 0;
  await$ (cuv_tcp_send_buffer, &$.opened, &$.send_buffer);
  $.recv_buffer = 0;
  await$ (cuv_tcp_recv_buffer, &$.opened, &$.recv_buffer);

  await$ (cuv_tcp_try_write, &$.try_size, &$.opened, "phase-r", 7);
  $.peer_size = recv ($.peer_fd, $.peer_data, sizeof ($.peer_data), 0);
  await$ (cuv_tcp_close_reset, &$.opened);

  await$ (cuv_tcp_init, &$.initialized, AF_INET);
  $.send_buffer = 0;
  await$ (cuv_tcp_send_buffer, &$.initialized, &$.send_buffer);
  await$ (cuv_tcp_close, &$.initialized);
  exit$ ();
}

static void
test_tcp_advanced_phase_r (void)
{
  int sockets[2];
  assert (make_tcp_pair (sockets) == 0);

  struct cuv_loop loop;
  struct tcp_advanced_phase_r_test test = {
    .socket = sockets[0],
    .peer_fd = sockets[1],
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, tcp_advanced_phase_r_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (test.try_size == 7);
  assert (test.peer_size == 7);
  assert (!memcmp (test.peer_data, "phase-r", 7));
  assert (test.queue_size == 0);
  assert (test.readable);
  assert (test.writable);
  assert (test.send_buffer > 0);
  assert (test.recv_buffer > 0);
  assert (((struct sockaddr *)&test.local)->sa_family == AF_INET);
  assert (((struct sockaddr *)&test.peer)->sa_family == AF_INET);
  assert (test.opened.state == CUV_TCP_CLOSED);
  assert (test.initialized.state == CUV_TCP_CLOSED);
  assert (!test.opened.resource.owner);
  assert (!test.initialized.resource.owner);
  assert (cuv_loop_close (&loop) == 0);
  close (sockets[1]);
}

struct phase_s_test
{
  struct cuv_task task;
  struct cuv_file input;
  struct cuv_file output;
  struct cuv_addrlist addresses;
  char const *input_path;
  char const *output_path;
  char const *symlink_path;
  char const *dir_path;
  uv_stat_t stat;
  isz size;
  usz address_count;
  int address_family;
};

static void
phase_s_test_run (struct cuv_task *task) async$ ((struct phase_s_test *)task)
{
  static struct addrinfo const hints = {
    .ai_family = AF_UNSPEC,
    .ai_socktype = SOCK_STREAM,
    .ai_flags = AI_NUMERICHOST,
  };

  await$ (cuv_fs_lstat, &$.stat, $.symlink_path);
  assert (S_ISLNK ($.stat.st_mode));

  await$ (cuv_fs_utime, $.input_path, 12345.0, 12346.0);
  await$ (cuv_fs_lutime, $.symlink_path, 12347.0, 12348.0);
  await$ (cuv_fs_lchown, $.symlink_path, getuid (), getgid ());

  await$ (cuv_fs_open, &$.input, $.input_path, O_RDONLY, 0);
  await$ (cuv_fs_open, &$.output, $.output_path, O_CREAT | O_TRUNC | O_WRONLY,
          0600);
  await$ (cuv_fs_futime, &$.input, 12349.0, 12350.0);
  await$ (cuv_fs_sendfile, &$.size, &$.output, &$.input, 0, 4096);
  await$ (cuv_fs_close, &$.output);
  await$ (cuv_fs_close, &$.input);

  await$ (cuv_getaddrinfo, &$.addresses, "127.0.0.1", null, &hints);
  $.address_count = $.addresses.count;
  $.address_family = $.addresses.entries[0].family;
  cuv_addrlist_clear (&$.addresses);

  await$ (cuv_fs_rmdir, $.dir_path);
  exit$ ();
}

static void
test_phase_s (void)
{
  char input_path[] = "/tmp/libcuv-phase-s-input-XXXXXX";
  int input_fd = mkstemp (input_path);
  assert (input_fd >= 0);
  static char const payload[] = "phase-s-sendfile";
  assert (write (input_fd, payload, sizeof (payload) - 1)
          == (ssize_t)(sizeof (payload) - 1));
  close (input_fd);

  char output_path[] = "/tmp/libcuv-phase-s-output-XXXXXX";
  int output_fd = mkstemp (output_path);
  assert (output_fd >= 0);
  close (output_fd);

  char symlink_path[512];
  assert (snprintf (symlink_path, sizeof (symlink_path), "%s-link", input_path)
          < (int)sizeof (symlink_path));
  assert (symlink (input_path, symlink_path) == 0);

  char dir_path[] = "/tmp/libcuv-phase-s-dir-XXXXXX";
  assert (mkdtemp (dir_path));

  struct cuv_loop loop;
  struct phase_s_test test = {
    .input_path = input_path,
    .output_path = output_path,
    .symlink_path = symlink_path,
    .dir_path = dir_path,
  };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, phase_s_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.size == (isz)(sizeof (payload) - 1));
  assert (test.address_count >= 1);
  assert (test.address_family == AF_INET || test.address_family == AF_INET6);
  assert (!test.addresses.resource.owner);
  assert (!test.addresses.storage);
  assert (!test.addresses.entries);
  assert (cuv_loop_close (&loop) == 0);

  char buffer[64] = { 0 };
  int fd = open (output_path, O_RDONLY);
  assert (fd >= 0);
  ssize_t count = read (fd, buffer, sizeof (buffer));
  close (fd);
  assert (count == (ssize_t)(sizeof (payload) - 1));
  assert (memcmp (buffer, payload, sizeof (payload) - 1) == 0);

  assert (unlink (symlink_path) == 0);
  assert (unlink (input_path) == 0);
  assert (unlink (output_path) == 0);
}

struct addrlist_cleanup_test
{
  struct cuv_task task;
  struct cuv_addrlist addresses;
  usz observed;
};

static void
addrlist_cleanup_test_run (struct cuv_task *task)
    async$ ((struct addrlist_cleanup_test *)task)
{
  await$ (cuv_getaddrinfo, &$.addresses, "localhost", null, null);
  $.observed = $.addresses.count;
  exit$ ();
}

static void
test_addrlist_cleanup (void)
{
  struct cuv_loop loop;
  struct addrlist_cleanup_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, addrlist_cleanup_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);
  assert (cuv_task_error (&test.task) == 0);
  assert (test.observed >= 1);
  assert (!test.addresses.resource.owner);
  assert (!test.addresses.storage);
  assert (!test.addresses.entries);
  assert (test.addresses.count == 0);
  assert (cuv_loop_close (&loop) == 0);
}

struct phase_t_test
{
  struct cuv_task task;
  struct cuv_idle idle;
  struct cuv_prepare prepare;
  struct cuv_check check;
  struct cuv_timer timer;
  u64 repeat;
  u64 due_in;
  unsigned phase_count;
  unsigned timer_count;
};

static void
phase_t_test_run (struct cuv_task *task) async$ ((struct phase_t_test *)task)
{
  await$ (cuv_idle_wait, &$.idle);
  ++$.phase_count;

  await$ (cuv_prepare_wait, &$.prepare);
  ++$.phase_count;

  await$ (cuv_check_wait, &$.check);
  ++$.phase_count;

  await$ (cuv_timer_start, &$.timer, 1, 1);
  await$ (cuv_sleep, 6);
  await$ (cuv_timer_stop, &$.timer);

  /* A tick that arrived while this task was sleeping remains consumable. */
  await$ (cuv_timer_wait, &$.timer);
  ++$.timer_count;

  await$ (cuv_timer_start, &$.timer, 1, 2);
  await$ (cuv_timer_get_repeat, &$.timer, &$.repeat);
  await$ (cuv_timer_get_due_in, &$.timer, &$.due_in);
  await$ (cuv_timer_wait, &$.timer);
  ++$.timer_count;

  await$ (cuv_timer_stop, &$.timer);
  await$ (cuv_timer_again, &$.timer);
  await$ (cuv_timer_wait, &$.timer);
  ++$.timer_count;

  await$ (cuv_timer_set_repeat, &$.timer, 0);
  await$ (cuv_timer_get_repeat, &$.timer, &$.repeat);
  await$ (cuv_timer_stop, &$.timer);
  await$ (cuv_timer_close, &$.timer);

  await$ (cuv_check_close, &$.check);
  await$ (cuv_prepare_close, &$.prepare);
  await$ (cuv_idle_close, &$.idle);
  exit$ ();
}

static void
test_phase_t (void)
{
  struct cuv_loop loop;
  struct phase_t_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, phase_t_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (test.phase_count == 3);
  assert (test.timer_count == 3);
  assert (test.repeat == 0);
  assert (test.timer.state == CUV_REUSABLE_TIMER_CLOSED);
  assert (test.idle.state == CUV_PHASE_CLOSED);
  assert (test.prepare.state == CUV_PHASE_CLOSED);
  assert (test.check.state == CUV_PHASE_CLOSED);
  assert (!test.timer.resource.owner);
  assert (!test.idle.resource.owner);
  assert (!test.prepare.resource.owner);
  assert (!test.check.resource.owner);
  assert (cuv_debug_check_task (&test.task) == 0);
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

struct timer_timeout_phase_t_test
{
  struct cuv_task task;
  struct cuv_timer timer;
  unsigned reached;
};

static void
timer_timeout_phase_t_test_run (struct cuv_task *task)
    async$ ((struct timer_timeout_phase_t_test *)task)
{
  await$ (cuv_timer_start, &$.timer, 1000, 1000);
  await$ (5, cuv_timer_wait, &$.timer);
  $.reached = 1;
  exit$ ();
}

static void
test_timer_timeout_phase_t (void)
{
  struct cuv_loop loop;
  struct timer_timeout_phase_t_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, timer_timeout_phase_t_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == UV_ETIMEDOUT);
  assert (!test.reached);
  assert (test.timer.state == CUV_REUSABLE_TIMER_CLOSED);
  assert (!test.timer.resource.owner);
  assert (cuv_debug_check_task (&test.task) == 0);
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

struct phase_cleanup_test
{
  struct cuv_task task;
  struct cuv_idle idle;
  struct cuv_prepare prepare;
  struct cuv_check check;
};

static void
phase_cleanup_test_run (struct cuv_task *task)
    async$ ((struct phase_cleanup_test *)task)
{
  await$ (cuv_idle_wait, &$.idle);
  await$ (cuv_prepare_wait, &$.prepare);
  await$ (cuv_check_wait, &$.check);
  exit$ ();
}

static void
test_phase_cleanup (void)
{
  struct cuv_loop loop;
  struct phase_cleanup_test test = { 0 };

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&test.task, &loop, phase_cleanup_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_task_error (&test.task) == 0);
  assert (test.idle.state == CUV_PHASE_CLOSED);
  assert (test.prepare.state == CUV_PHASE_CLOSED);
  assert (test.check.state == CUV_PHASE_CLOSED);
  assert (cuv_loop_close (&loop) == 0);
}

struct loop_stop_phase_u_test
{
  struct cuv_task task;
  unsigned step;
};

static void
loop_stop_phase_u_test_run (struct cuv_task *task)
    async$ ((struct loop_stop_phase_u_test *)task)
{
  $.step = 1;
  assert (cuv_loop_stop (task->loop) == 0);
  yield$ ();
  $.step = 2;
  exit$ ();
}

static void
test_loop_control_phase_u (void)
{
  struct cuv_loop loop;
  struct loop_stop_phase_u_test test = { 0 };
  u64 now;
  int backend_fd;
  int backend_timeout;

  assert (cuv_loop_init (&loop) == 0);
  assert (cuv_loop_alive (&loop) == 0);
  assert (cuv_loop_update_time (&loop) == 0);
  assert (cuv_loop_now (&loop, &now) == 0);
  assert (cuv_loop_backend_fd (&loop, &backend_fd) == 0);
  assert (backend_fd >= -1);
  assert (cuv_loop_backend_timeout (&loop, &backend_timeout) == 0);
  assert (backend_timeout >= -1);

  cuv_task_init (&test.task, &loop, loop_stop_phase_u_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_alive (&loop) == 1);

  assert (cuv_loop_run (&loop) == 0);
  assert (test.step == 1);
  assert (test.task.state == CUV_TASK_READY);
  assert (loop.stop_requested == 0);
  assert (cuv_loop_alive (&loop) == 1);
  assert (cuv_debug_check_loop (&loop) == 0);

  assert (cuv_loop_run (&loop) == 0);
  assert (test.step == 2);
  assert (test.task.state == CUV_TASK_DONE);
  assert (cuv_loop_alive (&loop) == 0);
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

struct loop_metrics_phase_u_test
{
  struct cuv_task task;
};

static void
loop_metrics_phase_u_test_run (struct cuv_task *task)
    async$ ((struct loop_metrics_phase_u_test *)task)
{
  await$ (cuv_sleep, 10);
  exit$ ();
}

static void
test_loop_metrics_phase_u (void)
{
  struct cuv_loop loop;
  struct loop_metrics_phase_u_test test = { 0 };
  uv_metrics_t metrics = { 0 };
  u64 before;
  u64 after;
  u64 idle;

  assert (cuv_loop_init (&loop) == 0);
  assert (cuv_loop_enable_idle_metrics (&loop) == 0);
  assert (cuv_loop_update_time (&loop) == 0);
  assert (cuv_loop_now (&loop, &before) == 0);

  cuv_task_init (&test.task, &loop, loop_metrics_phase_u_test_run);
  cuv_task_start (&test.task);
  assert (cuv_loop_run (&loop) == 0);

  assert (cuv_loop_metrics (&loop, &metrics) == 0);
  assert (metrics.loop_count > 0);
  assert (cuv_loop_idle_time (&loop, &idle) == 0);
  assert (cuv_loop_update_time (&loop) == 0);
  assert (cuv_loop_now (&loop, &after) == 0);
  assert (after >= before);

  assert (cuv_task_error (&test.task) == 0);
  assert (cuv_debug_check_loop (&loop) == 0);
  assert (cuv_loop_close (&loop) == 0);
}

#if !defined(_WIN32)
struct loop_fork_sleeper_phase_u_test
{
  struct cuv_task task;
  unsigned reached;
};

static void
loop_fork_sleeper_phase_u_test_run (struct cuv_task *task)
    async$ ((struct loop_fork_sleeper_phase_u_test *)task)
{
  await$ (cuv_sleep, 10000);
  $.reached = 1;
  exit$ ();
}

struct loop_fork_stopper_phase_u_test
{
  struct cuv_task task;
};

static void
loop_fork_stopper_phase_u_test_run (struct cuv_task *task)
    async$ ((struct loop_fork_stopper_phase_u_test *)task)
{
  assert (cuv_loop_stop (task->loop) == 0);
  exit$ ();
}

static void
test_loop_fork_phase_u (void)
{
  struct cuv_loop loop;
  struct loop_fork_sleeper_phase_u_test sleeper = { 0 };
  struct loop_fork_stopper_phase_u_test stopper = { 0 };
  int status;

  assert (cuv_loop_init (&loop) == 0);
  cuv_task_init (&sleeper.task, &loop, loop_fork_sleeper_phase_u_test_run);
  cuv_task_init (&stopper.task, &loop, loop_fork_stopper_phase_u_test_run);
  cuv_task_start (&sleeper.task);
  cuv_task_start (&stopper.task);

  /* Arm libcuv's internal task-timer heap, then return without completing it.
   */
  assert (cuv_loop_run (&loop) == 0);
  assert (sleeper.task.state == CUV_TASK_WAITING);
  assert (sleeper.task.timer.active);
  assert (stopper.task.state == CUV_TASK_DONE);

  pid_t pid = fork ();
  assert (pid >= 0);

  if (pid == 0)
    {
      if (cuv_loop_fork (&loop) != 0)
        _exit (10);
      if (cuv_task_cancel (&sleeper.task) != 0)
        _exit (11);
      if (cuv_loop_run (&loop) != 0)
        _exit (12);
      if (cuv_task_error (&sleeper.task) != UV_ECANCELED)
        _exit (13);
      if (cuv_debug_check_loop (&loop) != 0)
        _exit (14);
      if (cuv_loop_close (&loop) != 0)
        _exit (15);
      _exit (0);
    }

  assert (waitpid (pid, &status, 0) == pid);
  assert (WIFEXITED (status));
  assert (WEXITSTATUS (status) == 0);

  assert (cuv_task_cancel (&sleeper.task) == 0);
  assert (cuv_loop_run (&loop) == 0);
  assert (!sleeper.reached);
  assert (cuv_task_error (&sleeper.task) == UV_ECANCELED);
  assert (cuv_loop_close (&loop) == 0);
}
#else
static void
test_loop_fork_phase_u (void)
{
}
#endif

int
main ()
{
  test_loop_fork_phase_u ();
  test_loop_control_phase_u ();
  test_loop_metrics_phase_u ();
  test_diagnostics ();
  test_sleep ();
  test_timeout_sleep ();
  test_timeout_success ();
  test_fs ();
  test_fs_adapters ();
  test_fs_coverage ();
  test_phase_s ();
  test_dir_cleanup_phase_k ();
  test_dirlist_cleanup ();
  test_fs_event ();
  test_fs_event_timeout ();
  test_fs_poll ();
  test_fs_poll_missing ();
  test_fs_poll_timeout ();
  test_phase_t ();
  test_timer_timeout_phase_t ();
  test_phase_cleanup ();
  test_poll_fd ();
  test_poll_socket ();
  test_poll_timeout ();
  test_tty ();
  test_tty_vterm ();
  test_tty_timeout ();
  test_cleanup_on_error ();
  test_multi_cleanup_on_error ();
  test_task_wait ();
  test_tcp ();
  test_tcp_options ();
  test_tcp_advanced_phase_r ();
  test_dns ();
  test_addrlist_cleanup ();
  test_utility_adapters ();
  test_cancel_sleep ();
  test_tcp_read ();
  test_tcp_timeout ();
  test_tcp_cleanup_on_error ();
  test_work ();
  test_signal ();
  test_signal_timeout ();
  test_udp_send ();
  test_udp_recv ();
  test_udp_connect ();
  test_udp_options ();
  test_udp_coverage ();
  test_udp_batch_phase_v ();
  test_udp_batch_partial_phase_v ();
  test_udp_batch_timeout_phase_v ();
  test_pipe_open ();
  test_pipe_connect ();
  test_tcp_server ();
  test_tcp_accept_transfer ();
  test_tcp_accept_timeout ();
  test_pipe_server ();
  test_pipe_accept_timeout ();
  test_pipe_advanced_phase_q ();
  test_pipe_ipc_phase_q ();
  test_work_timeout ();
  test_udp_timeout ();
  test_pipe_timeout ();
  test_process ();
  test_process_failure ();
  test_process_cleanup ();
  test_process_error_cleanup ();
  test_process_timeout ();
  test_process_kill ();
  test_process_pid_kill ();
  test_process_stdio ();
  test_process_run_capture ();
  test_process_run_input ();
  test_process_run_truncate ();
  test_process_run_timeout ();
  test_process_run_logical_timeout ();
  test_process_run_failure ();
  test_task_group_wait ();
  test_task_group_error ();
  test_task_group_race ();
  test_ready_cancel_batch ();
  test_task_group_cleanup ();
  test_task_group_timeout ();
  test_task_group_reuse ();
  test_task_group_cancel ();
  test_task_cancel_states ();
  test_cancel_timeout_precedence ();
  test_cancel_timeout_running_work ();
  test_group_race_running_work ();
  test_nested_group_cancellation ();
  test_group_cancel_after_completion ();
  test_process_task_cancel ();
  test_process_global_helpers ();
  test_cleanup_error_precedence ();

  puts ("libcuv tests passed");
  return 0;
}
