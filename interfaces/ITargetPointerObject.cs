using System;

namespace Mono.Debugger
{
	public interface ITargetPointerObject : ITargetObject
	{
		ITargetPointerType Type {
			get;
		}

		// <summary>
		//   The current type of the object pointed to by this pointer.
		//   May only be used if ITargetPointerType.HasStaticType is false.
		// </summary>
		ITargetType CurrentType {
			get;
		}

		// <summary>
		//   Dereference the pointer and read @size bytes from the location it
		//   points to.  Only allowed for non-typesafe pointers.
		// </summary>
		byte[] GetDereferencedContents (int size);
	}
}
