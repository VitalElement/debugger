#define _GNU_SOURCE
#include <server.h>
#include <breakpoints.h>
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "i386-linux-ptrace.h"
#include "i386-arch.h"

struct ArchInfo
{
	long call_address;
	guint64 callback_argument;
	INFERIOR_REGS_TYPE current_regs;
	INFERIOR_FPREGS_TYPE current_fpregs;
	INFERIOR_REGS_TYPE *saved_regs;
	INFERIOR_FPREGS_TYPE *saved_fpregs;
	GPtrArray *rti_stack;
	unsigned dr_control, dr_status;
};

typedef struct
{
	INFERIOR_REGS_TYPE *saved_regs;
	INFERIOR_FPREGS_TYPE *saved_fpregs;
	long call_address;
	guint64 callback_argument;
} RuntimeInvokeData;

ArchInfo *
i386_arch_initialize (void)
{
	ArchInfo *arch = g_new0 (ArchInfo, 1);

	arch->rti_stack = g_ptr_array_new ();

	return arch;
}

void
i386_arch_finalize (ArchInfo *arch)
{
	g_ptr_array_free (arch->rti_stack, TRUE);
	g_free (arch->saved_regs);
     	g_free (arch->saved_fpregs);
	g_free (arch);
}

ServerCommandError
mono_debugger_server_current_insn_is_bpt (ServerHandle *handle, guint32 *is_breakpoint)
{
	mono_debugger_breakpoint_manager_lock (handle->bpm);
	if (mono_debugger_breakpoint_manager_lookup (handle->bpm, INFERIOR_REG_EIP (handle->arch->current_regs)))
		*is_breakpoint = TRUE;
	else
		*is_breakpoint = FALSE;
	mono_debugger_breakpoint_manager_unlock (handle->bpm);

	return COMMAND_ERROR_NONE;
}

void
i386_arch_remove_breakpoints_from_target_memory (ServerHandle *handle, guint64 start,
						 guint32 size, gpointer buffer)
{
	GPtrArray *breakpoints;
	guint8 *ptr = buffer;
	int i;

	mono_debugger_breakpoint_manager_lock (handle->bpm);

	breakpoints = mono_debugger_breakpoint_manager_get_breakpoints (handle->bpm);
	for (i = 0; i < breakpoints->len; i++) {
		I386BreakpointInfo *info = g_ptr_array_index (breakpoints, i);
		guint32 offset;

		if (info->info.is_hardware_bpt || !info->info.enabled)
			continue;
		if ((info->info.address < start) || (info->info.address >= start+size))
			continue;

		offset = (guint32) info->info.address - start;
		ptr [offset] = info->saved_insn;
	}

	mono_debugger_breakpoint_manager_unlock (handle->bpm);
}

ServerCommandError
mono_debugger_server_get_pc (ServerHandle *handle, guint64 *pc)
{
	*pc = (guint32) INFERIOR_REG_EIP (handle->arch->current_regs);
	return COMMAND_ERROR_NONE;
}

/*
 * This method is highly architecture and specific.
 * It will only work on the i386.
 */

