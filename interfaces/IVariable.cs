using System;

namespace Mono.Debugger
{
	// <summary>
	//   This interface provides information about a variable in the target application.
	// </summary>
	public interface IVariable
	{
		string Name {
			get;
		}

		ITargetType Type {
			get;
		}

		// <summary>
		//   Retrieve an instance of this variable from the stack-frame @frame.
		//   May only be called if Type.HasObject is true.
		// </summary>
		// <remarks>
		//   An instance of IVariable contains information about a variable (for
		//   instance a parameter of local variable of a method), but it's not
		//   bound to any particular target location.  This also means that it won't
		//   get invalid after the target exited.
		// </remarks>
		ITargetObject GetObject (StackFrame frame);
	}
}
