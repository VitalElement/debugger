#include <mono-debugger-jit-wrapper.h>
#include <mono/io-layer/io-layer.h>
#include <mono/metadata/threads.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/mono-debug.h>
#define IN_MONO_DEBUGGER
#include <mono/private/libgc-mono-debugger.h>
#include <unistd.h>
#include <locale.h>
#include <string.h>

static gpointer main_started_cond;
static gpointer main_ready_cond;

#define HEAP_SIZE 1048576
static char mono_debugger_heap [HEAP_SIZE];

static MonoMethod *debugger_main_method;

static guint64 debugger_insert_breakpoint (guint64 method_argument, const gchar *string_argument);
static guint64 debugger_remove_breakpoint (guint64 breakpoint);
static guint64 debugger_compile_method (guint64 method_arg);
static guint64 debugger_get_virtual_method (guint64 class_arg, guint64 method_arg);
static guint64 debugger_get_boxed_object (guint64 klass_arg, guint64 val_arg);
static guint64 debugger_create_string (guint64 dummy_argument, const gchar *string_argument);
static guint64 debugger_class_get_static_field_data (guint64 klass);
static guint64 debugger_lookup_type (guint64 dummy_argument, const gchar *string_argument);
static guint64 debugger_lookup_assembly (guint64 dummy_argument, const gchar *string_argument);

void (*mono_debugger_notification_function) (guint64 command, guint64 data, guint64 data2);

/*
 * This is a global data symbol which is read by the debugger.
 */
MonoDebuggerInfo MONO_DEBUGGER__debugger_info = {
	MONO_DEBUGGER_MAGIC,
	MONO_DEBUGGER_VERSION,
	sizeof (MonoDebuggerInfo),
	sizeof (MonoSymbolTable),
	HEAP_SIZE,
	mono_trampoline_code,
	&mono_symbol_table,
	&debugger_compile_method,
	&debugger_get_virtual_method,
	&debugger_get_boxed_object,
	&debugger_insert_breakpoint,
	&debugger_remove_breakpoint,
	&mono_debugger_runtime_invoke,
	&debugger_create_string,
	&debugger_class_get_static_field_data,
	&debugger_lookup_type,
	&debugger_lookup_assembly,
	mono_debugger_heap
};

MonoDebuggerManager MONO_DEBUGGER__manager = {
	sizeof (MonoDebuggerManager),
	sizeof (MonoDebuggerThread),
	NULL, NULL, 0, NULL
};

static guint64
debugger_insert_breakpoint (guint64 method_argument, const gchar *string_argument)
{
	MonoMethodDesc *desc;

	desc = mono_method_desc_new (string_argument, TRUE);
	if (!desc)
		return 0;

	return (guint64) mono_debugger_insert_breakpoint_full (desc);
}

static guint64
debugger_remove_breakpoint (guint64 breakpoint)
{
	return mono_debugger_remove_breakpoint (breakpoint);
}

static gpointer
debugger_compile_method_cb (MonoMethod *method)
{
	gpointer retval;

	mono_debugger_lock ();
	retval = mono_compile_method (method);
	mono_debugger_unlock ();

	mono_debugger_notification_function (NOTIFICATION_METHOD_COMPILED, GPOINTER_TO_UINT (retval), 0);

	return retval;
}

static guint64
debugger_compile_method (guint64 method_arg)
{
	MonoMethod *method = (MonoMethod *) GUINT_TO_POINTER ((gssize) method_arg);

	return GPOINTER_TO_UINT (debugger_compile_method_cb (method));
}

static guint64
debugger_get_virtual_method (guint64 object_arg, guint64 method_arg)
{
	MonoObject *object = (MonoObject *) GUINT_TO_POINTER ((gssize) object_arg);
	MonoMethod *method = (MonoMethod *) GUINT_TO_POINTER ((gssize) method_arg);

	if (mono_class_is_valuetype (mono_method_get_class (method)))
		return method_arg;

	return GPOINTER_TO_UINT (mono_object_get_virtual_method (object, method));
}