ServerCommandError
mono_debugger_server_call_method (ServerHandle *handle, guint64 method_address,
				  guint64 method_argument1, guint64 method_argument2,
				  guint64 callback_argument)
{
	ServerCommandError result = COMMAND_ERROR_NONE;
	ArchInfo *arch = handle->arch;
	long new_esp, call_disp;

	guint8 code[] = { 0x68, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00,
			  0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x00, 0x68,
			  0x00, 0x00, 0x00, 0x00, 0xe8, 0x00, 0x00, 0x00,
			  0x00, 0xcc };
	int size = sizeof (code);

	if (arch->saved_regs)
		return COMMAND_ERROR_RECURSIVE_CALL;

	new_esp = INFERIOR_REG_ESP (arch->current_regs) - size;

	arch->saved_regs = g_memdup (&arch->current_regs, sizeof (arch->current_regs));
	arch->saved_fpregs = g_memdup (&arch->current_fpregs, sizeof (arch->current_fpregs));
	arch->call_address = new_esp + 26;
	arch->callback_argument = callback_argument;

	call_disp = (int) method_address - new_esp;

	*((guint32 *) (code+1)) = method_argument2 >> 32;
	*((guint32 *) (code+6)) = method_argument2 & 0xffffffff;
	*((guint32 *) (code+11)) = method_argument1 >> 32;
	*((guint32 *) (code+16)) = method_argument1 & 0xffffffff;
	*((guint32 *) (code+21)) = call_disp - 25;

	mono_debugger_server_write_memory (handle, (unsigned long) new_esp, size, code);
	if (result != COMMAND_ERROR_NONE)
		return result;

	INFERIOR_REG_ESP (arch->current_regs) = INFERIOR_REG_EIP (arch->current_regs) = new_esp;

	result = _mono_debugger_server_set_registers (handle->inferior, &arch->current_regs);
	if (result != COMMAND_ERROR_NONE)
		return result;

	return mono_debugger_server_continue (handle);
}

/*
 * This method is highly architecture and specific.
 * It will only work on the i386.
 */

ServerCommandError
mono_debugger_server_call_method_1 (ServerHandle *handle, guint64 method_address,
				    guint64 method_argument, const gchar *string_argument,
				    guint64 callback_argument)
{
	ServerCommandError result = COMMAND_ERROR_NONE;
	ArchInfo *arch = handle->arch;
	long new_esp, call_disp;

	static guint8 static_code[] = { 0x68, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00,
					0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x00, 0xe8,
					0x00, 0x00, 0x00, 0x00, 0xcc };
	int static_size = sizeof (static_code);
	int size = static_size + strlen (string_argument) + 1;
	guint8 *code = g_malloc0 (size);
	memcpy (code, static_code, static_size);
	strcpy (code + static_size, string_argument);

	if (arch->saved_regs)
		return COMMAND_ERROR_RECURSIVE_CALL;

	new_esp = INFERIOR_REG_ESP (arch->current_regs) - size;

	arch->saved_regs = g_memdup (&arch->current_regs, sizeof (arch->current_regs));
	arch->saved_fpregs = g_memdup (&arch->current_fpregs, sizeof (arch->current_fpregs));
	arch->call_address = new_esp + 21;
	arch->callback_argument = callback_argument;

	call_disp = (int) method_address - new_esp;

	*((guint32 *) (code+1)) = new_esp + 21;
	*((guint32 *) (code+6)) = method_argument >> 32;
	*((guint32 *) (code+11)) = method_argument & 0xffffffff;
	*((guint32 *) (code+16)) = call_disp - 20;

	result = mono_debugger_server_write_memory (handle, (unsigned long) new_esp, size, code);
	if (result != COMMAND_ERROR_NONE)
		return result;

	INFERIOR_REG_ESP (arch->current_regs) = INFERIOR_REG_EIP (arch->current_regs) = new_esp;

	result = _mono_debugger_server_set_registers (handle->inferior, &arch->current_regs);
	if (result != COMMAND_ERROR_NONE)
		return result;

	return mono_debugger_server_continue (handle);
}

