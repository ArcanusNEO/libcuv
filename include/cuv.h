#ifndef _H_CUV_
#define _H_CUV_ 1

#include <cmacs.h>
#include <uv.h>

#ifndef CUV_API
#define CUV_API
#endif

struct cuv_loop;
struct cuv_task;
struct cuv_wait;
struct cuv_resource;
struct cuv_file;
struct cuv_dirlist;
struct cuv_dir;
struct cuv_addrlist;
struct cuv_tcp;
struct cuv_udp;
struct cuv_pipe;
struct cuv_signal;
struct cuv_fs_event;
struct cuv_fs_poll;
struct cuv_idle;
struct cuv_prepare;
struct cuv_check;
struct cuv_timer;
struct cuv_poll;
struct cuv_tty;
struct cuv_process;
struct cuv_process_run;
struct cuv_task_group;

enum cuv_task_state
{
  CUV_TASK_IDLE,
  CUV_TASK_READY,
  CUV_TASK_RUNNING,
  CUV_TASK_WAITING,
  CUV_TASK_CLOSING,
  CUV_TASK_DONE
};

enum cuv_tcp_state
{
  CUV_TCP_NEW,
  CUV_TCP_INITIALIZED,
  CUV_TCP_BOUND,
  CUV_TCP_LISTENING,
  CUV_TCP_CONNECTED,
  CUV_TCP_SHUTDOWN,
  CUV_TCP_CLOSING,
  CUV_TCP_CLOSED
};

enum cuv_udp_state
{
  CUV_UDP_NEW,
  CUV_UDP_INITIALIZED,
  CUV_UDP_BOUND,
  CUV_UDP_CLOSING,
  CUV_UDP_CLOSED
};

enum cuv_pipe_state
{
  CUV_PIPE_NEW,
  CUV_PIPE_INITIALIZED,
  CUV_PIPE_BOUND,
  CUV_PIPE_LISTENING,
  CUV_PIPE_ACTIVE,
  CUV_PIPE_SHUTDOWN,
  CUV_PIPE_CLOSING,
  CUV_PIPE_CLOSED
};

enum cuv_signal_state
{
  CUV_SIGNAL_NEW,
  CUV_SIGNAL_INITIALIZED,
  CUV_SIGNAL_CLOSING,
  CUV_SIGNAL_CLOSED
};

enum cuv_fs_event_state
{
  CUV_FS_EVENT_NEW,
  CUV_FS_EVENT_INITIALIZED,
  CUV_FS_EVENT_CLOSING,
  CUV_FS_EVENT_CLOSED
};

enum cuv_fs_poll_state
{
  CUV_FS_POLL_NEW,
  CUV_FS_POLL_INITIALIZED,
  CUV_FS_POLL_CLOSING,
  CUV_FS_POLL_CLOSED
};

enum cuv_phase_state
{
  CUV_PHASE_NEW,
  CUV_PHASE_INITIALIZED,
  CUV_PHASE_CLOSING,
  CUV_PHASE_CLOSED
};

enum cuv_reusable_timer_state
{
  CUV_REUSABLE_TIMER_NEW,
  CUV_REUSABLE_TIMER_INITIALIZED,
  CUV_REUSABLE_TIMER_CLOSING,
  CUV_REUSABLE_TIMER_CLOSED
};

enum cuv_poll_state
{
  CUV_POLL_NEW,
  CUV_POLL_INITIALIZED,
  CUV_POLL_CLOSING,
  CUV_POLL_CLOSED
};

enum cuv_tty_state
{
  CUV_TTY_NEW,
  CUV_TTY_INITIALIZED,
  CUV_TTY_CLOSING,
  CUV_TTY_CLOSED
};

enum cuv_process_state
{
  CUV_PROCESS_NEW,
  CUV_PROCESS_RUNNING,
  CUV_PROCESS_EXITED,
  CUV_PROCESS_FAILED,
  CUV_PROCESS_CLOSING,
  CUV_PROCESS_CLOSED
};

enum cuv_task_group_wait
{
  CUV_TASK_GROUP_WAIT_NONE,
  CUV_TASK_GROUP_WAIT_ALL,
  CUV_TASK_GROUP_WAIT_RACE
};

enum cuv_resource_kind
{
  CUV_RESOURCE_CUSTOM,
  CUV_RESOURCE_FILE,
  CUV_RESOURCE_DIRLIST,
  CUV_RESOURCE_DIR,
  CUV_RESOURCE_TCP,
  CUV_RESOURCE_UDP,
  CUV_RESOURCE_PIPE,
  CUV_RESOURCE_SIGNAL,
  CUV_RESOURCE_FS_EVENT,
  CUV_RESOURCE_FS_POLL,
  CUV_RESOURCE_POLL,
  CUV_RESOURCE_TTY,
  CUV_RESOURCE_PROCESS,
  CUV_RESOURCE_TASK_GROUP,
  CUV_RESOURCE_ADDRLIST,
  CUV_RESOURCE_IDLE,
  CUV_RESOURCE_PREPARE,
  CUV_RESOURCE_CHECK,
  CUV_RESOURCE_TIMER
};

enum
{
  CUV_PENDING = 0,
  CUV_READY = 1
};

#define CUV_UDP_BATCH_SLOT_SIZE (64u * 1024u)

struct cuv_resource
{
  struct lsnod node;
  struct cuv_task *owner;
  int (*close) (struct cuv_resource *);
  enum cuv_resource_kind kind;
};

struct cuv_file
{
  struct cuv_resource resource;
  uv_file fd;
  unsigned open;
};

struct cuv_dirent
{
  char *name;
  uv_dirent_type_t type;
};

struct cuv_dirlist
{
  struct cuv_resource resource;
  struct cuv_dirent *entries;
  usz count;
  unsigned active;
};

