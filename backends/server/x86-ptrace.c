#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <server.h>
#include <breakpoints.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <signal.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <mono/metadata/threads.h>

/*
 * NOTE:  The manpage is wrong about the POKE_* commands - the last argument
 *        is the data (a word) to be written, not a pointer to it.
 *
 * In general, the ptrace(2) manpage is very bad, you should really read
 * kernel/ptrace.c and arch/i386/kernel/ptrace.c in the Linux source code
 * to get a better understanding for this stuff.
 */

#ifdef __linux__
#include "x86-linux-ptrace.h"
#endif

#ifdef __FreeBSD__
#include "x86-freebsd-ptrace.h"
#endif

#include "x86-arch.h"

static guint32 io_thread (gpointer data);

struct InferiorHandle
{
	int pid, tid;
#ifdef __linux__
	int mem_fd;
#endif
	int last_signal;
	int output_fd [2], error_fd [2];
	ChildOutputFunc stdout_handler, stderr_handler;
	int is_thread;
};

static void
server_ptrace_finalize (ServerHandle *handle)
{
	if (handle->inferior->pid) {
	  int status;

	  do_wait (-1, &status);
	}
	x86_arch_finalize (handle->arch);
	g_free (handle->inferior);
	g_free (handle);
}

static ServerCommandError
server_ptrace_continue (ServerHandle *handle)
{
	InferiorHandle *inferior = handle->inferior;

	errno = 0;
	if (ptrace (PT_CONTINUE, inferior->pid, (caddr_t) 1, inferior->last_signal)) {
		if (errno == ESRCH)
			return COMMAND_ERROR_NOT_STOPPED;

		return COMMAND_ERROR_UNKNOWN_ERROR;
	}

	return COMMAND_ERROR_NONE;
}

static ServerCommandError
server_ptrace_step (ServerHandle *handle)
{
	InferiorHandle *inferior = handle->inferior;

	errno = 0;
	if (ptrace (PT_STEP, inferior->pid, (caddr_t) 1, inferior->last_signal)) {
		if (errno == ESRCH)
			return COMMAND_ERROR_NOT_STOPPED;

		return COMMAND_ERROR_UNKNOWN_ERROR;
	}

	return COMMAND_ERROR_NONE;
}

static ServerCommandError
server_ptrace_detach (ServerHandle *handle)
{
	InferiorHandle *inferior = handle->inferior;

	if (ptrace (PT_DETACH, inferior->pid, NULL, 0)) {
		g_message (G_STRLOC ": %d - %s", inferior->pid, g_strerror (errno));
		return COMMAND_ERROR_UNKNOWN_ERROR;
	}

	return COMMAND_ERROR_NONE;
}

static ServerCommandError
server_ptrace_kill (ServerHandle *handle)
{
	int status;
	InferiorHandle *inferior = handle->inferior;

	if (inferior->pid) {
		ptrace (PT_KILL, inferior->pid, NULL, 0);
		kill (inferior->pid, SIGKILL);
	}
	return COMMAND_ERROR_NONE;
}

static ServerCommandError
server_ptrace_peek_word (ServerHandle *handle, guint64 start, guint32 *retval)
{
	return server_ptrace_read_memory (handle, start, sizeof (int), retval);
}

static ServerCommandError
server_ptrace_write_memory (ServerHandle *handle, guint64 start,
			    guint32 size, gconstpointer buffer)
{
	InferiorHandle *inferior = handle->inferior;
	ServerCommandError result;
	const int *ptr = buffer;
	guint64 addr = start;
	char temp [4];

	while (size >= 4) {
		int word = *ptr++;

		errno = 0;
		if (ptrace (PT_WRITE_D, inferior->pid, (gpointer) addr, word) != 0) {
			if (errno == ESRCH)
				return COMMAND_ERROR_NOT_STOPPED;
			else if (errno) {
				g_message (G_STRLOC ": %d - %s", inferior->pid, g_strerror (errno));
				return COMMAND_ERROR_UNKNOWN_ERROR;
			}
		}

		addr += sizeof (int);
		size -= sizeof (int);
	}

	if (!size)
		return COMMAND_ERROR_NONE;

	result = server_ptrace_read_memory (handle, (guint32) addr, 4, &temp);
	if (result != COMMAND_ERROR_NONE)
		return result;
	memcpy (&temp, ptr, size);

	return server_ptrace_write_memory (handle, (guint32) addr, 4, &temp);
}	


