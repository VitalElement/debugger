using System;

namespace Mono.Debugger
{
	public interface ITargetObject
	{
		// <summary>
		//   The type of this object.
		// </summary>
		ITargetType Type {
			get;
		}

		// <summary>
		//   If true, you may get a Mono object representing this object with the
		//   @Object property.
		// </summary>
		bool HasObject {
			get;
		}

		// <summary>
		//   Returns a Mono object representing this object.
		//
		//   Objects in the target are represented as follows:
		//
		//   * Fundamental types:
		//
		//     An instance of the corresponding fundamental type
		//     (bool, char, sbyte, byte, short, ushort, int, uint, long, ulong,
		//      single, double, string)
		//
		//   * Single-dimensional arrays:
		//
		//     An instance of ITargetArray which can be used to get the array's
		//     element type, upper and lower bound and the array's elements.
		//
		//   * Multi-dimensional arrays:
		//
		//     A multi-dimensional array is represented as an array of arrays,
		//     so you'll get an instance of ITargetArray whose elements are
		//     ITargetArray's.
		//
		//     NOTE: We cannot return a multi-dimensional array because not all
		//           programming languages support multi-dimensional arrays.
		//
		// </summary>
		object Object {
			get;
		}
	}
}