ServerCommandError
mono_debugger_server_call_method_invoke (ServerHandle *handle, guint64 invoke_method,
					 guint64 method_argument, guint64 object_argument,
					 guint32 num_params, guint64 *param_data,
					 guint64 callback_argument)
{
	ServerCommandError result = COMMAND_ERROR_NONE;
	ArchInfo *arch = handle->arch;
	RuntimeInvokeData *rdata;
	long new_esp, call_disp;
	int i;

	static guint8 static_code[] = { 0x68, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00,
					0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x00, 0x68,
					0x00, 0x00, 0x00, 0x00, 0xe8, 0x00, 0x00, 0x00,
					0x00, 0xe8, 0x00, 0x00, 0x00, 0x00, 0x5a, 0x8d,
					0x92, 0x00, 0x00, 0x00, 0x00, 0x8b, 0x12, 0x31,
					0xdb, 0x31, 0xc9, 0xcc };
	int static_size = sizeof (static_code);
	int size = static_size + (num_params + 2) * 4;
	guint8 *code = g_malloc0 (size);
	guint32 *ptr = (guint32 *) (code + static_size);
	memcpy (code, static_code, static_size);

	for (i = 0; i < num_params; i++)
		ptr [i] = param_data [i];
	ptr [num_params] = 0;

	if (arch->saved_regs)
		return COMMAND_ERROR_RECURSIVE_CALL;

	new_esp = INFERIOR_REG_ESP (arch->current_regs) - size;

	rdata = g_new0 (RuntimeInvokeData, 1);
	rdata->saved_regs = g_memdup (&arch->current_regs, sizeof (arch->current_regs));
	rdata->saved_fpregs = g_memdup (&arch->current_fpregs, sizeof (arch->current_fpregs));
	rdata->call_address = new_esp + static_size;
	rdata->callback_argument = callback_argument;

	call_disp = (int) invoke_method - new_esp;

	*((guint32 *) (code+1)) = new_esp + static_size + (num_params + 1) * 4;
	*((guint32 *) (code+6)) = new_esp + static_size;
	*((guint32 *) (code+11)) = object_argument;
	*((guint32 *) (code+16)) = method_argument;
	*((guint32 *) (code+21)) = call_disp - 25;
	*((guint32 *) (code+33)) = 14 + (num_params + 1) * 4;

	result = mono_debugger_server_write_memory (handle, (unsigned long) new_esp, size, code);
	if (result != COMMAND_ERROR_NONE)
		return result;

	INFERIOR_REG_ESP (arch->current_regs) = INFERIOR_REG_EIP (arch->current_regs) = new_esp;
	g_ptr_array_add (arch->rti_stack, rdata);

	result = _mono_debugger_server_set_registers (handle->inferior, &arch->current_regs);
	if (result != COMMAND_ERROR_NONE)
		return result;

	return mono_debugger_server_continue (handle);
}

static gboolean
check_breakpoint (ServerHandle *handle, guint64 address, guint64 *retval)
{
	I386BreakpointInfo *info;

	mono_debugger_breakpoint_manager_lock (handle->bpm);
	info = (I386BreakpointInfo *) mono_debugger_breakpoint_manager_lookup (handle->bpm, address);
	if (!info || !info->info.enabled) {
		mono_debugger_breakpoint_manager_unlock (handle->bpm);
		return FALSE;
	}

	*retval = info->info.id;
	mono_debugger_breakpoint_manager_unlock (handle->bpm);
	return TRUE;
}

static RuntimeInvokeData *
get_runtime_invoke_data (ArchInfo *arch)
{
	if (!arch->rti_stack->len)
		return NULL;

	return g_ptr_array_index (arch->rti_stack, arch->rti_stack->len - 1);
}

void
i386_arch_get_registers (ServerHandle *handle)
{
	if (_mono_debugger_server_get_registers (handle->inferior, &handle->arch->current_regs) != COMMAND_ERROR_NONE)
		g_error (G_STRLOC ": Can't get registers");
	if (_mono_debugger_server_get_fp_registers (handle->inferior, &handle->arch->current_fpregs) != COMMAND_ERROR_NONE)
		g_error (G_STRLOC ": Can't get fp registers");
}

