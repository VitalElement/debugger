using System;
using System.Xml;
using Mono.Debugger.Backend;
using System.Runtime.Serialization;

using Mono.Debugger.Languages;
using Mono.Debugger.Languages.Mono;

namespace Mono.Debugger
{
	[Serializable]
	public sealed class ExceptionCatchPoint : Event
	{
		int handle = -1;

		public override bool IsPersistent {
			get { return true; }
		}

		internal ExceptionCatchPoint (ThreadGroup group, TargetType exception)
			: base (EventType.CatchException, exception.Name, group)
		{
			this.exception = exception;
		}

		internal ExceptionCatchPoint (int index, ThreadGroup group, string name)
			: base (EventType.CatchException, index, name, group)
		{ }

		public override bool IsActivated {
			get { return handle > 0; }
		}

		public override void Activate (Thread target)
		{
			lock (this) {
				EnableCatchpoint (target);
			}
		}

		public override void Deactivate (Thread target)
		{
			lock (this) {
				DisableCatchpoint (target);
			}
		}

		internal override void OnTargetExited ()
		{
			exception = null;
			handle = -1;
		}

		public override void Remove (Thread target)
		{
			lock (this) {
				DisableCatchpoint (target);
			}
		}

		void EnableCatchpoint (Thread target)
		{
			lock (this) {
				if (handle > 0)
					return;

				handle = target.AddEventHandler (this);
			}
		}

		void DisableCatchpoint (Thread target)
		{
			lock (this) {
				if (handle > 0)
					target.RemoveEventHandler (handle);

				handle = -1;
			}
		}

		bool IsSubclassOf (TargetMemoryAccess target, TargetStructType type,
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
			element.AppendChild (exception_e);
		}

		TargetType exception;
	}
}