static guint64
debugger_get_boxed_object (guint64 klass_arg, guint64 val_arg)
{
	static MonoObject *last_boxed_object = NULL;
	MonoClass *klass = (MonoClass *) GUINT_TO_POINTER ((gssize) klass_arg);
	gpointer val = (gpointer) GUINT_TO_POINTER ((gssize) val_arg);
	MonoObject *boxed;

	if (!mono_class_is_valuetype (klass))
		return val_arg;

	boxed = mono_value_box (mono_domain_get (), klass, val);
	last_boxed_object = boxed; // Protect the object from being garbage collected

	return GPOINTER_TO_UINT (boxed);
}

static guint64
debugger_create_string (guint64 dummy_argument, const gchar *string_argument)
{
	return GPOINTER_TO_UINT (mono_string_new_wrapper (string_argument));
}

static guint64
debugger_lookup_type (guint64 dummy_argument, const gchar *string_argument)
{
	guint64 retval;

	mono_debugger_lock ();
	// retval = mono_debugger_lookup_type (string_argument);
	retval = -1;
	mono_debugger_unlock ();
	return retval;
}

static guint64
debugger_lookup_assembly (guint64 dummy_argument, const gchar *string_argument)
{
	gint64 retval;

	mono_debugger_lock ();
	retval = mono_debugger_lookup_assembly (string_argument);
	mono_debugger_unlock ();
	return retval;
}

static guint64
debugger_class_get_static_field_data (guint64 value)
{
	MonoClass *klass = GUINT_TO_POINTER ((guint32) value);
	MonoVTable *vtable = mono_class_vtable (mono_domain_get (), klass);
	return GPOINTER_TO_UINT (mono_vtable_get_static_field_data (vtable));
}

static void
unhandled_exception (guint64 data, guint64 arg)
{
	MonoObject *exc = (MonoObject *) GUINT_TO_POINTER ((gssize) data);
	MonoClass *klass;
	const gchar *name;

	klass = mono_object_get_class (exc);
	name = mono_class_get_name (klass);

	if (!strcmp (name, "ThreadAbortException")) {
		guint32 tid = pthread_self ();

		mono_debugger_notification_function (NOTIFICATION_THREAD_ABORT, 0, tid);
		pthread_exit (NULL);
	}

	mono_debugger_notification_function (NOTIFICATION_UNHANDLED_EXCEPTION, data, arg);
}

static void
debugger_event_handler (MonoDebuggerEvent event, guint64 data, guint64 arg)
{
	switch (event) {
	case MONO_DEBUGGER_EVENT_RELOAD_SYMTABS:
		mono_debugger_notification_function (NOTIFICATION_RELOAD_SYMTABS, 0, 0);
		break;

	case MONO_DEBUGGER_EVENT_ADD_MODULE:
		mono_debugger_notification_function (NOTIFICATION_ADD_MODULE, data, 0);
		break;

	case MONO_DEBUGGER_EVENT_BREAKPOINT:
		mono_debugger_notification_function (NOTIFICATION_JIT_BREAKPOINT, data, arg);
		break;

	case MONO_DEBUGGER_EVENT_UNHANDLED_EXCEPTION:
		unhandled_exception (data, arg);
		break;

	case MONO_DEBUGGER_EVENT_EXCEPTION:
		mono_debugger_notification_function (NOTIFICATION_HANDLE_EXCEPTION, data, arg);
		break;

	case MONO_DEBUGGER_EVENT_THROW_EXCEPTION:
		mono_debugger_notification_function (NOTIFICATION_THROW_EXCEPTION, data, arg);
		break;
	}
}

static MonoThreadCallbacks thread_callbacks = {
	&debugger_compile_method_cb,
	&mono_debugger_thread_manager_add_thread,
	&mono_debugger_thread_manager_start_resume,
	&mono_debugger_thread_manager_end_resume
};

