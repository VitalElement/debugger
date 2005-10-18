using System;

namespace Mono.Debugger.Languages.Mono
{
	internal class MonoObjectObject : TargetObjectObject
	{
		public new readonly MonoObjectType Type;

		public MonoObjectObject (MonoObjectType type, TargetLocation location)
			: base (type, location)
		{
			this.Type = type;
		}

		public override TargetClassObject GetClassObject (TargetAccess target)
		{
			return (TargetClassObject) Type.ClassType.GetObject (Location);
		}

		public override TargetType GetCurrentType (TargetAccess target)
		{
			// location.Address resolves to the address of the MonoObject,
			// dereferencing it once gives us the vtable, dereferencing it
			// twice the class.
			TargetAddress address;
			address = target.TargetMemoryAccess.ReadAddress (Location.Address);
			address = target.TargetMemoryAccess.ReadAddress (address);

			return Type.File.MonoLanguage.GetClass (target, address);
		}

		public override TargetObject GetDereferencedObject (TargetAccess target)
		{
			TargetType current_type = GetCurrentType (target);
			if (current_type == null)
				return null;

			// If this is a reference type, then the `MonoObject *' already
			// points to the boxed object itself.
			// If it's a valuetype, then the boxed contents is immediately
			// after the `MonoObject' header.

			int offset = current_type.IsByRef ? 0 : type.Size;
			TargetLocation new_location = Location.GetLocationAtOffset (offset);
			TargetObject obj = current_type.GetObject (new_location);
			return obj;
		}
	}
}
