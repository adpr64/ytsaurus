// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.28.1
// 	protoc        v3.15.8
// source: yt_proto/yt/core/misc/proto/protobuf_helpers.proto

package misc

import (
	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	reflect "reflect"
	sync "sync"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

type TSerializedMessageEnvelope struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Codec *int32 `protobuf:"varint,1,opt,name=codec,def=0" json:"codec,omitempty"`
}

// Default values for TSerializedMessageEnvelope fields.
const (
	Default_TSerializedMessageEnvelope_Codec = int32(0)
)

func (x *TSerializedMessageEnvelope) Reset() {
	*x = TSerializedMessageEnvelope{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TSerializedMessageEnvelope) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TSerializedMessageEnvelope) ProtoMessage() {}

func (x *TSerializedMessageEnvelope) ProtoReflect() protoreflect.Message {
	mi := &file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TSerializedMessageEnvelope.ProtoReflect.Descriptor instead.
func (*TSerializedMessageEnvelope) Descriptor() ([]byte, []int) {
	return file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDescGZIP(), []int{0}
}

func (x *TSerializedMessageEnvelope) GetCodec() int32 {
	if x != nil && x.Codec != nil {
		return *x.Codec
	}
	return Default_TSerializedMessageEnvelope_Codec
}

type TExtension struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Tag  *int32 `protobuf:"varint,1,req,name=tag" json:"tag,omitempty"`
	Data []byte `protobuf:"bytes,2,req,name=data" json:"data,omitempty"`
}

func (x *TExtension) Reset() {
	*x = TExtension{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TExtension) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TExtension) ProtoMessage() {}

func (x *TExtension) ProtoReflect() protoreflect.Message {
	mi := &file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TExtension.ProtoReflect.Descriptor instead.
func (*TExtension) Descriptor() ([]byte, []int) {
	return file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDescGZIP(), []int{1}
}

func (x *TExtension) GetTag() int32 {
	if x != nil && x.Tag != nil {
		return *x.Tag
	}
	return 0
}

func (x *TExtension) GetData() []byte {
	if x != nil {
		return x.Data
	}
	return nil
}

type TExtensionSet struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Extensions []*TExtension `protobuf:"bytes,1,rep,name=extensions" json:"extensions,omitempty"`
}

func (x *TExtensionSet) Reset() {
	*x = TExtensionSet{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_msgTypes[2]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TExtensionSet) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TExtensionSet) ProtoMessage() {}

