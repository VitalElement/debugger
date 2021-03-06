using System;
using System.Xml;
using Mono.Debugger.Backend;
using System.Runtime.Serialization;

using Mono.Debugger.Languages;
using Mono.Debugger.Languages.Mono;
using Mono.Debugger.Backend.Mono;

namespace Mono.Debugger
{
	[Serializable]
	public sealed class ExceptionCatchPoint : Event
	{
		public override bool IsPersistent {
			get { return true; }
		}

		public override bool NeedsActivation {
			get { return false; }
		}

		public bool Unhandled {
			get; set;
		}

		internal ExceptionCatchPoint (ThreadGroup group, TargetType exception, bool unhandled)
			: base (EventType.CatchException, exception.Name, group)
		{
			this.exception = exception;
			this.Unhandled = unhandled;
		}

		internal ExceptionCatchPoint (int index, ThreadGroup group, string name, bool unhandled)
			: base (EventType.CatchException, index, name, group)
		{
			this.Unhandled = unhandled;
		}

		public override bool IsActivated {
			get { return true; }
		}

		public override void Activate (Thread target)
		{ }

		public override void Deactivate (Thread target)
		{ }

		internal override void OnTargetExited ()
		{
			exception = null;
		}

		public override void Remove (Thread target)
		{ }

		bool IsSubclassOf (TargetMemoryAccess target, TargetClassType type,
				   TargetType parent)
		{
			while (type != null) {
				if (type == parent)
					return true;

				if (!type.HasParent)
					return false;

				type = type.GetParentType (target);
			}

			return false;
		}

		internal bool CheckException (MonoLanguageBackend mono, TargetMemoryAccess target,
					      TargetAddress address)
		{
			TargetClassObject exc = mono.CreateObject (target, address) as TargetClassObject;
			if (exc == null)
				return false; // OOOPS

			if (exception == null)
				exception = mono.LookupType (Name);
			if (exception == null)
				return false;

			return IsSubclassOf (target, exc.Type, exception);
		}

		protected override void GetSessionData (XmlElement root, XmlElement element)
		{
			XmlElement exception_e = root.OwnerDocument.CreateElement ("Exception");
			exception_e.SetAttribute ("type", Name);
			exception_e.SetAttribute ("unhandled", Unhandled ? "true" : "false");
			element.AppendChild (exception_e);
		}

		TargetType exception;
	}
}