struct cuv_dir
{
  struct cuv_resource resource;
  uv_dir_t *uv;
  unsigned open;
};

struct cuv_addrinfo
{
  int flags;
  int family;
  int socktype;
  int protocol;
  usz address_length;
  struct sockaddr_storage address;
  char *canonname;
};

struct cuv_addrlist
{
  struct cuv_resource resource;
  struct cuv_addrinfo *entries;
  usz count;
  void *storage;
  unsigned active;
};

struct cuv_udp_message
{
  void *data;
  usz size;
  struct sockaddr_storage address;
  unsigned flags;
};

struct cuv_udp_batch
{
  struct cuv_udp_message *messages;
  usz capacity;
  usz count;
  void *buffer;
  usz buffer_capacity;
};

union cuv_wait_operation
{
  struct
  {
    struct cuv_file *file;
  } fs_file;

  struct
  {
    uv_buf_t buffer;
    isz *size;
  } fs_io;

  struct
  {
    uv_stat_t *stat;
  } fs_stat;

  struct
  {
    struct cuv_dirlist *list;
  } fs_scandir;

  struct
  {
    char *path;
    usz capacity;
    struct cuv_file *file;
  } fs_path;

  struct
  {
    uv_statfs_t *statfs;
  } fs_statfs;

  struct
  {
    struct cuv_dir *dir;
  } fs_dir;

  struct
  {
    struct cuv_dir *dir;
    struct cuv_dirlist *list;
    uv_dirent_t *entries;
    usz capacity;
  } fs_readdir;

  struct
  {
    uv_stream_t *stream;
    void *buffer;
    usz capacity;
    isz *size;
  } stream_read;

  struct
  {
    uv_buf_t buffer;
  } stream_write;

  struct
  {
    uv_stream_t *stream;
  } stream_shutdown;

  struct
  {
    struct cuv_tcp *tcp;
  } tcp_connect;

  struct
  {
    struct cuv_tcp *server;
    struct cuv_tcp *client;
  } tcp_accept;

  struct
  {
    uv_buf_t buffer;
  } tcp_write;

  struct
  {
    struct cuv_tcp *tcp;
    void *buffer;
    usz capacity;
    isz *size;
  } tcp_read;

  struct
  {
    struct sockaddr_storage *address;
  } resolve;

  struct
  {
    struct cuv_addrlist *list;
  } getaddrinfo;

  struct
  {
    char *host;
    usz host_capacity;
    char *service;
    usz service_capacity;
  } getnameinfo;

  struct
  {
    void (*function) (void *);
    void *argument;
  } work;

  struct
  {
    struct cuv_signal *signal;
  } signal_wait;

  struct
  {
    struct cuv_fs_event *watcher;
    char *filename;
    usz filename_capacity;
    int *events;
  } fs_event_wait;

  struct
  {
    struct cuv_fs_poll *watcher;
    int *status;
    uv_stat_t *previous;
    uv_stat_t *current;
  } fs_poll_wait;

  struct
  {
    struct cuv_idle *watcher;
  } idle_wait;

  struct
  {
    struct cuv_prepare *watcher;
  } prepare_wait;

  struct
  {
    struct cuv_check *watcher;
  } check_wait;

  struct
  {
    struct cuv_timer *timer;
  } timer_wait;

  struct
  {
    uv_buf_t buffer;
  } udp_send;

  struct
  {
    struct cuv_udp *udp;
    void *buffer;
    usz capacity;
    isz *size;
    struct sockaddr_storage *address;
    unsigned *flags;
  } udp_recv;

  struct
  {
    struct cuv_udp *udp;
    struct cuv_udp_batch *batch;
    unsigned using_recvmmsg;
  } udp_recv_many;

  struct
  {
    struct cuv_pipe *pipe;
  } pipe_connect;

  struct
  {
    struct cuv_pipe *server;
    struct cuv_pipe *client;
    unsigned ipc;
  } pipe_accept;

  struct
  {
    uv_buf_t buffer;
  } pipe_write;

  struct
  {
    struct cuv_pipe *pipe;
    void *buffer;
    usz capacity;
    isz *size;
  } pipe_read;

  struct
  {
    struct cuv_poll *poll;
    int *events;
  } poll_wait;

  struct
  {
    uv_buf_t buffer;
  } tty_write;

  struct
  {
    struct cuv_tty *tty;
    void *buffer;
    usz capacity;
    isz *size;
  } tty_read;

  struct
  {
    struct cuv_process *process;
    i64 *exit_status;
    int *term_signal;
  } process_wait;

  struct
  {
    struct cuv_process_run *run;
  } process_run;

  struct
  {
    struct cuv_task *target;
  } task_wait;

  struct
  {
    struct cuv_task_group *group;
    struct cuv_task **winner;
  } task_group;
};

struct cuv_wait
{
  union uv_any_req req;
  int (*cancel) (struct cuv_task *);
  int abort_error;
  char const *operation_name;
  union cuv_wait_operation operation;
};

enum cuv_timer_kind
{
  CUV_TIMER_NONE,
  CUV_TIMER_SLEEP,
  CUV_TIMER_TIMEOUT
};

struct cuv_timer_wait
{
  u64 deadline;
  int error;
  enum cuv_timer_kind kind;
  unsigned active;
};

coroutine$ (cuv_task, {
  struct cuv_loop *loop;
  void (*run) (struct cuv_task *);

  struct lsnod ready_node;
  struct lsnod loop_node;
  struct cuv_task *joiner;

  struct cuv_task_group *group;
  struct lsnod group_node;

  struct lsnod resources;
  struct cuv_resource *cleanup;

  enum cuv_task_state state;
  int error;
  unsigned aborting;

  struct cuv_wait wait;
  struct cuv_timer_wait timer;
});

