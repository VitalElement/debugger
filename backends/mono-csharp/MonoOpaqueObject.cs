using System;
using Mono.CSharp.Debugger;
using Mono.Debugger.Backends;

namespace Mono.Debugger.Languages.CSharp
{
	internal class MonoOpaqueObject : MonoObject, ITargetPointerObject
	{
		new MonoOpaqueType type;

		public MonoOpaqueObject (MonoOpaqueType type, TargetLocation location)
			: base (type, location)
		{
			this.type = type;
		}

		public new ITargetPointerType Type {
			get {
				return type;
			}
		}

		ITargetType ITargetPointerObject.CurrentType {
			get {
				throw new InvalidOperationException ();
			}
		}

		bool ITargetPointerObject.HasDereferencedObject {
			get { return false; }
		}

		ITargetObject ITargetPointerObject.DereferencedObject {
			get {
				throw new InvalidOperationException ();
			}
		}

		public byte[] GetDereferencedContents (int size)
		{
			try {
				return location.ReadBuffer (size);
			} catch (TargetException ex) {
				throw new LocationInvalidException (ex);
			}
		}

		protected override long GetDynamicSize (ITargetMemoryReader reader,
							TargetLocation location,
							out TargetLocation dynamic_location)
		{
			throw new InvalidOperationException ();
		}
	}
}

