// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.28.1
// 	protoc        v3.15.8
// source: yt_proto/yt/client/api/rpc_proxy/proto/discovery_service.proto

package rpc_proxy

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

type EAddressType int32

const (
	EAddressType_AT_INTERNAL_RPC          EAddressType = 0
	EAddressType_AT_MONITORING_HTTP       EAddressType = 1
	EAddressType_AT_TVM_ONLY_INTERNAL_RPC EAddressType = 2
)

// Enum value maps for EAddressType.
var (
	EAddressType_name = map[int32]string{
		0: "AT_INTERNAL_RPC",
		1: "AT_MONITORING_HTTP",
		2: "AT_TVM_ONLY_INTERNAL_RPC",
	}
	EAddressType_value = map[string]int32{
		"AT_INTERNAL_RPC":          0,
		"AT_MONITORING_HTTP":       1,
		"AT_TVM_ONLY_INTERNAL_RPC": 2,
	}
)

func (x EAddressType) Enum() *EAddressType {
	p := new(EAddressType)
	*p = x
	return p
}

func (x EAddressType) String() string {
	return protoimpl.X.EnumStringOf(x.Descriptor(), protoreflect.EnumNumber(x))
}

func (EAddressType) Descriptor() protoreflect.EnumDescriptor {
	return file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_enumTypes[0].Descriptor()
}

func (EAddressType) Type() protoreflect.EnumType {
	return &file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_enumTypes[0]
}

func (x EAddressType) Number() protoreflect.EnumNumber {
	return protoreflect.EnumNumber(x)
}

// Deprecated: Do not use.
func (x *EAddressType) UnmarshalJSON(b []byte) error {
	num, err := protoimpl.X.UnmarshalJSONEnum(x.Descriptor(), b)
	if err != nil {
		return err
	}
	*x = EAddressType(num)
	return nil
}

// Deprecated: Use EAddressType.Descriptor instead.
func (EAddressType) EnumDescriptor() ([]byte, []int) {
	return file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDescGZIP(), []int{0}
}

type TReqDiscoverProxies struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Role        *string       `protobuf:"bytes,1,opt,name=role" json:"role,omitempty"`
	AddressType *EAddressType `protobuf:"varint,2,opt,name=address_type,json=addressType,enum=NYT.NApi.NRpcProxy.NProto.EAddressType,def=0" json:"address_type,omitempty"`
	NetworkName *string       `protobuf:"bytes,3,opt,name=network_name,json=networkName" json:"network_name,omitempty"`
}

// Default values for TReqDiscoverProxies fields.
const (
	Default_TReqDiscoverProxies_AddressType = EAddressType_AT_INTERNAL_RPC
)

func (x *TReqDiscoverProxies) Reset() {
	*x = TReqDiscoverProxies{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TReqDiscoverProxies) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TReqDiscoverProxies) ProtoMessage() {}

func (x *TReqDiscoverProxies) ProtoReflect() protoreflect.Message {
	mi := &file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TReqDiscoverProxies.ProtoReflect.Descriptor instead.
func (*TReqDiscoverProxies) Descriptor() ([]byte, []int) {
	return file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDescGZIP(), []int{0}
}

func (x *TReqDiscoverProxies) GetRole() string {
	if x != nil && x.Role != nil {
		return *x.Role
	}
	return ""
}

func (x *TReqDiscoverProxies) GetAddressType() EAddressType {
	if x != nil && x.AddressType != nil {
		return *x.AddressType
	}
	return Default_TReqDiscoverProxies_AddressType
}

func (x *TReqDiscoverProxies) GetNetworkName() string {
	if x != nil && x.NetworkName != nil {
		return *x.NetworkName
	}
	return ""
}

type TRspDiscoverProxies struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Addresses []string `protobuf:"bytes,1,rep,name=addresses" json:"addresses,omitempty"`
}

func (x *TRspDiscoverProxies) Reset() {
	*x = TRspDiscoverProxies{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TRspDiscoverProxies) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TRspDiscoverProxies) ProtoMessage() {}