struct cuv_task_group
{
  struct cuv_resource resource;
  struct cuv_loop *loop;

  struct lsnod children;
  struct cuv_task *waiter;
  struct cuv_task *winner;
  struct cuv_task **winner_out;

  usz count;
  int first_error;
  int winner_error;
  int close_error;

  enum cuv_task_group_wait wait_mode;

  unsigned initialized;
  unsigned closing;
  unsigned close_active;
};

struct cuv_tcp
{
  uv_tcp_t uv;
  struct cuv_resource resource;

  struct cuv_task *reader;
  struct cuv_task *acceptor;
  struct cuv_task *close_waiter;

  int listen_error;
  unsigned connection_pending;

  enum cuv_tcp_state state;
};

struct cuv_udp
{
  uv_udp_t uv;
  struct cuv_resource resource;

  struct cuv_task *receiver;
  struct cuv_task *close_waiter;

  enum cuv_udp_state state;
  unsigned connected;
};

struct cuv_pipe
{
  uv_pipe_t uv;
  struct cuv_resource resource;

  struct cuv_task *reader;
  struct cuv_task *acceptor;
  struct cuv_task *close_waiter;

  int listen_error;
  unsigned connection_pending;

  enum cuv_pipe_state state;
  unsigned ipc;
};

struct cuv_signal
{
  uv_signal_t uv;
  struct cuv_resource resource;

  struct cuv_task *waiter;
  struct cuv_task *close_waiter;

  enum cuv_signal_state state;
  int signum;
};

struct cuv_fs_event
{
  uv_fs_event_t uv;
  struct cuv_resource resource;

  struct cuv_task *waiter;
  struct cuv_task *close_waiter;

  enum cuv_fs_event_state state;
  int close_error;
};

struct cuv_fs_poll
{
  uv_fs_poll_t uv;
  struct cuv_resource resource;

  struct cuv_task *waiter;
  struct cuv_task *close_waiter;

  enum cuv_fs_poll_state state;
  int close_error;
};

struct cuv_idle
{
  uv_idle_t uv;
  struct cuv_resource resource;

  struct cuv_task *waiter;
  struct cuv_task *close_waiter;

  enum cuv_phase_state state;
  int close_error;
};

struct cuv_prepare
{
  uv_prepare_t uv;
  struct cuv_resource resource;

  struct cuv_task *waiter;
  struct cuv_task *close_waiter;

  enum cuv_phase_state state;
  int close_error;
};

struct cuv_check
{
  uv_check_t uv;
  uv_idle_t kick;
  struct cuv_resource resource;

  struct cuv_task *waiter;
  struct cuv_task *close_waiter;

  enum cuv_phase_state state;
  int close_error;
  unsigned kick_initialized;
  unsigned close_pending;
};

struct cuv_timer
{
  uv_timer_t uv;
  struct cuv_resource resource;

  struct cuv_task *waiter;
  struct cuv_task *close_waiter;

  u64 pending;
  enum cuv_reusable_timer_state state;
  int close_error;
};

struct cuv_poll
{
  uv_poll_t uv;
  struct cuv_resource resource;

  struct cuv_task *waiter;
  struct cuv_task *close_waiter;

  enum cuv_poll_state state;
  int close_error;
};

struct cuv_tty
{
  uv_tty_t uv;
  struct cuv_resource resource;

  struct cuv_task *reader;
  struct cuv_task *close_waiter;

  enum cuv_tty_state state;
  int close_error;
};

struct cuv_process
{
  uv_process_t uv;
  struct cuv_resource resource;

  struct cuv_task *waiter;

  i64 exit_status;
  int term_signal;
  int spawn_error;

  enum cuv_process_state state;
};

struct cuv_process_run_options
{
  uv_process_options_t process;

  void const *input;
  usz input_size;

  void *output;
  usz output_capacity;

  void *error_output;
  usz error_output_capacity;

  /* 0 means logical cancellation only; otherwise send this signal first. */
  int kill_signal;
};

struct cuv_process_run
{
  struct cuv_task_group group;
  struct cuv_process process;

  struct cuv_pipe input_pipe;
  struct cuv_pipe output_pipe;
  struct cuv_pipe error_pipe;

  struct cuv_task wait_task;
  struct cuv_task input_task;
  struct cuv_task output_task;
  struct cuv_task error_task;

  void const *input;
  usz input_size;
  usz input_offset;

  void *output;
  usz output_capacity;
  usz output_size;
  isz output_read_size;

  void *error_output;
  usz error_output_capacity;
  usz error_output_size;
  isz error_read_size;

  i64 exit_status;
  int term_signal;
  int kill_signal;
  int kill_error;

  unsigned output_truncated;
  unsigned error_output_truncated;
  unsigned initialized;
  unsigned input_active;
  unsigned output_active;
  unsigned error_output_active;

  char output_scratch[4096];
  char error_output_scratch[4096];
};

struct cuv_loop
{
  uv_loop_t uv;

  struct lsnod ready;
  struct lsnod tasks;
  usz ready_count;
  usz task_count;

  uv_timer_t timer;
  struct cuv_task **timer_heap;
  usz timer_count;
  usz timer_capacity;

  unsigned initialized;
  unsigned stop_requested;
};

struct cuv_task_diagnostics
{
  enum cuv_task_state state;
  enum cuv_timer_kind timer_kind;
  char const *operation_name;
  int error;
  int abort_error;
  u64 timer_deadline;
  usz resource_count;
  struct cuv_task *joiner;
  struct cuv_task_group *group;
  struct cuv_resource *cleanup;
  unsigned aborting;
  unsigned timer_active;
  unsigned suspended;
};

