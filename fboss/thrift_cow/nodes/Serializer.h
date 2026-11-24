/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <folly/DynamicConverter.h>
#include <folly/io/IOBufQueue.h>
#include <thrift/lib/cpp2/folly_dynamic/folly_dynamic.h>
#include <thrift/lib/cpp2/op/Encode.h>
#include <thrift/lib/cpp2/op/Get.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <thrift/lib/cpp2/type/detail/TypeClassToTypeTag.h>
#include <utility>
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"

namespace facebook::fboss::thrift_cow {

// Parse keys for maps and sets. Keeping this outside nodes to reduce
// instatiations because it only depends on the type and tc of the key, not the
// type of the node itself.
template <typename KeyT, typename KeyTC>
std::optional<KeyT> tryParseKey(const std::string& token) {
  if constexpr (std::
                    is_same_v<KeyTC, apache::thrift::type_class::enumeration>) {
    // special handling for enum keyed maps
    KeyT enumKey;
    if (apache::thrift::util::tryParseEnum(token, &enumKey)) {
      return enumKey;
    }
  }
  auto key = folly::tryTo<KeyT>(token);
  if (key.hasValue()) {
    return *key;
  }

  return std::nullopt;
}

struct FieldBaseType {};

template <typename T>
using is_field_type = std::is_base_of<FieldBaseType, std::remove_cvref_t<T>>;
template <typename T>
constexpr bool is_field_type_v = is_field_type<T>::value;

namespace detail {

template <typename TC>
constexpr bool tc_is_struct_or_union =
    std::is_same_v<TC, apache::thrift::type_class::structure> ||
    std::is_same_v<TC, apache::thrift::type_class::variant>;

}

template <fsdb::OperProtocol Protocol>
struct ProtocolSerializers {
  static_assert(Protocol != Protocol, "Unsupported Protocol");
};

template <>
struct ProtocolSerializers<fsdb::OperProtocol::BINARY> {
  using Reader = apache::thrift::BinaryProtocolReader;
  using Writer = apache::thrift::BinaryProtocolWriter;
};

template <>
struct ProtocolSerializers<fsdb::OperProtocol::SIMPLE_JSON> {
  using Reader = apache::thrift::SimpleJSONProtocolReader;
  using Writer = apache::thrift::SimpleJSONProtocolWriter;
};

template <>
struct ProtocolSerializers<fsdb::OperProtocol::COMPACT> {
  using Reader = apache::thrift::CompactProtocolReader;
  using Writer = apache::thrift::CompactProtocolWriter;
};

template <fsdb::OperProtocol Protocol>
struct Serializer {
  using Serializers = ProtocolSerializers<Protocol>;
  using Reader = typename Serializers::Reader;
  using Writer = typename Serializers::Writer;
  using TSerializer = apache::thrift::Serializer<Reader, Writer>;

  // default size to serialize is 1KB instead of 16KB in writer::setOutput()
  static const size_t maxGrowth = 1024;

  template <typename TC, typename TType>
  static folly::fbstring serialize(const TType& ttype)
    requires(detail::tc_is_struct_or_union<TC>)
  {
    folly::fbstring encoded;
    TSerializer::serialize(ttype, &encoded);
    encoded.shrink_to_fit();
    return encoded;
  }

  template <typename TC, typename TType>
  static folly::fbstring serialize(const TType& ttype)
    requires(!detail::tc_is_struct_or_union<TC>)
  {
    auto str = serializeBuf<TC>(ttype).moveToFbString();
    str.shrink_to_fit();
    return str;
  }

  template <typename TC, typename TType>
  static folly::IOBuf serializeBuf(const TType& ttype)
    requires(detail::tc_is_struct_or_union<TC>)
  {
    folly::IOBufQueue queue;
    Writer writer;
    writer.setOutput(&queue, maxGrowth);
    apache::thrift::Cpp2Ops<TType>::write(&writer, &ttype);
    return queue.moveAsValue();
  }

  template <typename TC, typename TType>
  static folly::IOBuf serializeBuf(const TType& ttype)
    requires(!detail::tc_is_struct_or_union<TC>)
  {
    folly::IOBufQueue queue;
    Writer writer;
    writer.setOutput(&queue, maxGrowth);
    using Tag = apache::thrift::type_class::to_type_tag_t<TC, TType>;
    apache::thrift::op::encode<Tag>(writer, ttype);
    return queue.moveAsValue();
  }