ChildStoppedAction
i386_arch_child_stopped (ServerHandle *handle, int stopsig,
			 guint64 *callback_arg, guint64 *retval, guint64 *retval2)
{
	ArchInfo *arch = handle->arch;
	InferiorHandle *inferior = handle->inferior;
	RuntimeInvokeData *rdata;

	i386_arch_get_registers (handle);

	if (check_breakpoint (handle, INFERIOR_REG_EIP (arch->current_regs) - 1, retval)) {
		INFERIOR_REG_EIP (arch->current_regs)--;
		_mono_debugger_server_set_registers (inferior, &arch->current_regs);
		return STOP_ACTION_BREAKPOINT_HIT;
	}

	rdata = get_runtime_invoke_data (arch);
	if (rdata && (rdata->call_address == INFERIOR_REG_EIP (arch->current_regs))) {
		if (_mono_debugger_server_set_registers (inferior, rdata->saved_regs) != COMMAND_ERROR_NONE)
			g_error (G_STRLOC ": Can't restore registers after returning from a call");

		if (_mono_debugger_server_set_fp_registers (inferior, rdata->saved_fpregs) != COMMAND_ERROR_NONE)
			g_error (G_STRLOC ": Can't restore FP registers after returning from a call");

		*callback_arg = rdata->callback_argument;
		*retval = (((guint64) INFERIOR_REG_ECX (arch->current_regs)) << 32) + ((gulong) INFERIOR_REG_EAX (arch->current_regs));
		*retval2 = (((guint64) INFERIOR_REG_EBX (arch->current_regs)) << 32) + ((gulong) INFERIOR_REG_EDX (arch->current_regs));

		g_free (rdata->saved_regs);
		g_free (rdata->saved_fpregs);
		g_ptr_array_remove (arch->rti_stack, rdata);
		g_free (rdata);

		i386_arch_get_registers (handle);

		return STOP_ACTION_CALLBACK;
	}

	if (!arch->call_address || arch->call_address != INFERIOR_REG_EIP (arch->current_regs)) {
		int code;

#if defined(__linux__) || defined(__FreeBSD__)
		if (stopsig != SIGTRAP)
			return STOP_ACTION_SEND_STOPPED;
#endif

		if (mono_debugger_server_peek_word (handle, (guint32) (INFERIOR_REG_EIP (arch->current_regs) - 1), &code) != COMMAND_ERROR_NONE)
			return STOP_ACTION_SEND_STOPPED;

		if ((code & 0xff) == 0xcc) {
			*retval = 0;
			return STOP_ACTION_BREAKPOINT_HIT;
		}

		return STOP_ACTION_SEND_STOPPED;
	}

	if (_mono_debugger_server_set_registers (inferior, arch->saved_regs) != COMMAND_ERROR_NONE)
		g_error (G_STRLOC ": Can't restore registers after returning from a call");

	if (_mono_debugger_server_set_fp_registers (inferior, arch->saved_fpregs) != COMMAND_ERROR_NONE)
		g_error (G_STRLOC ": Can't restore FP registers after returning from a call");

	*callback_arg = arch->callback_argument;
	*retval = (((guint64) INFERIOR_REG_ECX (arch->current_regs)) << 32) + ((gulong) INFERIOR_REG_EAX (arch->current_regs));
	*retval2 = (((guint64) INFERIOR_REG_EBX (arch->current_regs)) << 32) + ((gulong) INFERIOR_REG_EDX (arch->current_regs));

	g_free (arch->saved_regs);
	g_free (arch->saved_fpregs);

	arch->saved_regs = NULL;
	arch->saved_fpregs = NULL;
	arch->call_address = 0;
	arch->callback_argument = 0;

	i386_arch_get_registers (handle);

	return STOP_ACTION_CALLBACK;
}

ServerCommandError
mono_debugger_server_get_target_info (ServerHandle *handle, guint32 *target_int_size,
				      guint32 *target_long_size, guint32 *target_address_size)
{
	*target_int_size = sizeof (guint32);
	*target_long_size = sizeof (guint64);
	*target_address_size = sizeof (void *);

	return COMMAND_ERROR_NONE;
}

