using System;
using NUnit.Framework;

using Mono.Debugger;
using Mono.Debugger.Languages;
using Mono.Debugger.Frontend;
using Mono.Debugger.Test.Framework;

namespace Mono.Debugger.Tests
{
	[DebuggerTestFixture]
	public class TestActivateBreakpoints2 : DebuggerTestFixture
	{
		public TestActivateBreakpoints2 ()
			: base ("TestActivateBreakpoints2")
		{
			Config.ThreadingModel = ThreadingModel.Single;
		}

		[Test]
		[Category("GUI")]
		public void Main ()
		{
			Process process = Start ();
			Assert.IsTrue (process.IsManaged);
			Assert.IsTrue (process.MainThread.IsStopped);

			Thread thread = process.MainThread;

			AssertStopped (thread, "main", "X.Main(string[])");

			AssertExecute ("continue");
			Thread blocking = AssertThreadCreated ();
			AssertHitBreakpoint (thread, "thread start1", "X.StartThreads()");
			AssertExecute ("continue");
			Thread sleeping = AssertThreadCreated ();
			AssertHitBreakpoint (thread, "thread start2", "X.StartThreads()");
			AssertExecute ("continue");
			Thread executing = AssertThreadCreated ();
			AssertTargetOutput ("True");
			AssertHitBreakpoint (thread, "thread start3", "X.StartThreads()");

			AssertExecute ("bg");
			int bpt_executing = (int) AssertExecute ("break -gui -global " + GetLine ("executing"));

			AssertHitBreakpoint (executing, bpt_executing, "X.ExecutingThread()", GetLine ("executing"));
			Interpreter.CurrentThread = executing;
			AssertExecute ("set loop = false");
			AssertPrint (executing, "loop", "(bool) false");
			AssertExecute ("bg");

			int status = 0;
			while (status != 63) {
				DebuggerEvent e = AssertEvent ();
				if (e.Type == DebuggerEventType.ThreadExited) {
					if (e.Data == blocking) {
						status |= 1;
						continue;
					} else if (e.Data == executing) {
						status |= 2;
						continue;
					} else if (e.Data == sleeping) {
						status |= 4;
						continue;
					}
				} else if (e.Type == DebuggerEventType.TargetEvent) {
					Thread e_thread = (Thread) e.Data;
					TargetEventArgs args = (TargetEventArgs) e.Data2;

					if (args.Type == TargetEventType.TargetExited) {
						if (e_thread == blocking) {
							status |= 8;
							continue;
						} else if (e_thread == executing) {
							status |= 16;
							continue;
						} else if (e_thread == sleeping) {
							status |= 32;
							continue;
						}
					}
				}

				Assert.Fail ("Received unexpected event {0}.", e);
			}

			AssertTargetOutput ("Blocking Done");
			AssertTargetExited (process);
		}
	}
}
