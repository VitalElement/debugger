using System;
using System.Runtime.InteropServices;

namespace Mono.Debugger.Languages.Native
{
	internal class NativeLanguage : ILanguage
	{
		NativeFundamentalType integer_type;
		NativeFundamentalType long_type;
		NativePointerType pointer_type;

		public NativeLanguage (ITargetInfo info)
		{
			integer_type = new NativeFundamentalType ("int", typeof (int), Marshal.SizeOf (typeof (int)));
			long_type = new NativeFundamentalType ("long", typeof (long), Marshal.SizeOf (typeof (long)));
			pointer_type = new NativePointerType ("pointer", info.TargetAddressSize);
		}

		public string Name {
			get { return "native"; }
		}

		public ITargetFundamentalType IntegerType {
			get { return integer_type; }
		}

		public ITargetFundamentalType LongIntegerType {
			get { return long_type; }
		}

		public ITargetFundamentalType StringType {
			get { return null; }
		}

		public ITargetType PointerType {
			get { return pointer_type; }
		}

		public ITargetType LookupType (StackFrame frame, string name)
		{
			return null;
		}
	}
}