static ServerStatusMessageType
server_ptrace_dispatch_event (ServerHandle *handle, guint32 status, guint64 *arg,
			      guint64 *data1, guint64 *data2)
{
	if (status >> 16) {
		switch (status >> 16) {
		case PTRACE_EVENT_CLONE: {
			int new_pid;

			if (ptrace (PTRACE_GETEVENTMSG, handle->inferior->pid, 0, &new_pid)) {
				g_warning (G_STRLOC ": %d - %s", handle->inferior->pid,
					   g_strerror (errno));
				return FALSE;
			}

			*arg = new_pid;
			return MESSAGE_CHILD_CREATED_THREAD;
		}

		default:
			g_warning (G_STRLOC ": Received unknown wait result %x on child %d",
				   status, handle->inferior->pid);
			return MESSAGE_UNKNOWN_ERROR;
		}
	}

	if (WIFSTOPPED (status)) {
		guint64 callback_arg, retval, retval2;
		ChildStoppedAction action;

		action = x86_arch_child_stopped (handle, WSTOPSIG (status),
						 &callback_arg, &retval, &retval2);

		switch (action) {
		case STOP_ACTION_SEND_STOPPED:
			if (WSTOPSIG (status) == SIGTRAP) {
				handle->inferior->last_signal = 0;
				*arg = 0;
			} else if (WSTOPSIG (status) == 32) {
				handle->inferior->last_signal = WSTOPSIG (status);
				return MESSAGE_NONE;
			} else {
				if (WSTOPSIG (status) == SIGSTOP)
					handle->inferior->last_signal = 0;
				else
					handle->inferior->last_signal = WSTOPSIG (status);
				*arg = handle->inferior->last_signal;
			}
			return MESSAGE_CHILD_STOPPED;

		case STOP_ACTION_BREAKPOINT_HIT:
			*arg = (int) retval;
			return MESSAGE_CHILD_HIT_BREAKPOINT;

		case STOP_ACTION_CALLBACK:
			*arg = callback_arg;
			*data1 = retval;
			*data2 = retval2;
			return MESSAGE_CHILD_CALLBACK;

		case STOP_ACTION_NOTIFICATION:
			*arg = callback_arg;
			*data1 = retval;
			*data2 = retval2;
			return MESSAGE_CHILD_NOTIFICATION;
		}

		g_assert_not_reached ();
	} else if (WIFEXITED (status)) {
		*arg = WEXITSTATUS (status);
		return MESSAGE_CHILD_EXITED;
	} else if (WIFSIGNALED (status)) {
		*arg = WTERMSIG (status);
		return MESSAGE_CHILD_SIGNALED;
	}

	g_warning (G_STRLOC ": Got unknown waitpid() result: %x", status);
	return MESSAGE_UNKNOWN_ERROR;
}

static ServerHandle *
server_ptrace_initialize (BreakpointManager *bpm)
{
	ServerHandle *handle = g_new0 (ServerHandle, 1);

	handle->bpm = bpm;
	handle->inferior = g_new0 (InferiorHandle, 1);
	handle->arch = x86_arch_initialize ();
	return handle;
}

static void
child_setup_func (InferiorHandle *inferior)
{
	if (ptrace (PT_TRACE_ME, getpid (), NULL, 0))
		g_error (G_STRLOC ": Can't PT_TRACEME: %s", g_strerror (errno));

	dup2 (inferior->output_fd[1], 1);
	dup2 (inferior->error_fd[1], 2);
}

static ServerCommandError
server_ptrace_spawn (ServerHandle *handle, const gchar *working_directory,
		     const gchar **argv, const gchar **envp, gint *child_pid,
		     ChildOutputFunc stdout_handler, ChildOutputFunc stderr_handler,
		     gchar **error)
{	
	InferiorHandle *inferior = handle->inferior;
	int fd[2], open_max, ret, len, i;

	*error = NULL;

	pipe (fd);

	inferior->stdout_handler = stdout_handler;
	inferior->stderr_handler = stderr_handler;

	pipe (inferior->output_fd);
	pipe (inferior->error_fd);

	*child_pid = fork ();
	if (*child_pid == 0) {
		gchar *error_message;

		open_max = sysconf (_SC_OPEN_MAX);
		for (i = 3; i < open_max; i++)
			fcntl (i, F_SETFD, FD_CLOEXEC);

		setsid ();

		child_setup_func (inferior);
		execve (argv [0], (char **) argv, (char **) envp);

		error_message = g_strdup_printf ("Cannot exec `%s': %s", argv [0], g_strerror (errno));
		len = strlen (error_message) + 1;
		write (fd [1], &len, sizeof (len));
		write (fd [1], error_message, len);
		_exit (1);
	}

	close (inferior->output_fd[1]);
	close (inferior->error_fd[1]);
	close (fd [1]);

	ret = read (fd [0], &len, sizeof (len));

	if (ret != 0) {
		g_assert (ret == 4);

		*error = g_malloc0 (len);
		read (fd [0], *error, len);
		close (fd [0]);
		close (inferior->output_fd[0]);
		close (inferior->error_fd[0]);
		return COMMAND_ERROR_CANNOT_START_TARGET;
	}

	inferior->pid = *child_pid;
	_server_ptrace_setup_inferior (handle, TRUE);
	_server_ptrace_setup_thread_manager (handle);

	mono_thread_create (mono_get_root_domain (), io_thread, inferior);

	return COMMAND_ERROR_NONE;
}

