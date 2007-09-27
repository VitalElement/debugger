using System;
using System.Text;
using System.Collections;
using C = Mono.CompilerServices.SymbolWriter;
using Cecil = Mono.Cecil;

using Mono.Debugger.Backends;

namespace Mono.Debugger.Languages.Mono
{
	internal delegate void ClassInitHandler (TargetMemoryAccess target, TargetAddress klass);

	internal class MonoClassType : TargetClassType
	{
		MonoFieldInfo[] fields;
		MonoFieldInfo[] static_fields;
		MonoMethodInfo[] methods;
		MonoMethodInfo[] static_methods;
		MonoPropertyInfo[] properties;
		MonoPropertyInfo[] static_properties;
		MonoEventInfo[] events;
		MonoEventInfo[] static_events;
		MonoMethodInfo[] constructors;
		MonoMethodInfo[] static_constructors;

		int num_methods = 0, num_smethods = 0;
		int first_method = 0, first_smethod = 0;
		int num_fields = 0, num_sfields = 0, first_field = 0;

		Cecil.TypeDefinition type;
		MonoSymbolFile file;
		MonoClassType parent_type;
		MonoClassInfo class_info;

		Hashtable load_handlers;
		int load_handler_id;

		public MonoClassType (MonoSymbolFile file, Cecil.TypeDefinition type)
			: base (file.MonoLanguage, TargetObjectKind.Class)
		{
			this.type = type;
			this.file = file;

			if (type.BaseType != null) {
				TargetType parent = file.MonoLanguage.LookupMonoType (type.BaseType);
				if (parent != null)
					parent_type = (MonoClassType) parent.ClassType;
			}
		}

		public MonoClassType (MonoSymbolFile file, Cecil.TypeDefinition typedef,
				      MonoClassInfo class_info)
			: this (file, typedef)
		{
			this.class_info = class_info;
		}

		public Cecil.TypeDefinition Type {
			get { return type; }
		}

		public override string Name {
			get { return type.FullName; }
		}

		public override bool IsByRef {
			get { return !type.IsValueType; }
		}

		public override bool HasFixedSize {
			get { return false; }
		}

		public override int Size {
			get { return 2 * Language.TargetInfo.TargetAddressSize; }
		}

		public MonoSymbolFile File {
			get { return file; }
		}

		public override bool HasParent {
			get { return parent_type != null; }
		}

		public override TargetClassType ParentType {
			get { return parent_type; }
		}

		internal MonoClassType MonoParentType {
			get { return parent_type; }
		}

		public override Module Module {
			get { return file.Module; }
		}

		public override bool HasClassType {
			get { return true; }
		}

		public override TargetClassType ClassType {
			get { return this; }
		}

		internal int Token {
			get { return (int) (type.MetadataToken.TokenType + type.MetadataToken.RID); }
		}

		internal int FirstField {
			get {
				return first_field;
			}
		}

		void get_fields ()
		{
			if (fields != null)
				return;

			foreach (Cecil.FieldDefinition field in type.Fields) {
				if (field.IsStatic)
					num_sfields++;
				else
					num_fields++;
			}

			fields = new MonoFieldInfo [num_fields];
			static_fields = new MonoFieldInfo [num_sfields];

			if (parent_type != null) {
				parent_type.get_fields ();
				first_field = parent_type.first_field + 
					parent_type.num_fields + parent_type.num_sfields;
			}

			int pos = 0, spos = 0, i = first_field;
			foreach (Cecil.FieldDefinition field in type.Fields) {
				TargetType ftype = File.MonoLanguage.LookupMonoType (field.FieldType);
				if (field.IsStatic) {
					static_fields [spos] = new MonoFieldInfo (ftype, spos, i, field);
					spos++;
				} else {
					fields [pos] = new MonoFieldInfo (ftype, pos, i, field);
					pos++;
				}

				i++;
			}
		}

		public override TargetFieldInfo[] Fields {
			get {
				get_fields ();
				return fields;
			}
		}

		public override TargetFieldInfo[] StaticFields {
			get {
				get_fields ();
				return static_fields;
			}
		}

		public int CountMethods {
			get {
				if (parent_type != null)
					return parent_type.CountMethods + num_methods;
				else
					return num_methods;
			}
		}

		public int CountStaticMethods {
			get {
				if (parent_type != null)
					return parent_type.CountStaticMethods + num_smethods;
				else
					return num_smethods;
			}
		}