ServerCommandError
mono_debugger_server_get_registers (ServerHandle *handle, guint32 count,
				    guint32 *registers, guint64 *values)
{
	ArchInfo *arch = handle->arch;
	int i;

	for (i = 0; i < count; i++) {
		switch (registers [i]) {
		case DEBUGGER_REG_EBX:
			values [i] = (guint32) INFERIOR_REG_EBX (arch->current_regs);
			break;
		case DEBUGGER_REG_ECX:
			values [i] = (guint32) INFERIOR_REG_ECX (arch->current_regs);
			break;
		case DEBUGGER_REG_EDX:
			values [i] = (guint32) INFERIOR_REG_EDX (arch->current_regs);
			break;
		case DEBUGGER_REG_ESI:
			values [i] = (guint32) INFERIOR_REG_ESI (arch->current_regs);
			break;
		case DEBUGGER_REG_EDI:
			values [i] = (guint32) INFERIOR_REG_EDI (arch->current_regs);
			break;
		case DEBUGGER_REG_EBP:
			values [i] = (guint32) INFERIOR_REG_EBP (arch->current_regs);
			break;
		case DEBUGGER_REG_EAX:
			values [i] = (guint32) INFERIOR_REG_EAX (arch->current_regs);
			break;
		case DEBUGGER_REG_DS:
			values [i] = (guint32) INFERIOR_REG_DS (arch->current_regs);
			break;
		case DEBUGGER_REG_ES:
			values [i] = (guint32) INFERIOR_REG_ES (arch->current_regs);
			break;
		case DEBUGGER_REG_FS:
			values [i] = (guint32) INFERIOR_REG_FS (arch->current_regs);
			break;
		case DEBUGGER_REG_GS:
			values [i] = (guint32) INFERIOR_REG_GS (arch->current_regs);
			break;
		case DEBUGGER_REG_EIP:
			values [i] = (guint32) INFERIOR_REG_EIP (arch->current_regs);
			break;
		case DEBUGGER_REG_CS:
			values [i] = (guint32) INFERIOR_REG_CS (arch->current_regs);
			break;
		case DEBUGGER_REG_EFLAGS:
			values [i] = (guint32) INFERIOR_REG_EFLAGS (arch->current_regs);
			break;
		case DEBUGGER_REG_ESP:
			values [i] = (guint32) INFERIOR_REG_ESP (arch->current_regs);
			break;
		case DEBUGGER_REG_SS:
			values [i] = (guint32) INFERIOR_REG_SS (arch->current_regs);
			break;
		default:
			return COMMAND_ERROR_UNKNOWN_REGISTER;
		}
	}

	return COMMAND_ERROR_NONE;
}