func (x *TRspDiscoverProxies) ProtoReflect() protoreflect.Message {
	mi := &file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TRspDiscoverProxies.ProtoReflect.Descriptor instead.
func (*TRspDiscoverProxies) Descriptor() ([]byte, []int) {
	return file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDescGZIP(), []int{1}
}

func (x *TRspDiscoverProxies) GetAddresses() []string {
	if x != nil {
		return x.Addresses
	}
	return nil
}

var File_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto protoreflect.FileDescriptor

var file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDesc = []byte{
	0x0a, 0x3e, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x79, 0x74, 0x2f, 0x63, 0x6c,
	0x69, 0x65, 0x6e, 0x74, 0x2f, 0x61, 0x70, 0x69, 0x2f, 0x72, 0x70, 0x63, 0x5f, 0x70, 0x72, 0x6f,
	0x78, 0x79, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x64, 0x69, 0x73, 0x63, 0x6f, 0x76, 0x65,
	0x72, 0x79, 0x5f, 0x73, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f,
	0x12, 0x19, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x41, 0x70, 0x69, 0x2e, 0x4e, 0x52, 0x70, 0x63, 0x50,
	0x72, 0x6f, 0x78, 0x79, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x22, 0xa9, 0x01, 0x0a, 0x13,
	0x54, 0x52, 0x65, 0x71, 0x44, 0x69, 0x73, 0x63, 0x6f, 0x76, 0x65, 0x72, 0x50, 0x72, 0x6f, 0x78,
	0x69, 0x65, 0x73, 0x12, 0x12, 0x0a, 0x04, 0x72, 0x6f, 0x6c, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28,
	0x09, 0x52, 0x04, 0x72, 0x6f, 0x6c, 0x65, 0x12, 0x5b, 0x0a, 0x0c, 0x61, 0x64, 0x64, 0x72, 0x65,
	0x73, 0x73, 0x5f, 0x74, 0x79, 0x70, 0x65, 0x18, 0x02, 0x20, 0x01, 0x28, 0x0e, 0x32, 0x27, 0x2e,
	0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x41, 0x70, 0x69, 0x2e, 0x4e, 0x52, 0x70, 0x63, 0x50, 0x72, 0x6f,
	0x78, 0x79, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x45, 0x41, 0x64, 0x64, 0x72, 0x65,
	0x73, 0x73, 0x54, 0x79, 0x70, 0x65, 0x3a, 0x0f, 0x41, 0x54, 0x5f, 0x49, 0x4e, 0x54, 0x45, 0x52,
	0x4e, 0x41, 0x4c, 0x5f, 0x52, 0x50, 0x43, 0x52, 0x0b, 0x61, 0x64, 0x64, 0x72, 0x65, 0x73, 0x73,
	0x54, 0x79, 0x70, 0x65, 0x12, 0x21, 0x0a, 0x0c, 0x6e, 0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b, 0x5f,
	0x6e, 0x61, 0x6d, 0x65, 0x18, 0x03, 0x20, 0x01, 0x28, 0x09, 0x52, 0x0b, 0x6e, 0x65, 0x74, 0x77,
	0x6f, 0x72, 0x6b, 0x4e, 0x61, 0x6d, 0x65, 0x22, 0x33, 0x0a, 0x13, 0x54, 0x52, 0x73, 0x70, 0x44,
	0x69, 0x73, 0x63, 0x6f, 0x76, 0x65, 0x72, 0x50, 0x72, 0x6f, 0x78, 0x69, 0x65, 0x73, 0x12, 0x1c,
	0x0a, 0x09, 0x61, 0x64, 0x64, 0x72, 0x65, 0x73, 0x73, 0x65, 0x73, 0x18, 0x01, 0x20, 0x03, 0x28,
	0x09, 0x52, 0x09, 0x61, 0x64, 0x64, 0x72, 0x65, 0x73, 0x73, 0x65, 0x73, 0x2a, 0x59, 0x0a, 0x0c,
	0x45, 0x41, 0x64, 0x64, 0x72, 0x65, 0x73, 0x73, 0x54, 0x79, 0x70, 0x65, 0x12, 0x13, 0x0a, 0x0f,
	0x41, 0x54, 0x5f, 0x49, 0x4e, 0x54, 0x45, 0x52, 0x4e, 0x41, 0x4c, 0x5f, 0x52, 0x50, 0x43, 0x10,
	0x00, 0x12, 0x16, 0x0a, 0x12, 0x41, 0x54, 0x5f, 0x4d, 0x4f, 0x4e, 0x49, 0x54, 0x4f, 0x52, 0x49,
	0x4e, 0x47, 0x5f, 0x48, 0x54, 0x54, 0x50, 0x10, 0x01, 0x12, 0x1c, 0x0a, 0x18, 0x41, 0x54, 0x5f,
	0x54, 0x56, 0x4d, 0x5f, 0x4f, 0x4e, 0x4c, 0x59, 0x5f, 0x49, 0x4e, 0x54, 0x45, 0x52, 0x4e, 0x41,
	0x4c, 0x5f, 0x52, 0x50, 0x43, 0x10, 0x02, 0x42, 0x52, 0x0a, 0x0d, 0x74, 0x65, 0x63, 0x68, 0x2e,
	0x79, 0x74, 0x73, 0x61, 0x75, 0x72, 0x75, 0x73, 0x42, 0x0f, 0x44, 0x69, 0x73, 0x63, 0x6f, 0x76,
	0x65, 0x72, 0x79, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x73, 0x50, 0x01, 0x5a, 0x2e, 0x79, 0x74, 0x73,
	0x61, 0x75, 0x72, 0x75, 0x73, 0x2e, 0x74, 0x65, 0x63, 0x68, 0x2f, 0x79, 0x74, 0x2f, 0x67, 0x6f,
	0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2f, 0x61, 0x70,
	0x69, 0x2f, 0x72, 0x70, 0x63, 0x5f, 0x70, 0x72, 0x6f, 0x78, 0x79,
}

var (
	file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDescOnce sync.Once
	file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDescData = file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDesc
)

func file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDescGZIP() []byte {
	file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDescOnce.Do(func() {
		file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDescData = protoimpl.X.CompressGZIP(file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDescData)
	})
	return file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDescData
}

var file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_enumTypes = make([]protoimpl.EnumInfo, 1)
var file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_msgTypes = make([]protoimpl.MessageInfo, 2)
var file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_goTypes = []interface{}{
	(EAddressType)(0),           // 0: NYT.NApi.NRpcProxy.NProto.EAddressType
	(*TReqDiscoverProxies)(nil), // 1: NYT.NApi.NRpcProxy.NProto.TReqDiscoverProxies
	(*TRspDiscoverProxies)(nil), // 2: NYT.NApi.NRpcProxy.NProto.TRspDiscoverProxies
}
var file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_depIdxs = []int32{
	0, // 0: NYT.NApi.NRpcProxy.NProto.TReqDiscoverProxies.address_type:type_name -> NYT.NApi.NRpcProxy.NProto.EAddressType
	1, // [1:1] is the sub-list for method output_type
	1, // [1:1] is the sub-list for method input_type
	1, // [1:1] is the sub-list for extension type_name
	1, // [1:1] is the sub-list for extension extendee
	0, // [0:1] is the sub-list for field type_name
}

func init() { file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_init() }
func file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_init() {
	if File_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TReqDiscoverProxies); i {
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
		file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TRspDiscoverProxies); i {
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
			RawDescriptor: file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDesc,
			NumEnums:      1,
			NumMessages:   2,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_goTypes,
		DependencyIndexes: file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_depIdxs,
		EnumInfos:         file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_enumTypes,
		MessageInfos:      file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_msgTypes,
	}.Build()
	File_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto = out.File
	file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_rawDesc = nil
	file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_goTypes = nil
	file_yt_proto_yt_client_api_rpc_proxy_proto_discovery_service_proto_depIdxs = nil
}