struct cuv_loop_diagnostics
{
  usz ready_count;
  usz task_count;
  usz timer_count;
  unsigned initialized;
  unsigned uv_alive;
};

struct cuv_task_group_diagnostics
{
  struct cuv_task *owner;
  struct cuv_task *waiter;
  struct cuv_task *winner;
  usz count;
  int first_error;
  int winner_error;
  int close_error;
  enum cuv_task_group_wait wait_mode;
  unsigned initialized;
  unsigned closing;
};

CUV_API char const *cuv_task_state_name (enum cuv_task_state state);
CUV_API char const *cuv_timer_kind_name (enum cuv_timer_kind kind);
CUV_API char const *cuv_resource_kind_name (enum cuv_resource_kind kind);

CUV_API int cuv_task_inspect (struct cuv_task const *task,
                              struct cuv_task_diagnostics *diagnostics);
CUV_API int cuv_loop_inspect (struct cuv_loop const *loop,
                              struct cuv_loop_diagnostics *diagnostics);
CUV_API int
cuv_task_group_inspect (struct cuv_task_group const *group,
                        struct cuv_task_group_diagnostics *diagnostics);

CUV_API int cuv_debug_dump_task (FILE *stream, struct cuv_task const *task);
CUV_API int cuv_debug_dump_group (FILE *stream,
                                  struct cuv_task_group const *group);
CUV_API int cuv_debug_dump_loop (FILE *stream, struct cuv_loop const *loop);

CUV_API int cuv_debug_check_task (struct cuv_task const *task);
CUV_API int cuv_debug_check_group (struct cuv_task_group const *group);
CUV_API int cuv_debug_check_loop (struct cuv_loop const *loop);

CUV_API int cuv_loop_init (struct cuv_loop *loop);
CUV_API int cuv_loop_run (struct cuv_loop *loop);
CUV_API int cuv_loop_stop (struct cuv_loop *loop);
CUV_API int cuv_loop_alive (struct cuv_loop const *loop);
CUV_API int cuv_loop_now (struct cuv_loop const *loop, u64 *milliseconds);
CUV_API int cuv_loop_update_time (struct cuv_loop *loop);
CUV_API int cuv_loop_backend_fd (struct cuv_loop const *loop, int *fd);
CUV_API int cuv_loop_backend_timeout (struct cuv_loop const *loop,
                                      int *milliseconds);
CUV_API int cuv_loop_fork (struct cuv_loop *loop);
CUV_API int cuv_loop_enable_idle_metrics (struct cuv_loop *loop);
CUV_API int cuv_loop_metrics (struct cuv_loop *loop, uv_metrics_t *metrics);
CUV_API int cuv_loop_idle_time (struct cuv_loop *loop, u64 *nanoseconds);
CUV_API int cuv_loop_close (struct cuv_loop *loop);

CUV_API void cuv_task_init (struct cuv_task *task, struct cuv_loop *loop,
                            void (*run) (struct cuv_task *));
CUV_API void cuv_task_start (struct cuv_task *task);
CUV_API int cuv_task_wait (struct cuv_task *task, struct cuv_task *target);
CUV_API int cuv_task_cancel (struct cuv_task *task);
CUV_API int cuv_task_error (struct cuv_task const *task);

CUV_API int cuv_task_group_init (struct cuv_task_group *group,
                                 struct cuv_task *owner);
CUV_API int cuv_task_group_start (struct cuv_task_group *group,
                                  struct cuv_task *child);
CUV_API int cuv_task_group_wait (struct cuv_task *task,
                                 struct cuv_task_group *group);
CUV_API int cuv_task_group_race (struct cuv_task *task,
                                 struct cuv_task_group *group,
                                 struct cuv_task **winner);
CUV_API int cuv_task_group_cancel (struct cuv_task_group *group);

CUV_API int cuv_fs_open (struct cuv_task *task, struct cuv_file *file,
                         char const *path, int flags, int mode);
CUV_API int cuv_fs_read (struct cuv_task *task, isz *size,
                         struct cuv_file *file, void *buffer, usz capacity,
                         i64 offset);
CUV_API int cuv_fs_write (struct cuv_task *task, isz *size,
                          struct cuv_file *file, void const *buffer,
                          usz length, i64 offset);
CUV_API int cuv_fs_close (struct cuv_task *task, struct cuv_file *file);
CUV_API int cuv_fs_stat (struct cuv_task *task, uv_stat_t *stat,
                         char const *path);
CUV_API int cuv_fs_lstat (struct cuv_task *task, uv_stat_t *stat,
                          char const *path);
CUV_API int cuv_fs_fstat (struct cuv_task *task, uv_stat_t *stat,
                          struct cuv_file *file);
CUV_API int cuv_fs_unlink (struct cuv_task *task, char const *path);
CUV_API int cuv_fs_rmdir (struct cuv_task *task, char const *path);
CUV_API int cuv_fs_rename (struct cuv_task *task, char const *path,
                           char const *new_path);
CUV_API int cuv_fs_mkdir (struct cuv_task *task, char const *path, int mode);
CUV_API int cuv_fs_scandir (struct cuv_task *task, struct cuv_dirlist *list,
                            char const *path, int flags);
CUV_API int cuv_fs_access (struct cuv_task *task, char const *path, int mode);
CUV_API int cuv_fs_chmod (struct cuv_task *task, char const *path, int mode);
CUV_API int cuv_fs_fchmod (struct cuv_task *task, struct cuv_file *file,
                           int mode);
CUV_API int cuv_fs_chown (struct cuv_task *task, char const *path,
                          uv_uid_t uid, uv_gid_t gid);
CUV_API int cuv_fs_fchown (struct cuv_task *task, struct cuv_file *file,
                           uv_uid_t uid, uv_gid_t gid);