  template <typename TC, typename TType>
  static TType deserialize(const folly::fbstring& encoded)
    requires(detail::tc_is_struct_or_union<TC>)
  {
    return TSerializer::template deserialize<TType>(encoded.toStdString());
  }

  template <typename TC, typename TType>
  static TType deserialize(const folly::fbstring& encoded)
    requires(!detail::tc_is_struct_or_union<TC>)
  {
    Reader reader;
    auto buf = folly::IOBuf::copyBuffer(encoded.data(), encoded.length());
    reader.setInput(buf.get());
    TType recovered;
    using Tag = apache::thrift::type_class::to_type_tag_t<TC, TType>;
    apache::thrift::op::decode<Tag>(reader, recovered);
    return recovered;
  }

  template <typename TC, typename TType>
  static TType deserializeBuf(folly::IOBuf&& buf)
    requires(detail::tc_is_struct_or_union<TC>)
  {
    return TSerializer::template deserialize<TType>(&buf);
  }

  template <typename TC, typename TType>
  static TType deserializeBuf(folly::IOBuf&& buf)
    requires(!detail::tc_is_struct_or_union<TC>)
  {
    Reader reader;
    reader.setInput(&buf);
    TType recovered;
    using Tag = apache::thrift::type_class::to_type_tag_t<TC, TType>;
    apache::thrift::op::decode<Tag>(reader, recovered);
    return recovered;
  }
};

template <typename TC, typename TType>
folly::fbstring serialize(fsdb::OperProtocol proto, const TType& ttype) {
  switch (proto) {
    case fsdb::OperProtocol::BINARY:
      return Serializer<fsdb::OperProtocol::BINARY>::template serialize<TC>(
          ttype);
    case fsdb::OperProtocol::SIMPLE_JSON:
      return Serializer<fsdb::OperProtocol::SIMPLE_JSON>::template serialize<
          TC>(ttype);
    case fsdb::OperProtocol::COMPACT:
      return Serializer<fsdb::OperProtocol::COMPACT>::template serialize<TC>(
          ttype);
    default:
      throw std::logic_error("Unexpected protocol");
  }
}

template <typename TC, typename TType>
folly::IOBuf serializeBuf(fsdb::OperProtocol proto, const TType& ttype) {
  switch (proto) {
    case fsdb::OperProtocol::BINARY:
      return Serializer<fsdb::OperProtocol::BINARY>::template serializeBuf<TC>(
          ttype);
    case fsdb::OperProtocol::SIMPLE_JSON:
      return Serializer<fsdb::OperProtocol::SIMPLE_JSON>::template serializeBuf<
          TC>(ttype);
    case fsdb::OperProtocol::COMPACT:
      return Serializer<fsdb::OperProtocol::COMPACT>::template serializeBuf<TC>(
          ttype);
    default:
      throw std::logic_error("Unexpected protocol");
  }
}

template <typename TC, typename TType>
TType deserialize(fsdb::OperProtocol proto, const folly::fbstring& encoded) {
  switch (proto) {
    case fsdb::OperProtocol::BINARY:
      return Serializer<
          fsdb::OperProtocol::BINARY>::template deserialize<TC, TType>(encoded);
    case fsdb::OperProtocol::SIMPLE_JSON:
      return Serializer<fsdb::OperProtocol::SIMPLE_JSON>::
          template deserialize<TC, TType>(encoded);
    case fsdb::OperProtocol::COMPACT:
      return Serializer<fsdb::OperProtocol::COMPACT>::
          template deserialize<TC, TType>(encoded);
    default:
      throw std::logic_error("Unexpected protocol");
  }
}

template <typename TC, typename TType>
TType deserializeBuf(fsdb::OperProtocol proto, folly::IOBuf&& encoded) {
  switch (proto) {
    case fsdb::OperProtocol::BINARY:
      return Serializer<fsdb::OperProtocol::BINARY>::
          template deserializeBuf<TC, TType>(std::move(encoded));
    case fsdb::OperProtocol::SIMPLE_JSON:
      return Serializer<fsdb::OperProtocol::SIMPLE_JSON>::
          template deserializeBuf<TC, TType>(std::move(encoded));
    case fsdb::OperProtocol::COMPACT:
      return Serializer<fsdb::OperProtocol::COMPACT>::
          template deserializeBuf<TC, TType>(std::move(encoded));
    default:
      throw std::logic_error("Unexpected protocol");
  }
}

struct Serializable {
  virtual ~Serializable() = default;