func (x *TExtensionSet) ProtoReflect() protoreflect.Message {
	mi := &file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_msgTypes[2]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TExtensionSet.ProtoReflect.Descriptor instead.
func (*TExtensionSet) Descriptor() ([]byte, []int) {
	return file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDescGZIP(), []int{2}
}

func (x *TExtensionSet) GetExtensions() []*TExtension {
	if x != nil {
		return x.Extensions
	}
	return nil
}

var File_yt_proto_yt_core_misc_proto_protobuf_helpers_proto protoreflect.FileDescriptor

var file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDesc = []byte{
	0x0a, 0x32, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x79, 0x74, 0x2f, 0x63, 0x6f,
	0x72, 0x65, 0x2f, 0x6d, 0x69, 0x73, 0x63, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x70, 0x72,
	0x6f, 0x74, 0x6f, 0x62, 0x75, 0x66, 0x5f, 0x68, 0x65, 0x6c, 0x70, 0x65, 0x72, 0x73, 0x2e, 0x70,
	0x72, 0x6f, 0x74, 0x6f, 0x12, 0x0a, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f,
	0x22, 0x35, 0x0a, 0x1a, 0x54, 0x53, 0x65, 0x72, 0x69, 0x61, 0x6c, 0x69, 0x7a, 0x65, 0x64, 0x4d,
	0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x45, 0x6e, 0x76, 0x65, 0x6c, 0x6f, 0x70, 0x65, 0x12, 0x17,
	0x0a, 0x05, 0x63, 0x6f, 0x64, 0x65, 0x63, 0x18, 0x01, 0x20, 0x01, 0x28, 0x05, 0x3a, 0x01, 0x30,
	0x52, 0x05, 0x63, 0x6f, 0x64, 0x65, 0x63, 0x22, 0x32, 0x0a, 0x0a, 0x54, 0x45, 0x78, 0x74, 0x65,
	0x6e, 0x73, 0x69, 0x6f, 0x6e, 0x12, 0x10, 0x0a, 0x03, 0x74, 0x61, 0x67, 0x18, 0x01, 0x20, 0x02,
	0x28, 0x05, 0x52, 0x03, 0x74, 0x61, 0x67, 0x12, 0x12, 0x0a, 0x04, 0x64, 0x61, 0x74, 0x61, 0x18,
	0x02, 0x20, 0x02, 0x28, 0x0c, 0x52, 0x04, 0x64, 0x61, 0x74, 0x61, 0x22, 0x47, 0x0a, 0x0d, 0x54,
	0x45, 0x78, 0x74, 0x65, 0x6e, 0x73, 0x69, 0x6f, 0x6e, 0x53, 0x65, 0x74, 0x12, 0x36, 0x0a, 0x0a,
	0x65, 0x78, 0x74, 0x65, 0x6e, 0x73, 0x69, 0x6f, 0x6e, 0x73, 0x18, 0x01, 0x20, 0x03, 0x28, 0x0b,
	0x32, 0x16, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x45,
	0x78, 0x74, 0x65, 0x6e, 0x73, 0x69, 0x6f, 0x6e, 0x52, 0x0a, 0x65, 0x78, 0x74, 0x65, 0x6e, 0x73,
	0x69, 0x6f, 0x6e, 0x73, 0x42, 0x36, 0x0a, 0x0d, 0x74, 0x65, 0x63, 0x68, 0x2e, 0x79, 0x74, 0x73,
	0x61, 0x75, 0x72, 0x75, 0x73, 0x50, 0x01, 0x5a, 0x23, 0x79, 0x74, 0x73, 0x61, 0x75, 0x72, 0x75,
	0x73, 0x2e, 0x74, 0x65, 0x63, 0x68, 0x2f, 0x79, 0x74, 0x2f, 0x67, 0x6f, 0x2f, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x6d, 0x69, 0x73, 0x63,
}

var (
	file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDescOnce sync.Once
	file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDescData = file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDesc
)

func file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDescGZIP() []byte {
	file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDescOnce.Do(func() {
		file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDescData = protoimpl.X.CompressGZIP(file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDescData)
	})
	return file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDescData
}

var file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_msgTypes = make([]protoimpl.MessageInfo, 3)
var file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_goTypes = []interface{}{
	(*TSerializedMessageEnvelope)(nil), // 0: NYT.NProto.TSerializedMessageEnvelope
	(*TExtension)(nil),                 // 1: NYT.NProto.TExtension
	(*TExtensionSet)(nil),              // 2: NYT.NProto.TExtensionSet
}
var file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_depIdxs = []int32{
	1, // 0: NYT.NProto.TExtensionSet.extensions:type_name -> NYT.NProto.TExtension
	1, // [1:1] is the sub-list for method output_type
	1, // [1:1] is the sub-list for method input_type
	1, // [1:1] is the sub-list for extension type_name
	1, // [1:1] is the sub-list for extension extendee
	0, // [0:1] is the sub-list for field type_name
}

func init() { file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_init() }
func file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_init() {
	if File_yt_proto_yt_core_misc_proto_protobuf_helpers_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TSerializedMessageEnvelope); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TExtension); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_msgTypes[2].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TExtensionSet); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
	}
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   3,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_goTypes,
		DependencyIndexes: file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_depIdxs,
		MessageInfos:      file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_msgTypes,
	}.Build()
	File_yt_proto_yt_core_misc_proto_protobuf_helpers_proto = out.File
	file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_rawDesc = nil
	file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_goTypes = nil
	file_yt_proto_yt_core_misc_proto_protobuf_helpers_proto_depIdxs = nil
}