CUV_API int cuv_fs_lchown (struct cuv_task *task, char const *path,
                           uv_uid_t uid, uv_gid_t gid);
CUV_API int cuv_fs_fsync (struct cuv_task *task, struct cuv_file *file);
CUV_API int cuv_fs_fdatasync (struct cuv_task *task, struct cuv_file *file);
CUV_API int cuv_fs_ftruncate (struct cuv_task *task, struct cuv_file *file,
                              i64 offset);
CUV_API int cuv_fs_sendfile (struct cuv_task *task, isz *size,
                             struct cuv_file *output, struct cuv_file *input,
                             i64 offset, usz length);
CUV_API int cuv_fs_utime (struct cuv_task *task, char const *path,
                          double atime, double mtime);
CUV_API int cuv_fs_futime (struct cuv_task *task, struct cuv_file *file,
                           double atime, double mtime);
CUV_API int cuv_fs_lutime (struct cuv_task *task, char const *path,
                           double atime, double mtime);
CUV_API int cuv_fs_readlink (struct cuv_task *task, char *buffer, usz capacity,
                             char const *path);
CUV_API int cuv_fs_realpath (struct cuv_task *task, char *buffer, usz capacity,
                             char const *path);
CUV_API int cuv_fs_link (struct cuv_task *task, char const *path,
                         char const *new_path);
CUV_API int cuv_fs_symlink (struct cuv_task *task, char const *path,
                            char const *new_path, int flags);
CUV_API int cuv_fs_copyfile (struct cuv_task *task, char const *path,
                             char const *new_path, int flags);
CUV_API int cuv_fs_mkdtemp (struct cuv_task *task, char *path, usz capacity,
                            char const *template);
CUV_API int cuv_fs_mkstemp (struct cuv_task *task, struct cuv_file *file,
                            char *path, usz capacity, char const *template);
CUV_API int cuv_fs_statfs (struct cuv_task *task, uv_statfs_t *statfs,
                           char const *path);
CUV_API int cuv_fs_opendir (struct cuv_task *task, struct cuv_dir *dir,
                            char const *path);
CUV_API int cuv_fs_readdir (struct cuv_task *task, struct cuv_dirlist *list,
                            struct cuv_dir *dir, usz capacity);
CUV_API int cuv_fs_closedir (struct cuv_task *task, struct cuv_dir *dir);
CUV_API void cuv_dirlist_clear (struct cuv_dirlist *list);

CUV_API int cuv_tcp_init (struct cuv_task *task, struct cuv_tcp *tcp,
                          unsigned flags);
CUV_API int cuv_tcp_open (struct cuv_task *task, struct cuv_tcp *tcp,
                          uv_os_sock_t socket);
CUV_API int cuv_tcp_connect (struct cuv_task *task, struct cuv_tcp *tcp,
                             struct sockaddr const *address);
CUV_API int cuv_tcp_getsockname (struct cuv_task *task, struct cuv_tcp *tcp,
                                 struct sockaddr_storage *address);
CUV_API int cuv_tcp_getpeername (struct cuv_task *task, struct cuv_tcp *tcp,
                                 struct sockaddr_storage *address);
CUV_API int cuv_tcp_bind (struct cuv_task *task, struct cuv_tcp *tcp,
                          struct sockaddr const *address, unsigned flags);
CUV_API int cuv_tcp_listen (struct cuv_task *task, struct cuv_tcp *tcp,
                            int backlog);
CUV_API int cuv_tcp_accept (struct cuv_task *task, struct cuv_tcp *server,
                            struct cuv_tcp *client);
CUV_API int cuv_tcp_read (struct cuv_task *task, isz *size,
                          struct cuv_tcp *tcp, void *buffer, usz capacity);
CUV_API int cuv_tcp_write (struct cuv_task *task, struct cuv_tcp *tcp,
                           void const *buffer, usz length);
CUV_API int cuv_tcp_try_write (struct cuv_task *task, isz *size,
                               struct cuv_tcp *tcp, void const *buffer,
                               usz length);
CUV_API int cuv_tcp_write_queue (struct cuv_task *task, struct cuv_tcp *tcp,
                                 usz *size);
CUV_API int cuv_tcp_is_readable (struct cuv_task *task, struct cuv_tcp *tcp,
                                 int *readable);
CUV_API int cuv_tcp_is_writable (struct cuv_task *task, struct cuv_tcp *tcp,
                                 int *writable);
CUV_API int cuv_tcp_set_blocking (struct cuv_task *task, struct cuv_tcp *tcp,
                                  int blocking);
CUV_API int cuv_tcp_send_buffer (struct cuv_task *task, struct cuv_tcp *tcp,
                                 int *size);
CUV_API int cuv_tcp_recv_buffer (struct cuv_task *task, struct cuv_tcp *tcp,
                                 int *size);
CUV_API int cuv_tcp_shutdown (struct cuv_task *task, struct cuv_tcp *tcp);
CUV_API int cuv_tcp_close (struct cuv_task *task, struct cuv_tcp *tcp);
CUV_API int cuv_tcp_close_reset (struct cuv_task *task, struct cuv_tcp *tcp);
CUV_API int cuv_tcp_nodelay (struct cuv_task *task, struct cuv_tcp *tcp,
                             int enable);
CUV_API int cuv_tcp_keepalive (struct cuv_task *task, struct cuv_tcp *tcp,
                               int enable, unsigned delay);
CUV_API int cuv_tcp_keepalive_ex (struct cuv_task *task, struct cuv_tcp *tcp,
                                  int enable, unsigned idle, unsigned interval,
                                  unsigned count);
CUV_API int cuv_tcp_simultaneous_accepts (struct cuv_task *task,
                                          struct cuv_tcp *tcp, int enable);