		void get_methods ()
		{
			if (methods != null)
				return;

			foreach (Cecil.MethodDefinition method in type.Methods) {
				if ((method.Attributes & Cecil.MethodAttributes.SpecialName) != 0)
					continue;
				if (method.IsStatic)
					num_smethods++;
				else
					num_methods++;
			}

			methods = new MonoMethodInfo [num_methods];
			static_methods = new MonoMethodInfo [num_smethods];

			if (parent_type != null) {
				parent_type.get_methods ();
				first_method = parent_type.CountMethods;
				first_smethod = parent_type.CountStaticMethods;
			}

			int pos = 0, spos = 0;
			foreach (Cecil.MethodDefinition method in type.Methods) {
				if ((method.Attributes & Cecil.MethodAttributes.SpecialName) != 0)
					continue;
				if (method.IsStatic) {
					static_methods [spos] = MonoMethodInfo.Create (this, spos, method);
					spos++;
				} else {
					methods [pos] = MonoMethodInfo.Create (this, pos, method);
					pos++;
				}
			}
		}

		void get_properties ()
		{
			if (properties != null)
				return;

			int num_sproperties = 0, num_properties = 0;

			foreach (Cecil.PropertyDefinition prop in type.Properties) {
				Cecil.MethodDefinition m = prop.GetMethod;
				if (m == null) m = prop.SetMethod;

				if (m.IsStatic)
					num_sproperties++;
				else
					num_properties++;
			}

			properties = new MonoPropertyInfo [num_properties];
			static_properties = new MonoPropertyInfo [num_sproperties];

			int pos = 0, spos = 0;
			foreach (Cecil.PropertyDefinition prop in type.Properties) {
				Cecil.MethodDefinition m = prop.GetMethod;
				if (m == null) m = prop.SetMethod;

				if (m.IsStatic) {
					static_properties [spos] = MonoPropertyInfo.Create (
						this, spos, prop, true);
					spos++;
				}
				else {
					properties [pos] = MonoPropertyInfo.Create (
						this, pos, prop, false);
					pos++;
				}
			}
		}

		public override TargetMethodInfo[] Methods {
			get {
				get_methods ();
				return methods;
			}
		}

		public override TargetMethodInfo[] StaticMethods {
			get {
				get_methods ();
				return static_methods;
			}
		}

		public override TargetPropertyInfo[] Properties {
			get {
				get_properties ();
				return properties;
			}
		}

		public override TargetPropertyInfo[] StaticProperties {
			get {
				get_properties ();
				return static_properties;
			}
		}

		void get_events ()
		{
			if (events != null)
				return;

			int num_sevents = 0, num_events = 0;
			foreach (Cecil.EventDefinition ev in type.Events) {
				Cecil.MethodDefinition m = ev.AddMethod;

				if (m.IsStatic)
					num_sevents++;
				else
					num_events++;
			}

			events = new MonoEventInfo [num_events];
			static_events = new MonoEventInfo [num_sevents];

			int pos = 0, spos = 0;
			foreach (Cecil.EventDefinition ev in type.Events) {
				Cecil.MethodDefinition m = ev.AddMethod;

				if (m.IsStatic) {
					static_events [spos] = MonoEventInfo.Create (this, spos, ev, true);
					spos++;
				}
				else {
					events [pos] = MonoEventInfo.Create (this, pos, ev, false);
					pos++;
				}
			}
		}

		public override TargetEventInfo[] Events {
			get {
				get_events ();
				return events;
			}
		}

		public override TargetEventInfo[] StaticEvents {
			get {
				get_events ();
				return static_events;
			}
		}

		public TargetObject GetStaticEvent (StackFrame frame, int index)
		{
			get_events ();
			return null;
		}

		void get_constructors ()
		{
			if (constructors != null)
				return;

			int num_ctors = 0, num_sctors = 0;

			foreach (Cecil.MethodDefinition method in type.Constructors) {
				if (method.IsStatic)
					num_sctors++;
				else
					num_ctors++;
			}

			constructors = new MonoMethodInfo [num_ctors];
			static_constructors = new MonoMethodInfo [num_sctors];

			int pos = 0, spos = 0;
			foreach (Cecil.MethodDefinition method in type.Constructors) {
				if (method.IsStatic) {
					static_constructors [spos] = MonoMethodInfo.Create (
						this, spos, method);
					spos++;
				} else {
					constructors [pos] = MonoMethodInfo.Create (
						this, pos, method);
					pos++;
				}
			}
		}

