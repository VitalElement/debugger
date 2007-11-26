using System;
using Mono.Debugger.Backends;

namespace Mono.Debugger.Languages.Mono
{
	internal class MonoClassObject : TargetClassObject
	{
		new MonoClassType type;
		MonoClassInfo info;

		public MonoClassObject (MonoClassType type, MonoClassInfo info,
					TargetLocation location)
			: base (type, location)
		{
			this.type = type;
			this.info = info;
		}

		public override TargetClassObject GetParentObject (Thread target)
		{
			if (!type.HasParent || !type.IsByRef)
				return null;

			MonoClassInfo parent_info = info.GetParent (target);
			if (parent_info == null)
				return null;

			MonoClassType parent_type = parent_info.ClassType;
			if (!type.IsByRef && parent_type.IsByRef)
				return null;

			return new MonoClassObject (parent_type, parent_info, Location);
		}

		public override TargetClassObject GetCurrentObject (Thread thread)
		{
			if (!type.IsByRef)
				return null;

			return (TargetClassObject) thread.ThreadServant.DoTargetAccess (
				delegate (InternalTargetAccess target, object data) {
					return type.GetCurrentObject (target, Location);
			}, null);
		}

		public override TargetObject GetField (Thread target, TargetFieldInfo field)
		{
			return info.GetField (target, Location, field);
		}

		public override void SetField (Thread target, TargetFieldInfo field,
					       TargetObject obj)
		{
			info.SetField (target, Location, field, obj);
		}

		internal TargetAddress GetKlassAddress (TargetMemoryAccess target)
		{
			return info.KlassAddress;
		}

		internal override long GetDynamicSize (TargetMemoryAccess target, TargetBlob blob,
						       TargetLocation location,
						       out TargetLocation dynamic_location)
		{
			throw new InvalidOperationException ();
		}

		public override string Print (Thread target)
		{
			if (Location.HasAddress)
				return String.Format ("{0}", Location.GetAddress (target));
			else
				return String.Format ("{0}", Location);
		}
	}
}