CUV_API int cuv_resolve (struct cuv_task *task,
                         struct sockaddr_storage *address, char const *host,
                         char const *service);
CUV_API int cuv_getaddrinfo (struct cuv_task *task, struct cuv_addrlist *list,
                             char const *host, char const *service,
                             struct addrinfo const *hints);
CUV_API void cuv_addrlist_clear (struct cuv_addrlist *list);

CUV_API int cuv_getnameinfo (struct cuv_task *task, char *host,
                             usz host_capacity, char *service,
                             usz service_capacity,
                             struct sockaddr const *address, int flags);

CUV_API int cuv_random (struct cuv_task *task, void *buffer, usz length,
                        unsigned flags);

CUV_API int cuv_work (struct cuv_task *task, void (*work) (void *),
                      void *argument);

CUV_API int cuv_signal_wait (struct cuv_task *task, struct cuv_signal *signal,
                             int signum);
CUV_API int cuv_signal_close (struct cuv_task *task,
                              struct cuv_signal *signal);

CUV_API int cuv_fs_event_wait (struct cuv_task *task,
                               struct cuv_fs_event *watcher, char *filename,
                               usz filename_capacity, int *events,
                               char const *path, unsigned flags);
CUV_API int cuv_fs_event_close (struct cuv_task *task,
                                struct cuv_fs_event *watcher);
CUV_API int cuv_fs_event_getpath (struct cuv_task *task,
                                  struct cuv_fs_event *watcher, char *buffer,
                                  usz *size);

CUV_API int cuv_fs_poll_wait (struct cuv_task *task,
                              struct cuv_fs_poll *watcher, int *status,
                              uv_stat_t *previous, uv_stat_t *current,
                              char const *path, unsigned interval);
CUV_API int cuv_fs_poll_close (struct cuv_task *task,
                               struct cuv_fs_poll *watcher);
CUV_API int cuv_fs_poll_getpath (struct cuv_task *task,
                                 struct cuv_fs_poll *watcher, char *buffer,
                                 usz *size);

CUV_API int cuv_idle_wait (struct cuv_task *task, struct cuv_idle *watcher);
CUV_API int cuv_idle_close (struct cuv_task *task, struct cuv_idle *watcher);

CUV_API int cuv_prepare_wait (struct cuv_task *task,
                              struct cuv_prepare *watcher);
CUV_API int cuv_prepare_close (struct cuv_task *task,
                               struct cuv_prepare *watcher);

CUV_API int cuv_check_wait (struct cuv_task *task, struct cuv_check *watcher);
CUV_API int cuv_check_close (struct cuv_task *task, struct cuv_check *watcher);

CUV_API int cuv_timer_start (struct cuv_task *task, struct cuv_timer *timer,
                             u64 timeout, u64 repeat);
CUV_API int cuv_timer_wait (struct cuv_task *task, struct cuv_timer *timer);
CUV_API int cuv_timer_stop (struct cuv_task *task, struct cuv_timer *timer);
CUV_API int cuv_timer_again (struct cuv_task *task, struct cuv_timer *timer);
CUV_API int cuv_timer_set_repeat (struct cuv_task *task,
                                  struct cuv_timer *timer, u64 repeat);
CUV_API int cuv_timer_get_repeat (struct cuv_task *task,
                                  struct cuv_timer *timer, u64 *repeat);
CUV_API int cuv_timer_get_due_in (struct cuv_task *task,
                                  struct cuv_timer *timer, u64 *due_in);
CUV_API int cuv_timer_close (struct cuv_task *task, struct cuv_timer *timer);

CUV_API int cuv_udp_init (struct cuv_task *task, struct cuv_udp *udp,
                          unsigned flags);
CUV_API int cuv_udp_open (struct cuv_task *task, struct cuv_udp *udp,
                          uv_os_sock_t socket);
CUV_API int cuv_udp_open_ex (struct cuv_task *task, struct cuv_udp *udp,
                             uv_os_sock_t socket, unsigned flags);
CUV_API int cuv_udp_bind (struct cuv_task *task, struct cuv_udp *udp,
                          struct sockaddr const *address, unsigned flags);
CUV_API int cuv_udp_connect (struct cuv_task *task, struct cuv_udp *udp,
                             struct sockaddr const *address);
CUV_API int cuv_udp_send (struct cuv_task *task, struct cuv_udp *udp,
                          void const *buffer, usz length,
                          struct sockaddr const *address);
CUV_API int cuv_udp_recv (struct cuv_task *task, isz *size,
                          struct cuv_udp *udp, void *buffer, usz capacity,
                          struct sockaddr_storage *address, unsigned *flags);
CUV_API int cuv_udp_close (struct cuv_task *task, struct cuv_udp *udp);
CUV_API int cuv_udp_set_membership (struct cuv_task *task, struct cuv_udp *udp,
                                    char const *multicast_address,
                                    char const *interface_address,
                                    uv_membership membership);
CUV_API int cuv_udp_set_source_membership (struct cuv_task *task,
                                           struct cuv_udp *udp,
                                           char const *multicast_address,
                                           char const *interface_address,
                                           char const *source_address,
                                           uv_membership membership);
CUV_API int cuv_udp_set_multicast_loop (struct cuv_task *task,
                                        struct cuv_udp *udp, int enable);
CUV_API int cuv_udp_set_multicast_ttl (struct cuv_task *task,
                                       struct cuv_udp *udp, int ttl);
CUV_API int cuv_udp_set_multicast_interface (struct cuv_task *task,
                                             struct cuv_udp *udp,
                                             char const *interface_address);
CUV_API int cuv_udp_set_broadcast (struct cuv_task *task, struct cuv_udp *udp,
                                   int enable);
CUV_API int cuv_udp_set_ttl (struct cuv_task *task, struct cuv_udp *udp,
                             int ttl);
