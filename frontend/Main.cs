using System;
using System.Configuration;
using ST = System.Threading;
using System.Runtime.InteropServices;

using Mono.Debugger;
using Mono.Debugger.Backends;

namespace Mono.Debugger.Frontend
{
	public class CommandLineInterpreter
	{
		Interpreter interpreter;
		DebuggerEngine engine;
		LineParser parser;
		const string prompt = "(mdb) ";
		int line = 0;

		ST.Thread command_thread;
		ST.Thread main_thread;

		[DllImport("monodebuggerserver")]
		static extern int mono_debugger_server_static_init ();

		[DllImport("monodebuggerserver")]
		static extern int mono_debugger_server_get_pending_sigint ();

		static CommandLineInterpreter ()
		{
			mono_debugger_server_static_init ();
		}

		internal CommandLineInterpreter (bool is_interactive, DebuggerConfiguration config,
						 DebuggerOptions options)
		{
			if (options.HasDebugFlags)
				Report.Initialize (options.DebugOutput, options.DebugFlags);
			else
				Report.Initialize ();

			interpreter = new Interpreter (true, is_interactive, config, options);
			engine = interpreter.DebuggerEngine;
			parser = new LineParser (engine);

			main_thread = new ST.Thread (new ST.ThreadStart (main_thread_main));
			main_thread.IsBackground = true;

			command_thread = new ST.Thread (new ST.ThreadStart (command_thread_main));
			command_thread.IsBackground = true;
		}

		public void MainLoop ()
		{
			string s;
			bool is_complete = true;

			parser.Reset ();
			while ((s = ReadInput (is_complete)) != null) {
				parser.Append (s);
				if (parser.IsComplete ()){
					parser.Execute ();
					parser.Reset ();
					is_complete = true;
				} else
					is_complete = false;
			}
		}

		void main_thread_main ()
		{
			while (true) {
				try {
					interpreter.ClearInterrupt ();
					MainLoop ();
				} catch (ST.ThreadAbortException) {
					ST.Thread.ResetAbort ();
				}
			}
		}

		public void RunMainLoop ()
		{
			command_thread.Start ();

			try {
				if (interpreter.Options.StartTarget)
					interpreter.Start ();

				main_thread.Start ();
				main_thread.Join ();
			} catch (ScriptingException ex) {
				interpreter.Error (ex);
			} catch (TargetException ex) {
				interpreter.Error (ex);
			} catch (Exception ex) {
				interpreter.Error ("ERROR: {0}", ex);
			} finally {
				interpreter.Exit ();
			}
		}

		public string ReadInput (bool is_complete)
		{
			++line;
		again:
			string result;
			string the_prompt = is_complete ? prompt : "... ";
			if (interpreter.Options.IsScript) {
				Console.Write (the_prompt);
				result = Console.ReadLine ();
				if (result == null)
					return null;
				if (result != "")
					;
				else if (is_complete) {
					engine.Repeat ();
					goto again;
				}
				return result;
			} else {
				result = GnuReadLine.ReadLine (the_prompt);
				if (result == null)
					return null;
				if (result != "")
					GnuReadLine.AddHistory (result);
				else if (is_complete) {
					engine.Repeat ();
					goto again;
				}
				return result;
			}
		}

		public void Error (int pos, string message)
		{
			if (interpreter.Options.IsScript) {
				// If we're reading from a script, abort.
				Console.Write ("ERROR in line {0}, column {1}: ", line, pos);
				Console.WriteLine (message);
				Environment.Exit (1);
			}
			else {
				string prefix = new String (' ', pos + prompt.Length);
				Console.WriteLine ("{0}^", prefix);
				Console.Write ("ERROR: ");
				Console.WriteLine (message);
			}
		}

		static string GetValue (ref string[] args, ref int i, string ms_val)
		{
			if (ms_val == "")
				return null;

			if (ms_val != null)
				return ms_val;

			if (i >= args.Length)
				return null;

			return args[++i];
		}

		static bool ParseDebugFlags (DebuggerOptions options, string value)
		{
			if (value == null)
				return false;

			int pos = value.IndexOf (':');
			if (pos > 0) {
				string filename = value.Substring (0, pos);
				value = value.Substring (pos + 1);
				
				options.DebugOutput = filename;
			}
			try {
				options.DebugFlags = (DebugFlags) Int32.Parse (value);
			} catch {
				return false;
			}
			options.HasDebugFlags = true;
			return true;
		}