static void
initialize_debugger_support (void)
{
	main_started_cond = IO_LAYER (CreateSemaphore) (NULL, 0, 1, NULL);
	main_ready_cond = IO_LAYER (CreateSemaphore) (NULL, 0, 1, NULL);

	mono_debugger_notification_function = mono_debugger_create_notification_function
		(&MONO_DEBUGGER__manager.notification_address);
}

typedef struct 
{
	MonoDomain *domain;
	const char *file;
} DebuggerThreadArgs;

typedef struct
{
	MonoDomain *domain;
	MonoMethod *method;
	int argc;
	char **argv;
} MainThreadArgs;

static guint32
main_thread_handler (gpointer user_data)
{
	MainThreadArgs *main_args = (MainThreadArgs *) user_data;
	int retval;

	MONO_DEBUGGER__manager.main_tid = IO_LAYER (GetCurrentThreadId) ();
	MONO_DEBUGGER__manager.main_thread = g_new0 (MonoDebuggerThread, 1);
	MONO_DEBUGGER__manager.main_thread->tid = IO_LAYER (GetCurrentThreadId) ();
	MONO_DEBUGGER__manager.main_thread->start_stack = &main_args;

	mono_debugger_thread_manager_thread_created (MONO_DEBUGGER__manager.main_thread);

	IO_LAYER (ReleaseSemaphore) (main_started_cond, 1, NULL);

	/*
	 * Wait until everything is ready.
	 */
	IO_LAYER (WaitForSingleObject) (main_ready_cond, INFINITE, FALSE);

	retval = mono_runtime_run_main (main_args->method, main_args->argc, main_args->argv, NULL);
	/*
	 * This will never return.
	 */
	mono_debugger_notification_function (NOTIFICATION_MAIN_EXITED, 0, GPOINTER_TO_UINT (retval));

	return retval;
}

int
mono_debugger_main (MonoDomain *domain, const char *file, int argc, char **argv, char **envp)
{
	MainThreadArgs main_args;
	MonoAssembly *assembly;
	MonoImage *image;

	initialize_debugger_support ();

	mono_debugger_init_icalls ();

	/*
	 * Start the debugger thread and wait until it's ready.
	 */
	mono_debug_init_1 (domain);

	assembly = mono_domain_assembly_open (domain, file);
	if (!assembly){
		fprintf (stderr, "Can not open image %s\n", file);
		exit (1);
	}

	mono_debug_init_2 (assembly);

	/*
	 * Get and compile the main function.
	 */

	image = mono_assembly_get_image (assembly);
	debugger_main_method = mono_get_method (
		image, mono_image_get_entry_point (image), NULL);
	MONO_DEBUGGER__manager.main_function = mono_compile_method (debugger_main_method);

	/*
	 * Start the main thread and wait until it's ready.
	 */

	main_args.domain = domain;
	main_args.method = debugger_main_method;
	main_args.argc = argc - 2;
	main_args.argv = argv + 2;

	mono_thread_create (domain, main_thread_handler, &main_args);
	IO_LAYER (WaitForSingleObject) (main_started_cond, INFINITE, FALSE);

	/*
	 * Initialize the thread manager.
	 */

	mono_debugger_event_handler = debugger_event_handler;
	mono_install_thread_callbacks (&thread_callbacks);
	mono_debugger_thread_manager_init ();

	/*
	 * Reload symbol tables.
	 */
	mono_debugger_notification_function (NOTIFICATION_INITIALIZE_MANAGED_CODE, 0, 0);
	mono_debugger_notification_function (NOTIFICATION_INITIALIZE_THREAD_MANAGER, 0, 0);

	mono_debugger_unlock ();

	/*
	 * Signal the main thread that it can execute the managed Main().
	 */
	IO_LAYER (ReleaseSemaphore) (main_ready_cond, 1, NULL);

	/*
	 * This will never return.
	 */
	mono_debugger_notification_function (NOTIFICATION_WRAPPER_MAIN, 0, 0);

	return 0;
}