CUV_API int cuv_udp_getsockname (struct cuv_task *task, struct cuv_udp *udp,
                                 struct sockaddr_storage *address);
CUV_API int cuv_udp_getpeername (struct cuv_task *task, struct cuv_udp *udp,
                                 struct sockaddr_storage *address);
CUV_API int cuv_udp_try_send (struct cuv_task *task, isz *size,
                              struct cuv_udp *udp, void const *buffer,
                              usz length, struct sockaddr const *address);
CUV_API int cuv_udp_try_send2 (struct cuv_task *task, int *sent,
                               struct cuv_udp *udp, unsigned count,
                               uv_buf_t *buffers[], unsigned buffer_counts[],
                               struct sockaddr *addresses[], unsigned flags);
CUV_API int cuv_udp_recv_many (struct cuv_task *task, struct cuv_udp *udp,
                               struct cuv_udp_batch *batch);
CUV_API int cuv_udp_using_recvmmsg (struct cuv_task *task, struct cuv_udp *udp,
                                    int *enabled);
CUV_API int cuv_udp_send_queue (struct cuv_task *task, struct cuv_udp *udp,
                                usz *size, usz *count);
CUV_API int cuv_udp_send_buffer (struct cuv_task *task, struct cuv_udp *udp,
                                 int *size);
CUV_API int cuv_udp_recv_buffer (struct cuv_task *task, struct cuv_udp *udp,
                                 int *size);

CUV_API int cuv_pipe_init (struct cuv_task *task, struct cuv_pipe *pipe,
                           int ipc);
CUV_API int cuv_pipe_open (struct cuv_task *task, struct cuv_pipe *pipe,
                           uv_file file, int ipc);
CUV_API int cuv_pipe_connect (struct cuv_task *task, struct cuv_pipe *pipe,
                              char const *name, int ipc);
CUV_API int cuv_pipe_connect2 (struct cuv_task *task, struct cuv_pipe *pipe,
                               char const *name, usz name_length,
                               unsigned flags, int ipc);
CUV_API int cuv_pipe_bind (struct cuv_task *task, struct cuv_pipe *pipe,
                           char const *name, int ipc);
CUV_API int cuv_pipe_bind2 (struct cuv_task *task, struct cuv_pipe *pipe,
                            char const *name, usz name_length, unsigned flags,
                            int ipc);
CUV_API int cuv_pipe_listen (struct cuv_task *task, struct cuv_pipe *pipe,
                             int backlog);
CUV_API int cuv_pipe_accept (struct cuv_task *task, struct cuv_pipe *server,
                             struct cuv_pipe *client, int ipc);
CUV_API int cuv_pipe_read (struct cuv_task *task, isz *size,
                           struct cuv_pipe *pipe, void *buffer, usz capacity);
CUV_API int cuv_pipe_write (struct cuv_task *task, struct cuv_pipe *pipe,
                            void const *buffer, usz length);
CUV_API int cuv_pipe_try_write (struct cuv_task *task, isz *size,
                                struct cuv_pipe *pipe, void const *buffer,
                                usz length);
CUV_API int cuv_pipe_write_queue (struct cuv_task *task, struct cuv_pipe *pipe,
                                  usz *size);
CUV_API int cuv_pipe_is_readable (struct cuv_task *task, struct cuv_pipe *pipe,
                                  int *readable);
CUV_API int cuv_pipe_is_writable (struct cuv_task *task, struct cuv_pipe *pipe,
                                  int *writable);
CUV_API int cuv_pipe_set_blocking (struct cuv_task *task,
                                   struct cuv_pipe *pipe, int blocking);
CUV_API int cuv_pipe_write2 (struct cuv_task *task, struct cuv_pipe *pipe,
                             void const *buffer, usz length,
                             struct cuv_resource *send);
CUV_API int cuv_pipe_try_write2 (struct cuv_task *task, isz *size,
                                 struct cuv_pipe *pipe, void const *buffer,
                                 usz length, struct cuv_resource *send);
CUV_API int cuv_pipe_getsockname (struct cuv_task *task, struct cuv_pipe *pipe,
                                  char *buffer, usz *size);
CUV_API int cuv_pipe_getpeername (struct cuv_task *task, struct cuv_pipe *pipe,
                                  char *buffer, usz *size);
CUV_API int cuv_pipe_pending_instances (struct cuv_task *task,
                                        struct cuv_pipe *pipe, int count);
CUV_API int cuv_pipe_pending_count (struct cuv_task *task,
                                    struct cuv_pipe *pipe, int *count);
CUV_API int cuv_pipe_pending_type (struct cuv_task *task,
                                   struct cuv_pipe *pipe,
                                   uv_handle_type *type);
CUV_API int cuv_pipe_chmod (struct cuv_task *task, struct cuv_pipe *pipe,
                            int flags);
CUV_API int cuv_pipe_receive_tcp (struct cuv_task *task, struct cuv_pipe *pipe,
                                  struct cuv_tcp *tcp);
CUV_API int cuv_pipe_receive_pipe (struct cuv_task *task,
                                   struct cuv_pipe *pipe,
                                   struct cuv_pipe *received, int ipc);
CUV_API int cuv_pipe_receive_udp (struct cuv_task *task, struct cuv_pipe *pipe,
                                  struct cuv_udp *udp);
CUV_API int cuv_pipe_shutdown (struct cuv_task *task, struct cuv_pipe *pipe);
CUV_API int cuv_pipe_close (struct cuv_task *task, struct cuv_pipe *pipe);

CUV_API int cuv_poll_init (struct cuv_task *task, struct cuv_poll *poll,
                           int fd);
CUV_API int cuv_poll_init_socket (struct cuv_task *task, struct cuv_poll *poll,
                                  uv_os_sock_t socket);