		static bool ParseOption (DebuggerOptions debug_options,
					 string option,
					 ref string [] args,
					 ref int i,
					 ref bool args_follow_exe)
		{
			int idx = option.IndexOf (':');
			string arg, value, ms_value = null;

			if (idx == -1){
				arg = option;
			} else {
				arg = option.Substring (0, idx);
				ms_value = option.Substring (idx + 1);
			}

			switch (arg) {
			case "-args":
				if (ms_value != null) {
					Usage ();
					Environment.Exit (1);
				}
				args_follow_exe = true;
				return true;

			case "-working-directory":
			case "-cd":
				value = GetValue (ref args, ref i, ms_value);
				if (value == null) {
					Usage ();
					Environment.Exit (1);
				}
				debug_options.WorkingDirectory = value;
				return true;

			case "-debug-flags":
				value = GetValue (ref args, ref i, ms_value);
				if (!ParseDebugFlags (debug_options, value)) {
					Usage ();
					Environment.Exit (1);
				}
				return true;

			case "-jit-optimizations":
				value = GetValue (ref args, ref i, ms_value);
				if (value == null) {
					Usage ();
					Environment.Exit (1);
				}
				debug_options.JitOptimizations = value;
				return true;

			case "-jit-arg":
				value = GetValue (ref args, ref i, ms_value);
				if (ms_value == null) {
					Usage ();
					Environment.Exit (1);
				}
				if (debug_options.JitArguments != null) {
					string[] old = debug_options.JitArguments;
					string[] new_args = new string [old.Length + 1];
					old.CopyTo (new_args, 0);
					new_args [old.Length] = value;
					debug_options.JitArguments = new_args;
				} else {
					debug_options.JitArguments = new string[] { value };
				}
				return true;

			case "-fullname":
			case "-f":
				if (ms_value != null) {
					Usage ();
					Environment.Exit (1);
				}
				debug_options.InEmacs = true;
				return true;

			case "-mono-prefix":
				value = GetValue (ref args, ref i, ms_value);
				if (value == null) {
					Usage ();
					Environment.Exit (1);
				}
				debug_options.MonoPrefix = value;
				return true;

			case "-mono":
				value = GetValue (ref args, ref i, ms_value);
				if (value == null) {
					Usage ();
					Environment.Exit (1);
				}
				debug_options.MonoPath = value;
				return true;

			case "-script":
				if (ms_value != null) {
					Usage ();
					Environment.Exit (1);
				}
				debug_options.IsScript = true;
				return true;

			case "-version":
			case "-V":
				if (ms_value != null) {
					Usage ();
					Environment.Exit (1);
				}
				About();
				Environment.Exit (1);
				return true;

			case "-help":
			case "--help":
			case "-h":
			case "-usage":
				if (ms_value != null) {
					Usage ();
					Environment.Exit (1);
				}
				Usage();
				Environment.Exit (1);
				return true;

			case "-run":
				if (ms_value != null) {
					Usage ();
					Environment.Exit (1);
				}
				debug_options.StartTarget = true;
				return true;
			}

			return false;
		}

		static DebuggerOptions ParseCommandLine (string[] args)
		{
			DebuggerOptions options = new DebuggerOptions ();
			int i;
			bool parsing_options = true;
			bool args_follow = false;

			for (i = 0; i < args.Length; i++) {
				string arg = args[i];

				if (arg == "")
					continue;

				if (!parsing_options)
					continue;

				if (arg.StartsWith ("-")) {
					if (ParseOption (options, arg, ref args, ref i, ref args_follow))
						continue;
				} else if (arg.StartsWith ("/")) {
					string unix_opt = "-" + arg.Substring (1);
					if (ParseOption (options, unix_opt, ref args, ref i, ref args_follow))
						continue;
				}

				options.File = arg;
				break;
			}

			if (args_follow) {
				string[] argv = new string [args.Length - i - 1];
				Array.Copy (args, i + 1, argv, 0, args.Length - i - 1);
				options.InferiorArgs = argv;
			} else {
				options.InferiorArgs = new string [0];
			}

			return options;
		}

		static void Usage ()
		{
			Console.WriteLine (
				"Mono Debugger, (C) 2003-2004 Novell, Inc.\n" +
				"mdb [options] [exe-file]\n" +
				"mdb [options] -args exe-file [inferior-arguments ...]\n\n" +
				
				"   -args                     Arguments after exe-file are passed to inferior\n" +
				"   -debug-flags:PARAM        Sets the debugging flags\n" +
				"   -fullname                 Sets the debugging flags (short -f)\n" +
				"   -jit-optimizations:PARAM  Set jit optimizations used on the inferior process\n" +
				"   -mono:PATH                Override the inferior mono\n" +
				"   -mono-prefix:PATH         Override the mono prefix\n" +
				"   -native-symtabs           Load native symtabs\n" +
				"   -script                  \n" +
				"   -usage                   \n" +
				"   -version                  Display version and licensing information (short -V)\n" +
				"   -working-directory:DIR    Sets the working directory (short -cd)\n"
				);
		}

		static void About ()
		{
			Console.WriteLine (
				"The Mono Debugger is (C) 2003, 2004 Novell, Inc.\n\n" +
				"The debugger source code is released under the terms of the GNU GPL\n\n" +

				"For more information on Mono, visit the project Web site\n" +
				"   http://www.go-mono.com\n\n" +

				"The debugger was written by Martin Baulig and Chris Toshok");

			Environment.Exit (0);
		}

		void command_thread_main ()
		{
			do {
				Semaphore.Wait ();
				if (mono_debugger_server_get_pending_sigint () == 0)
					continue;

				interpreter.Interrupt ();
				main_thread.Abort ();
			} while (true);
		}

		public static void Main (string[] args)
		{
			bool is_terminal = GnuReadLine.IsTerminal (0);

			DebuggerConfiguration config = new DebuggerConfiguration ();
			config.LoadConfiguration ();

			DebuggerOptions options = ParseCommandLine (args);

			Console.WriteLine ("Mono Debugger");

			CommandLineInterpreter interpreter = new CommandLineInterpreter (
				is_terminal, config, options);

			interpreter.RunMainLoop ();
		}
	}
}
