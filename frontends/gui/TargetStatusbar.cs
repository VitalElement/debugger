using Gtk;
using System;
using System.IO;
using System.Runtime.InteropServices;

using Mono.Debugger;

namespace Mono.Debugger.GUI
{
	public class TargetStatusbar : DebuggerWidget
	{
		protected Gtk.Statusbar status_bar;
		protected uint status_id;

		public TargetStatusbar (DebuggerGUI gui, Gtk.Statusbar widget)
			: base (gui, widget)
		{
			status_bar = widget;
			status_id = status_bar.GetContextId ("message");
		}

		public override void SetBackend (DebuggerBackend backend, Process process)
		{
			base.SetBackend (backend, process);
		}
		
		public void Message (string message)
		{
			if (!IsVisible)
				return;

			status_bar.Pop (status_id);
			status_bar.Push (status_id, message);
		}

		protected virtual string GetStopReason (int arg)
		{
			if (arg == 0)
				return "Stopped";
			else
				return String.Format ("Received signal {0}", arg);
		}

		protected virtual string GetStopMessage (StackFrame frame, int arg)
		{
			if (frame.Method != null) {
				long offset = frame.TargetAddress - frame.Method.StartAddress;

				if (offset > 0)
					return String.Format ("{3} at {0} in {1}+{2:x}",
							      frame.TargetAddress, frame.Method.Name,
							      offset, GetStopReason (arg));
				else if (offset == 0)
					return String.Format ("{2} at {0} in {1}",
							      frame.TargetAddress, frame.Method.Name,
							      GetStopReason (arg));
			}

			return String.Format ("{1} at {0}.", frame.TargetAddress, GetStopReason (arg));
		}

		protected override void StateChanged (TargetState new_state, int arg)
		{
			if (!IsVisible)
				return;

			switch (new_state) {
			case TargetState.RUNNING:
				Message ("Running ....");
				break;

			case TargetState.CORE_FILE:
			case TargetState.STOPPED:
				if (CurrentFrame != null)
					Message (GetStopMessage (CurrentFrame, arg));
				else
					Message (String.Format ("{0}.", GetStopReason (arg)));
				break;

			case TargetState.EXITED:
				if (arg == 0)
					Message ("Program terminated.");
				else
					Message (String.Format ("Program terminated with signal {0}.", arg));
				break;

			case TargetState.NO_TARGET:
				Message ("No target to debug.");
				break;

			case TargetState.BUSY:
				Message ("Debugger busy ...");
				break;
			}
		}
	}
}