  folly::fbstring encode(fsdb::OperProtocol proto) const {
    auto str = encodeBuf(proto).moveToFbString();
    str.shrink_to_fit();
    return str;
  }

  virtual folly::IOBuf encodeBuf(fsdb::OperProtocol proto) const = 0;

  void fromEncoded(fsdb::OperProtocol proto, const folly::fbstring& encoded) {
    auto buf =
        folly::IOBuf::wrapBufferAsValue(encoded.data(), encoded.length());
    fromEncodedBuf(proto, std::move(buf));
  }

  virtual void fromEncodedBuf(
      fsdb::OperProtocol proto,
      folly::IOBuf&& encoded) = 0;

  virtual folly::dynamic toFollyDynamic() const = 0;

  virtual void modify(const std::string& token, bool construct = true) = 0;

  virtual bool remove(const std::string& token) = 0;
};

template <typename TC>
struct WritableImpl;

/**
 * primitives
 */
template <typename TC>
struct WritableImpl {
  static_assert(
      !std::is_same<apache::thrift::type_class::unknown, TC>::value,
      "No static reflection support for the given type. "
      "Forgot to specify reflection option or include fatal header file? "
      "Refer to thrift/lib/cpp2/reflection/reflection.h");
  template <typename TType>
  static inline bool remove(TType&, const std::string& token) {
    throw std::runtime_error(
        folly::to<std::string>(
            "Cannot remove a child from a primitive node: ", token));
  }

  template <typename TType>
  static inline void
  modify(TType& node, const std::string& token, bool construct) {
    throw std::runtime_error("Cannot mutate an immutable primitive node");
  }
};

/**
 * structure
 */
template <>
struct WritableImpl<apache::thrift::type_class::structure> {
  template <typename TType>
  static inline bool remove(TType& node, const std::string& token) {
    bool removed = false;
    apache::thrift::op::for_each_field_id<TType>([&]<class Id>(Id) {
      auto fieldName = apache::thrift::op::get_field_name<TType, Id>();
      if (fieldName != token) {
        return;
      }
      auto field_ref = apache::thrift::op::get<Id>(node);
      if constexpr (apache::thrift::detail::is_optional_field_ref_v<
                        std::remove_cvref_t<decltype(field_ref)>>) {
        if (field_ref.has_value()) {
          field_ref.reset();
          removed = true;
        }
      }
    });
    return removed;
  }

  template <typename TType>
  static inline void
  modify(TType& node, const std::string& token, bool construct) {
    if (!construct) {
      return;
    }
    apache::thrift::op::for_each_field_id<TType>([&]<class Id>(Id) {
      auto fieldName = apache::thrift::op::get_field_name<TType, Id>();
      if (fieldName != token) {
        return;
      }
      auto field_ref = apache::thrift::op::get<Id>(node);
      if constexpr (apache::thrift::detail::is_optional_field_ref_v<
                        std::remove_cvref_t<decltype(field_ref)>>) {
        if (!field_ref.has_value()) {
          field_ref.ensure();
        }
      }
    });
  }
};

/**
 * Variant
 */
template <>
struct WritableImpl<apache::thrift::type_class::variant> {
  template <typename TType>
  static inline bool remove(TType&, const std::string&) {
    throw std::runtime_error("not implemented remove variant");
  }

  template <typename TType>
  static inline void
  modify(TType& node, const std::string& token, bool construct) {
    throw std::runtime_error("not implemented modify variant");
  }
};

/**
 * Map
 */
template <typename KeyTypeClass, typename MappedTypeClass>
struct WritableImpl<
    apache::thrift::type_class::map<KeyTypeClass, MappedTypeClass>> {
  template <typename TType>
  static inline bool remove(TType& node, const std::string& token) {
    if constexpr (std::is_same_v<typename TType::key_type, std::string>) {
      return node.erase(token);
    } else if (auto key = folly::tryTo<typename TType::key_type>(token)) {
      return node.erase(key.value());
    }
    return false;
  }

  template <typename TType>
  static inline void
  modify(TType& node, const std::string& token, bool construct) {
    if (auto parsedKey =
            tryParseKey<typename TType::key_type, KeyTypeClass>(token)) {
      if (auto it = node.find(parsedKey.value()); it != node.end()) {
        // key exists
      } else if (construct) {
        // create unpublished default constructed child if missing
        node.try_emplace(parsedKey.value());
      }
    }
  }
};

/**
 * List
 */
template <typename ValueTypeClass>
struct WritableImpl<apache::thrift::type_class::list<ValueTypeClass>> {
  template <typename TType>
  static inline bool remove(TType& node, const std::string& token) {
    auto index = folly::tryTo<std::size_t>(token);
    if (index.hasValue()) {
      if (index.value() < node.size()) {
        node.erase(node.begin() + index.value());
        return true;
      }
    }
    return false;
  }