		public override TargetMethodInfo[] Constructors {
			get {
				get_constructors ();
				return constructors;
			}
		}

		public override TargetMethodInfo[] StaticConstructors {
			get {
				get_constructors ();
				return static_constructors;
			}
		}

		internal MonoClassInfo ResolveClass (TargetMemoryAccess target, bool fail)
		{
			if (class_info != null)
				return class_info;

			if (parent_type != null) {
				if (parent_type.ResolveClass (target, fail) == null)
					return null;
			}

			class_info = file.LookupClassInfo (target, (int) type.MetadataToken.ToUInt ());
			if (class_info != null)
				return class_info;

			if (fail)
				throw new TargetException (TargetError.ClassNotInitialized,
							   "Class `{0}' not initialized yet.", Name);

			return null;
		}

		internal MonoClassInfo ClassResolved (TargetMemoryAccess target, TargetAddress klass)
		{
			class_info = File.MonoLanguage.ReadClassInfo (target, klass);
			return class_info;
		}

		protected override TargetObject DoGetObject (TargetLocation location)
		{
			return new MonoClassObject (this, location);
		}

		public override TargetMemberInfo FindMember (string name, bool search_static,
							     bool search_instance)
		{
			if (search_static) {
				foreach (TargetFieldInfo field in StaticFields)
					if (field.Name == name)
						return field;

				foreach (TargetPropertyInfo property in StaticProperties)
					if (property.Name == name)
						return property;

				foreach (TargetEventInfo ev in StaticEvents)
					if (ev.Name == name)
						return ev;
			}

			if (search_instance) {
				foreach (TargetFieldInfo field in Fields)
					if (field.Name == name)
						return field;

				foreach (TargetPropertyInfo property in Properties)
					if (property.Name == name)
						return property;

				foreach (TargetEventInfo ev in Events)
					if (ev.Name == name)
						return ev;
			}

			return null;
		}

		internal static TargetType ReadMonoClass (MonoLanguageBackend language,
							  TargetMemoryAccess target, TargetAddress address)
		{
			TargetReader reader = new TargetReader (
				target.ReadMemory (address, language.MonoMetadataInfo.KlassSize));

			TargetAddress image = reader.ReadAddress ();
			MonoSymbolFile file = language.GetImage (image);

			reader.ReadAddress ();
			TargetAddress element_class = reader.ReadAddress ();

			if (file == null)
				return null;

			reader.Offset = language.MonoMetadataInfo.KlassTokenOffset;
			uint token = reader.BinaryReader.ReadUInt32 ();

			reader.Offset = language.MonoMetadataInfo.KlassByValArgOffset;
			TargetAddress byval_data_addr = reader.ReadAddress ();
			reader.Offset += 2;
			int type = reader.ReadByte ();

			reader.Offset = language.MonoMetadataInfo.KlassGenericClassOffset;
			TargetAddress generic_class = reader.ReadAddress ();

			reader.Offset = language.MonoMetadataInfo.KlassGenericContainerOffset;
			TargetAddress generic_container = reader.ReadAddress ();

			if (!generic_class.IsNull || !generic_container.IsNull)
				return null;

			if ((type == 0x11) || (type == 0x12)) { // MONO_TYPE_(VALUETYPE|CLASS)
				Cecil.TypeDefinition tdef;

				if ((token & 0xff000000) != 0x02000000)
					return null;

				token &= 0x00ffffff;
				tdef = (Cecil.TypeDefinition) file.ModuleDefinition.LookupByToken (
					Cecil.Metadata.TokenType.TypeDef, (int) token);

				if (tdef != null)
					return file.LookupMonoType (tdef);
			} else if (type == 0x1d) { // MONO_TYPE_SZARRAY
				TargetType eklass = ReadMonoClass (language, target, element_class);
				if (eklass == null)
					return null;

				return new MonoArrayType (eklass, 1);
			} else if (type == 0x14) { // MONO_TYPE_ARRAY
				TargetType eklass = ReadMonoClass (language, target, element_class);
				if (eklass == null)
					return null;

				TargetBlob array_data = target.ReadMemory (
					byval_data_addr, language.MonoMetadataInfo.ArrayTypeSize);
				TargetReader array_reader = new TargetReader (array_data);

				array_reader.ReadAddress ();
				int rank = array_reader.ReadByte ();

				return new MonoArrayType (eklass, rank);
			}

			return null;
		}