static ServerCommandError
server_ptrace_attach (ServerHandle *handle, guint32 pid, guint32 *tid)
{
	InferiorHandle *inferior = handle->inferior;

	ptrace (PT_ATTACH, pid, NULL, 0);

	inferior->pid = pid;
	inferior->is_thread = TRUE;

	_server_ptrace_setup_inferior (handle, FALSE);

	*tid = inferior->tid;

	return COMMAND_ERROR_NONE;
}

static void
process_output (InferiorHandle *inferior, int fd, ChildOutputFunc func)
{
	char buffer [BUFSIZ + 1];
	int count;

	count = read (fd, buffer, BUFSIZ);
	if (count < 0)
		return;

	buffer [count] = 0;
	func (buffer);
}

static guint32
io_thread (gpointer data)
{
	InferiorHandle *inferior = (InferiorHandle*)data;
	struct pollfd fds [2];
	int ret;

	fds [0].fd = inferior->output_fd [0];
	fds [0].events = POLLIN | POLLHUP | POLLERR;
	fds [0].revents = 0;
	fds [1].fd = inferior->error_fd [0];
	fds [1].events = POLLIN | POLLHUP | POLLERR;
	fds [1].revents = 0;

	while (1) {
		ret = poll (fds, 2, -1);

		if (fds [0].revents & POLLIN)
			process_output (inferior, inferior->output_fd [0], inferior->stdout_handler);
		if (fds [1].revents & POLLIN)
			process_output (inferior, inferior->error_fd [0], inferior->stderr_handler);

		if ((fds [0].revents & (POLLHUP | POLLERR))
		    || (fds [1].revents & (POLLHUP | POLLERR)))
			break;
	}

	return 0;
}

static ServerCommandError
server_ptrace_set_signal (ServerHandle *handle, guint32 sig, guint32 send_it)
{
	if (send_it)
		kill (handle->inferior->pid, sig);
	else
		handle->inferior->last_signal = sig;
	return COMMAND_ERROR_NONE;
}

extern void GC_start_blocking (void);
extern void GC_end_blocking (void);

#ifdef __linux__
#include "x86-linux-ptrace.c"
#endif

#ifdef __FreeBSD__
#include "x86-freebsd-ptrace.c"
#endif

#if defined(__i386__)
#include "i386-arch.c"
#elif defined(__x86_64__)
#include "x86_64-arch.c"
#else
#error "Unknown architecture"
#endif

InferiorVTable i386_ptrace_inferior = {
	server_ptrace_global_init,
	server_ptrace_initialize,
	server_ptrace_spawn,
	server_ptrace_attach,
	server_ptrace_detach,
	server_ptrace_finalize,
	server_ptrace_global_wait,
	server_ptrace_stop_and_wait,
	server_ptrace_dispatch_event,
	server_ptrace_get_target_info,
	server_ptrace_continue,
	server_ptrace_step,
	server_ptrace_get_frame,
	server_ptrace_current_insn_is_bpt,
	server_ptrace_peek_word,
	server_ptrace_read_memory,
	server_ptrace_write_memory,
	server_ptrace_call_method,
	server_ptrace_call_method_1,
	server_ptrace_call_method_invoke,
	server_ptrace_insert_breakpoint,
	server_ptrace_insert_hw_breakpoint,
	server_ptrace_remove_breakpoint,
	server_ptrace_enable_breakpoint,
	server_ptrace_disable_breakpoint,
	server_ptrace_get_breakpoints,
	server_ptrace_get_registers,
	server_ptrace_set_registers,
	server_ptrace_get_backtrace,
	server_ptrace_get_ret_address,
	server_ptrace_stop,
	server_ptrace_global_stop,
	server_ptrace_set_signal,
	server_ptrace_kill,
	server_ptrace_get_signal_info,
	server_ptrace_set_notification
};