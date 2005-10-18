using System;
using Cecil = Mono.Cecil;

namespace Mono.Debugger.Languages.Mono
{
	internal class MonoObjectType : TargetObjectType
	{
		MonoSymbolFile file;

		public MonoObjectType (MonoSymbolFile file, Cecil.ITypeDefinition typedef, int size)
			: base (file.MonoLanguage, "object", size)
		{
			this.file = file;
		}

		public MonoSymbolFile File {
			get { return file; }
		}

		public override TargetClassType ClassType {
			get { return file.MonoLanguage.ObjectType; }
		}

		internal override TargetObject GetObject (TargetLocation location)
		{
			return new MonoObjectObject (this, location);
		}
	}
}