  template <typename TType>
  static inline void
  modify(TType& node, const std::string& token, bool construct) {
    auto index = folly::tryTo<std::size_t>(token);
    if (construct) {
      if (index.hasValue()) {
        while (node.size() <= index.value()) {
          node.emplace_back();
        }
      }
    }
  }
};

/**
 * Set
 */
template <typename ValueTypeClass>
struct WritableImpl<apache::thrift::type_class::set<ValueTypeClass>> {
  template <typename TType>
  static inline bool remove(TType& node, const std::string& token) {
    if constexpr (std::is_same_v<typename TType::value_type, std::string>) {
      return node.erase(token);
    } else if (auto key = folly::tryTo<typename TType::value_type>(token)) {
      return node.erase(key.value());
    }
    return false;
  }

  template <typename TType>
  static inline void
  modify(TType& node, const std::string& token, bool construct) {
    if (!construct) {
      return;
    }
    if constexpr (std::is_same_v<typename TType::value_type, std::string>) {
      node.insert(token);
    } else if (auto key = folly::tryTo<typename TType::value_type>(token)) {
      node.insert(key.value());
    }
  }
};

struct ThriftObject {};

template <typename TClass, typename TType>
class SerializableWrapper : public Serializable {
 public:
  using TC = TClass;
  using ThriftType = TType;
  using CowType = ThriftObject;
  explicit SerializableWrapper(TType& node) : node_(node) {}

  folly::IOBuf encodeBuf(fsdb::OperProtocol proto) const override {
    switch (proto) {
      case fsdb::OperProtocol::BINARY:
        return Serializer<fsdb::OperProtocol::BINARY>::template serializeBuf<
            TC>(node_);
      case fsdb::OperProtocol::SIMPLE_JSON:
        return Serializer<
            fsdb::OperProtocol::SIMPLE_JSON>::template serializeBuf<TC>(node_);
      case fsdb::OperProtocol::COMPACT:
        return Serializer<fsdb::OperProtocol::COMPACT>::template serializeBuf<
            TC>(node_);
      default:
        throw std::runtime_error(
            folly::to<std::string>(
                "Unknown protocol: ", static_cast<int>(proto)));
    }
  }

  void fromEncodedBuf(fsdb::OperProtocol proto, folly::IOBuf&& encoded)
      override {
    if constexpr (std::is_const<TType>::value) {
      throw std::runtime_error(
          folly::to<std::string>(
              "deserialize assigns to const type: ",
              folly::demangle(typeid(TType)).toStdString()));
    } else {
      node_ = deserializeBuf<TC, TType>(proto, std::move(encoded));
    }
  }

#ifdef ENABLE_DYNAMIC_APIS
  folly::dynamic toFollyDynamic() const override {
    folly::dynamic dyn;
    facebook::thrift::to_dynamic(
        dyn, node_, facebook::thrift::dynamic_format::JSON_1);
    return dyn;
  }
#else
  folly::dynamic toFollyDynamic() const override {
    return {};
  }
#endif

  TType& toThrift() {
    return node_;
  }

  void fromThrift(const TType& val) {
    node_ = val;
  }

  virtual bool remove(const std::string& token) override {
    if constexpr (std::is_const<TType>::value) {
      throw std::runtime_error(
          folly::to<std::string>(
              "remove(token) called on const type: ",
              folly::demangle(typeid(TType)).toStdString()));
    } else {
      return WritableImpl<TC>::remove(node_, token);
    }
  }

  virtual void modify(const std::string& token, bool construct = true)
      override {
    if constexpr (std::is_const<TType>::value) {
      throw std::runtime_error(
          folly::to<std::string>(
              "modify(token) called on const type: ",
              folly::demangle(typeid(TType)).toStdString()));
    } else {
      WritableImpl<TC>::modify(node_, token, construct);
    }
  }

 private:
  TType& node_;
};

} // namespace facebook::fboss::thrift_cow
