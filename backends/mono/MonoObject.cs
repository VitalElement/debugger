using System;

namespace Mono.Debugger.Languages.Mono
{
	internal abstract class MonoObject : ITargetObject
	{
		protected MonoTypeInfo type;
		protected TargetLocation location;
		protected bool is_valid;

		public MonoObject (MonoTypeInfo type, TargetLocation location)
		{
			this.type = type;
			this.location = location;
			is_valid = true;
		}

		public ITargetTypeInfo Type {
			get {
				return type;
			}
		}

		public bool IsValid {
			get {
				return is_valid && (location != null) && location.IsValid;
			}
		}

		public virtual byte[] RawContents {
			get {
				try {
					return location.ReadBuffer (type.Size);
				} catch (TargetException ex) {
					is_valid = false;
					throw new LocationInvalidException (ex);
				}
			}
			set {
				try {
					if (!type.HasFixedSize || (value.Length != type.Size))
						throw new ArgumentException ();
					location.WriteBuffer (value);
				} catch (TargetException ex) {
					is_valid = false;
					throw new LocationInvalidException (ex);
				}
			}
		}

		protected virtual int MaximumDynamicSize {
			get {
				return -1;
			}
		}

		public virtual long DynamicSize {
			get {
				if (type.HasFixedSize)
					throw new InvalidOperationException ();

				try {
					TargetLocation dynamic_location;
					ITargetMemoryReader reader = location.ReadMemory (type.Size);
					return GetDynamicSize (reader, location, out dynamic_location);
				} catch (TargetException ex) {
					is_valid = false;
					throw new LocationInvalidException (ex);
				}
			}
		}

		public virtual byte[] GetRawDynamicContents (int max_size)
		{
			if (type.HasFixedSize)
				throw new InvalidOperationException ();

			try {
				return GetDynamicContents (location, max_size).Contents;
			} catch (TargetException ex) {
				is_valid = false;
				throw new LocationInvalidException (ex);
			}
		}

		protected virtual ITargetMemoryReader GetDynamicContents (TargetLocation location,
									  int max_size)
		{
			try {
				TargetLocation dynamic_location;
				ITargetMemoryReader reader = location.ReadMemory (type.Size);
				long size = GetDynamicSize (reader, location, out dynamic_location);

				if ((max_size > 0) && (size > (long) max_size))
					size = max_size;

				return dynamic_location.ReadMemory ((int) size);
			} catch (TargetException ex) {
				is_valid = false;
				throw new LocationInvalidException (ex);
			}
		}

		protected abstract long GetDynamicSize (ITargetMemoryReader reader,
							TargetLocation location,
							out TargetLocation dynamic_location);

		public TargetLocation Location {
			get {
				if (!IsValid)
					throw new LocationInvalidException ();

				return location;
			}
		}

		public virtual void SetObject (ITargetObject obj)
		{
			Location.WriteAddress (obj.Location.Address);
		}

		public virtual string Print ()
		{
			return ToString ();
		}

		public override string ToString ()
		{
			return String.Format ("{0} [{1}]", GetType (), Type);
		}
	}
}