CUV_API int cuv_poll_wait (struct cuv_task *task, struct cuv_poll *poll,
                           int *events, int interests);
CUV_API int cuv_poll_close (struct cuv_task *task, struct cuv_poll *poll);

CUV_API int cuv_tty_open (struct cuv_task *task, struct cuv_tty *tty,
                          uv_file file);
CUV_API int cuv_tty_read (struct cuv_task *task, isz *size,
                          struct cuv_tty *tty, void *buffer, usz capacity);
CUV_API int cuv_tty_write (struct cuv_task *task, struct cuv_tty *tty,
                           void const *buffer, usz length);
CUV_API int cuv_tty_try_write (struct cuv_task *task, isz *size,
                               struct cuv_tty *tty, void const *buffer,
                               usz length);
CUV_API int cuv_tty_write_queue (struct cuv_task *task, struct cuv_tty *tty,
                                 usz *size);
CUV_API int cuv_tty_is_readable (struct cuv_task *task, struct cuv_tty *tty,
                                 int *readable);
CUV_API int cuv_tty_is_writable (struct cuv_task *task, struct cuv_tty *tty,
                                 int *writable);
CUV_API int cuv_tty_set_blocking (struct cuv_task *task, struct cuv_tty *tty,
                                  int blocking);
CUV_API int cuv_tty_set_mode (struct cuv_task *task, struct cuv_tty *tty,
                              uv_tty_mode_t mode);
CUV_API int cuv_tty_get_winsize (struct cuv_task *task, struct cuv_tty *tty,
                                 int *width, int *height);
CUV_API int cuv_tty_close (struct cuv_task *task, struct cuv_tty *tty);
CUV_API int cuv_tty_reset_mode ();
CUV_API int cuv_tty_get_vterm_state (uv_tty_vtermstate_t *state);
CUV_API int cuv_tty_set_vterm_state (uv_tty_vtermstate_t state);

CUV_API int cuv_process_spawn (struct cuv_task *task,
                               struct cuv_process *process,
                               uv_process_options_t const *options);
CUV_API int cuv_process_wait (struct cuv_task *task,
                              struct cuv_process *process, i64 *exit_status,
                              int *term_signal);
CUV_API int cuv_process_kill (struct cuv_process *process, int signum);
CUV_API int cuv_process_pid (struct cuv_process const *process, uv_pid_t *pid);
CUV_API int cuv_kill (uv_pid_t pid, int signum);
CUV_API int cuv_disable_stdio_inheritance ();

CUV_API int cuv_process_run (struct cuv_task *task,
                             struct cuv_process_run *run,
                             struct cuv_process_run_options const *options);
CUV_API int cuv_process_run_clear (struct cuv_process_run *run);

CUV_API int cuv_sleep (struct cuv_task *task, u64 milliseconds);
CUV_API void cuv_task_timeout_start (struct cuv_task *task, u64 milliseconds);

CUV_API void cuv_resource_transfer (struct cuv_resource *resource,
                                    struct cuv_task *owner);
CUV_API void cuv_resource_detach (struct cuv_resource *resource);

#define _P_CUV_await_dummy_ ((int (*) (struct cuv_task *, ...))null)
#define _P_CUV_await_task_ ((struct cuv_task *)&$)
#define _P_CUV_await_plain_call_(first, second, ...)                          \
  choose$ (ipe$ (first), (first), _P_CUV_await_dummy_) (                      \
      _P_CUV_await_task_, second __VA_OPT__ (, ) __VA_ARGS__)
#define _P_CUV_await_timed_call_(first, second, ...)                          \
  choose$ (ipe$ (first), _P_CUV_await_dummy_,                                 \
           (second)) (_P_CUV_await_task_ __VA_OPT__ (, ) __VA_ARGS__)
#define _P_CUV_await_call_(first, second, ...)                                \
  choose$ (                                                                   \
      ipe$ (first),                                                           \
      _P_CUV_await_plain_call_ (first, second __VA_OPT__ (, ) __VA_ARGS__),   \
      _P_CUV_await_timed_call_ (first, second __VA_OPT__ (, ) __VA_ARGS__))
#define _P_CUV_await_helper_(ms, status, first, second, ...)                  \
  do                                                                          \
    {                                                                         \
      _P_CUV_await_task_->error = 0;                                          \
      _P_CUV_await_task_->wait.operation_name                                 \
          = choose$ (ipe$ (first), quote$ (first), quote$ (second));          \
      u64 ms = choose$ (ipe$ (first), 0, first);                              \
      int status                                                              \
          = _P_CUV_await_call_ (first, second __VA_OPT__ (, ) __VA_ARGS__);   \
      if (status != CUV_PENDING)                                              \
        _P_CUV_await_task_->wait.operation_name = null;                       \
      if (status < 0)                                                         \
        {                                                                     \
          _P_CUV_await_task_->error = status;                                 \
          loop$ ();                                                           \
        }                                                                     \
      if (status == CUV_PENDING)                                              \
        {                                                                     \
          _P_CUV_await_task_->state = CUV_TASK_WAITING;                       \
          if (ms > 0)                                                         \
            cuv_task_timeout_start (_P_CUV_await_task_, ms);                  \
          yield$ ();                                                          \
          if (_P_CUV_await_task_->error < 0)                                  \
            loop$ ();                                                         \
        }                                                                     \
    }                                                                         \
  while (0)
#define _P_CUV_await_(first, second, ...)                                     \
  _P_CUV_await_helper_ (uniq$ (ms), uniq$ (status), first,                    \
                        second __VA_OPT__ (, ) __VA_ARGS__)
#undef await$
#define await$(...) _P_CUV_await_ (__VA_ARGS__)

#endif /* _H_CUV_ */