ServerCommandError
mono_debugger_server_set_registers (ServerHandle *handle, guint32 count,
				    guint32 *registers, guint64 *values)
{
	ArchInfo *arch = handle->arch;
	int i;

	for (i = 0; i < count; i++) {
		switch (registers [i]) {
		case DEBUGGER_REG_EBX:
			INFERIOR_REG_EBX (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_ECX:
			INFERIOR_REG_ECX (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_EDX:
			INFERIOR_REG_EDX (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_ESI:
			INFERIOR_REG_ESI (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_EDI:
			INFERIOR_REG_EDI (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_EBP:
			INFERIOR_REG_EBP (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_EAX:
			INFERIOR_REG_EAX (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_DS:
			INFERIOR_REG_DS (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_ES:
			INFERIOR_REG_ES (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_FS:
			INFERIOR_REG_FS (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_GS:
			INFERIOR_REG_GS (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_EIP:
			INFERIOR_REG_EIP (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_CS:
			INFERIOR_REG_CS (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_EFLAGS:
			INFERIOR_REG_EFLAGS (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_ESP:
			INFERIOR_REG_ESP (arch->current_regs) = values [i];
			break;
		case DEBUGGER_REG_SS:
			INFERIOR_REG_SS (arch->current_regs) = values [i];
			break;
		default:
			return COMMAND_ERROR_UNKNOWN_REGISTER;
		}
	}

	return _mono_debugger_server_set_registers (handle->inferior, &arch->current_regs);
}

ServerCommandError
i386_arch_get_frame (ServerHandle *handle, guint32 eip,
		     guint32 esp, guint32 ebp, guint32 *retaddr, guint32 *frame)
{
	ServerCommandError result;
	guint32 value;

	result = mono_debugger_server_peek_word (handle, eip, &value);
	if (result != COMMAND_ERROR_NONE)
		return result;

	if ((value == 0xec8b5590) || (value == 0xec8b55cc) ||
	    ((value & 0xffffff) == 0xec8b55) || ((value & 0xffffff) == 0xe58955)) {
		result = mono_debugger_server_peek_word (handle, esp, &value);
		if (result != COMMAND_ERROR_NONE)
			return result;

		*retaddr = value;
		*frame = ebp;
		return COMMAND_ERROR_NONE;
	}

	result = mono_debugger_server_peek_word (handle, eip - 1, &value);
	if (result != COMMAND_ERROR_NONE)
		return result;

	if (((value & 0xffffff) == 0xec8b55) || ((value & 0xffffff) == 0xe58955)) {
		result = mono_debugger_server_peek_word (handle, esp + 4, &value);
		if (result != COMMAND_ERROR_NONE)
			return result;

		*retaddr = value;
		*frame = ebp;
		return COMMAND_ERROR_NONE;
	}

	result = mono_debugger_server_peek_word (handle, ebp, frame);
	if (result != COMMAND_ERROR_NONE)
		return result;

	result = mono_debugger_server_peek_word (handle, ebp + 4, retaddr);
	if (result != COMMAND_ERROR_NONE)
		return result;

	return COMMAND_ERROR_NONE;
}

ServerCommandError
mono_debugger_server_get_ret_address (ServerHandle *handle, guint64 *retval)
{
	ServerCommandError result;
	ArchInfo *arch = handle->arch;
	guint32 retaddr, frame;

	result = i386_arch_get_frame (handle, INFERIOR_REG_EIP (arch->current_regs),
				      INFERIOR_REG_ESP (arch->current_regs),
				      INFERIOR_REG_EBP (arch->current_regs),
				      &retaddr, &frame);
	if (result != COMMAND_ERROR_NONE)
		return result;

	*retval = retaddr;
	return COMMAND_ERROR_NONE;
}

ServerCommandError
mono_debugger_server_get_backtrace (ServerHandle *handle, gint32 max_frames,
				    guint64 stop_address, guint32 *count, StackFrame **data)
{
	GArray *frames = g_array_new (FALSE, FALSE, sizeof (StackFrame));
	ServerCommandError result = COMMAND_ERROR_NONE;
	ArchInfo *arch = handle->arch;
	guint32 address, frame;
	StackFrame sframe;
	int i;

	sframe.address = (guint32) INFERIOR_REG_EIP (arch->current_regs);
	sframe.frame_address = (guint32) INFERIOR_REG_EBP (arch->current_regs);

	g_array_append_val (frames, sframe);

	if (INFERIOR_REG_EBP (arch->current_regs) == 0)
		goto out;

	result = i386_arch_get_frame (handle, INFERIOR_REG_EIP (arch->current_regs),
				      INFERIOR_REG_ESP (arch->current_regs),
				      INFERIOR_REG_EBP (arch->current_regs),
				      &address, &frame);
	if (result != COMMAND_ERROR_NONE)
		goto out;

	while (frame != 0) {
		if ((max_frames >= 0) && (frames->len >= max_frames))
			break;

		if (address == stop_address)
			goto out;

		sframe.address = address;
		sframe.frame_address = frame;

		g_array_append_val (frames, sframe);

		result = mono_debugger_server_peek_word (handle, frame + 4, &address);
		if (result != COMMAND_ERROR_NONE)
			goto out;

		result = mono_debugger_server_peek_word (handle, frame, &frame);
		if (result != COMMAND_ERROR_NONE)
			goto out;
	}

	goto out;

 out:
	*count = frames->len;
	*data = g_new0 (StackFrame, frames->len);
	for (i = 0; i < frames->len; i++)
		(*data)[i] = g_array_index (frames, StackFrame, i);
	g_array_free (frames, FALSE);
	return result;
}

static ServerCommandError
do_enable (ServerHandle *handle, I386BreakpointInfo *breakpoint)
{
	ServerCommandError result;
	ArchInfo *arch = handle->arch;
	InferiorHandle *inferior = handle->inferior;
	char bopcode = 0xcc;
	guint32 address;

	if (breakpoint->info.enabled)
		return COMMAND_ERROR_NONE;

	address = (guint32) breakpoint->info.address;

	if (breakpoint->dr_index >= 0) {
		I386_DR_SET_RW_LEN (arch, breakpoint->dr_index, DR_RW_EXECUTE | DR_LEN_1);
		I386_DR_LOCAL_ENABLE (arch, breakpoint->dr_index);

		result = _mono_debugger_server_set_dr (inferior, breakpoint->dr_index, address);
		if (result != COMMAND_ERROR_NONE)
			return result;

		result = _mono_debugger_server_set_dr (inferior, DR_CONTROL, arch->dr_control);
		if (result != COMMAND_ERROR_NONE)
			return result;
	} else {
		result = mono_debugger_server_read_memory (handle, address, 1, &breakpoint->saved_insn);
		if (result != COMMAND_ERROR_NONE)
			return result;

		result = mono_debugger_server_write_memory (handle, address, 1, &bopcode);
		if (result != COMMAND_ERROR_NONE)
			return result;
	}

	breakpoint->info.enabled = TRUE;

	return COMMAND_ERROR_NONE;
}

static ServerCommandError
do_disable (ServerHandle *handle, I386BreakpointInfo *breakpoint)
{
	ServerCommandError result;
	ArchInfo *arch = handle->arch;
	InferiorHandle *inferior = handle->inferior;
	guint32 address;

	if (!breakpoint->info.enabled)
		return COMMAND_ERROR_NONE;

	address = (guint32) breakpoint->info.address;

	if (breakpoint->dr_index >= 0) {
		I386_DR_DISABLE (arch, breakpoint->dr_index);

		result = _mono_debugger_server_set_dr (inferior, breakpoint->dr_index, 0L);
		if (result != COMMAND_ERROR_NONE)
			return result;

		result = _mono_debugger_server_set_dr (inferior, DR_CONTROL, arch->dr_control);
		if (result != COMMAND_ERROR_NONE)
			return result;
	} else {
		result = mono_debugger_server_write_memory (handle, address, 1, &breakpoint->saved_insn);
		if (result != COMMAND_ERROR_NONE)
			return result;
	}

	breakpoint->info.enabled = FALSE;

	return COMMAND_ERROR_NONE;
}

ServerCommandError
mono_debugger_server_insert_breakpoint (ServerHandle *handle, guint64 address, guint32 *bhandle)
{
	I386BreakpointInfo *breakpoint;
	ServerCommandError result;

	mono_debugger_breakpoint_manager_lock (handle->bpm);
	breakpoint = (I386BreakpointInfo *) mono_debugger_breakpoint_manager_lookup (handle->bpm, address);
	if (breakpoint && !breakpoint->info.is_hardware_bpt) {
		breakpoint->info.refcount++;
		goto done;
	}

	breakpoint = g_new0 (I386BreakpointInfo, 1);

	breakpoint->info.refcount = 1;
	breakpoint->info.address = address;
	breakpoint->info.is_hardware_bpt = FALSE;
	breakpoint->info.id = mono_debugger_breakpoint_manager_get_next_id ();
	breakpoint->dr_index = -1;

	result = do_enable (handle, breakpoint);
	if (result != COMMAND_ERROR_NONE) {
		mono_debugger_breakpoint_manager_unlock (handle->bpm);
		g_free (breakpoint);
		return result;
	}

	mono_debugger_breakpoint_manager_insert (handle->bpm, (BreakpointInfo *) breakpoint);
 done:
	*bhandle = breakpoint->info.id;
	mono_debugger_breakpoint_manager_unlock (handle->bpm);

	return COMMAND_ERROR_NONE;
}

ServerCommandError
mono_debugger_server_remove_breakpoint (ServerHandle *handle, guint32 bhandle)
{
	I386BreakpointInfo *breakpoint;
	ServerCommandError result;

	mono_debugger_breakpoint_manager_lock (handle->bpm);
	breakpoint = (I386BreakpointInfo *) mono_debugger_breakpoint_manager_lookup_by_id (handle->bpm, bhandle);
	if (!breakpoint) {
		result = COMMAND_ERROR_NO_SUCH_BREAKPOINT;
		goto out;
	}

	result = do_disable (handle, breakpoint);
	if (result != COMMAND_ERROR_NONE)
		goto out;

	mono_debugger_breakpoint_manager_remove (handle->bpm, (BreakpointInfo *) breakpoint);

 out:
	mono_debugger_breakpoint_manager_unlock (handle->bpm);
	return result;
}

ServerCommandError
mono_debugger_server_insert_hw_breakpoint (ServerHandle *handle, guint32 idx,
					   guint64 address, guint32 *bhandle)
{
	I386BreakpointInfo *breakpoint;
	ServerCommandError result;

	if ((idx < 0) || (idx > DR_NADDR))
		return COMMAND_ERROR_DR_OCCUPIED;

	if (!I386_DR_VACANT (handle->arch, idx))
		return COMMAND_ERROR_DR_OCCUPIED;

	mono_debugger_breakpoint_manager_lock (handle->bpm);
	breakpoint = g_new0 (I386BreakpointInfo, 1);
	breakpoint->info.address = address;
	breakpoint->info.refcount = 1;
	breakpoint->info.id = mono_debugger_breakpoint_manager_get_next_id ();
	breakpoint->info.is_hardware_bpt = TRUE;
	breakpoint->dr_index = idx;

	result = do_enable (handle, breakpoint);
	if (result != COMMAND_ERROR_NONE) {
		mono_debugger_breakpoint_manager_unlock (handle->bpm);
		g_free (breakpoint);
		return result;
	}

	mono_debugger_breakpoint_manager_insert (handle->bpm, (BreakpointInfo *) breakpoint);
	*bhandle = breakpoint->info.id;
	mono_debugger_breakpoint_manager_unlock (handle->bpm);

	return COMMAND_ERROR_NONE;
}

ServerCommandError
mono_debugger_server_enable_breakpoint (ServerHandle *handle, guint32 bhandle)
{
	I386BreakpointInfo *breakpoint;
	ServerCommandError result;

	mono_debugger_breakpoint_manager_lock (handle->bpm);
	breakpoint = (I386BreakpointInfo *) mono_debugger_breakpoint_manager_lookup_by_id (handle->bpm, bhandle);
	if (!breakpoint) {
		mono_debugger_breakpoint_manager_unlock (handle->bpm);
		return COMMAND_ERROR_NO_SUCH_BREAKPOINT;
	}

	result = do_enable (handle, breakpoint);
	mono_debugger_breakpoint_manager_unlock (handle->bpm);
	return result;
}

ServerCommandError
mono_debugger_server_disable_breakpoint (ServerHandle *handle, guint32 bhandle)
{
	I386BreakpointInfo *breakpoint;
	ServerCommandError result;

	mono_debugger_breakpoint_manager_lock (handle->bpm);
	breakpoint = (I386BreakpointInfo *) mono_debugger_breakpoint_manager_lookup_by_id (handle->bpm, bhandle);
	if (!breakpoint) {
		mono_debugger_breakpoint_manager_unlock (handle->bpm);
		return COMMAND_ERROR_NO_SUCH_BREAKPOINT;
	}

	result = do_disable (handle, breakpoint);
	mono_debugger_breakpoint_manager_unlock (handle->bpm);
	return result;
}

ServerCommandError
mono_debugger_server_get_breakpoints (ServerHandle *handle, guint32 *count, guint32 **retval)
{
	int i;
	GPtrArray *breakpoints;

	mono_debugger_breakpoint_manager_lock (handle->bpm);
	breakpoints = mono_debugger_breakpoint_manager_get_breakpoints (handle->bpm);
	*count = breakpoints->len;
	*retval = g_new0 (guint32, breakpoints->len);

	for (i = 0; i < breakpoints->len; i++) {
		BreakpointInfo *info = g_ptr_array_index (breakpoints, i);

		(*retval) [i] = info->id;
	}
	mono_debugger_breakpoint_manager_unlock (handle->bpm);

	return COMMAND_ERROR_NONE;	
}
