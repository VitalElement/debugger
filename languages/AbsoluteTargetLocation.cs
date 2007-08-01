using System;

namespace Mono.Debugger.Languages
{
	// <summary>
	//   This is an absolute address - usually supplied by the user.
	// </summary>
	internal class AbsoluteTargetLocation : TargetLocation
	{
		TargetAddress address;

		public AbsoluteTargetLocation (TargetAddress address)
		{
			this.address = address;
		}

		public override bool HasAddress {
			get { return true; }
		}

		public override TargetAddress GetAddress (TargetMemoryAccess target)
		{
			return address;
		}

		public override string Print ()
		{
			return address.ToString ();
		}

		protected override string MyToString ()
		{
			return String.Format (":{0}", address);
		}
	}
}