		int GetFieldOffset (TargetMemoryAccess target, TargetFieldInfo field)
		{
			ResolveClass (target, true);

			if (field.Position < FirstField)
				return parent_type.GetFieldOffset (target, field);

			return class_info.GetFieldOffsets (target) [field.Position - FirstField];
		}

		internal TargetObject GetField (Thread target, TargetLocation location,
						TargetFieldInfo finfo)
		{
			int offset = GetFieldOffset (target, finfo);
			if (!IsByRef)
				offset -= 2 * target.TargetInfo.TargetAddressSize;
			TargetLocation field_loc = location.GetLocationAtOffset (offset);

			if (finfo.Type.IsByRef)
				field_loc = field_loc.GetDereferencedLocation ();

			if (field_loc.HasAddress && field_loc.GetAddress (target).IsNull)
				return null;

			return finfo.Type.GetObject (field_loc);
		}

		internal void SetField (Thread target, TargetLocation location,
					TargetFieldInfo finfo, TargetObject obj)
		{
			int offset = GetFieldOffset (target, finfo);
			if (!IsByRef)
				offset -= 2 * target.TargetInfo.TargetAddressSize;
			TargetLocation field_loc = location.GetLocationAtOffset (offset);

			if (finfo.Type.IsByRef)
				field_loc = field_loc.GetDereferencedLocation ();

			finfo.Type.SetObject (target, field_loc, obj);
		}

		public override TargetObject GetStaticField (Thread target, TargetFieldInfo finfo)
		{
			int offset = GetFieldOffset (target, finfo);

			TargetAddress data_address = target.CallMethod (
				File.MonoLanguage.MonoDebuggerInfo.ClassGetStaticFieldData,
				class_info.KlassAddress, 0);

			TargetLocation location = new AbsoluteTargetLocation (data_address);
			TargetLocation field_loc = location.GetLocationAtOffset (offset);

			if (finfo.Type.IsByRef)
				field_loc = field_loc.GetDereferencedLocation ();

			return finfo.Type.GetObject (field_loc);
		}

		public override void SetStaticField (Thread target, TargetFieldInfo finfo,
						     TargetObject obj)
		{
			int offset = GetFieldOffset (target, finfo);

			TargetAddress data_address = target.CallMethod (
				File.MonoLanguage.MonoDebuggerInfo.ClassGetStaticFieldData,
				class_info.KlassAddress, 0);

			TargetLocation location = new AbsoluteTargetLocation (data_address);
			TargetLocation field_loc = location.GetLocationAtOffset (offset);

			if (finfo.Type.IsByRef)
				field_loc = field_loc.GetDereferencedLocation ();

			finfo.Type.SetObject (target, field_loc, obj);
		}

		internal MonoClassObject GetParentObject (Thread target, TargetLocation location)
		{
			if (parent_type == null)
				throw new InvalidOperationException ();

			if (!IsByRef && parent_type.IsByRef) {
				TargetAddress boxed = target.CallMethod (
					File.MonoLanguage.MonoDebuggerInfo.GetBoxedObjectMethod,
					class_info.KlassAddress, location.GetAddress (target).Address);
				TargetLocation new_loc = new AbsoluteTargetLocation (boxed);
				return new MonoClassObject (parent_type, new_loc);
			}

			return new MonoClassObject (parent_type, location);
		}

		internal MonoClassObject GetCurrentObject (Thread target, TargetLocation location)
		{
			// location.Address resolves to the address of the MonoObject,
			// dereferencing it once gives us the vtable, dereferencing it
			// twice the class.
			TargetAddress address;
			address = target.ReadAddress (location.GetAddress (target));
			address = target.ReadAddress (address);

			TargetType current = MonoType.ReadMonoClass (File.MonoLanguage, target, address);
			if (current == null)
				return null;

			if (IsByRef && !current.IsByRef) // Unbox
				location = location.GetLocationAtOffset (
					2 * target.TargetInfo.TargetAddressSize);

			return (MonoClassObject) current.GetObject (location);
		}

		internal MonoClassInfo HardResolveClass (Thread target)
		{
			if (ResolveClass (target, false) != null)
				return class_info;

			TargetAddress klass_address = target.CallMethod (
				file.MonoLanguage.MonoDebuggerInfo.LookupClass,
				file.MonoImage, 0, Name);

			class_info = file.MonoLanguage.ReadClassInfo (target, klass_address);
			if (class_info == null)
				throw new InternalError ();

			return class_info;
		}
	}
}
