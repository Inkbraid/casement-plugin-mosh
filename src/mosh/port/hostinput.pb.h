// HostBuffers (third_party/mosh/src/protobufs/hostinput.proto) without
// libprotobuf: the generated API mosh's state synchronisation uses,
// extensions included. See protobuf_wire.h.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace HostBuffers {

class HostBytes {
public:
  const std::string &hoststring( void ) const { return hoststring_; }
  bool has_hoststring( void ) const { return has_hoststring_; }
  void set_hoststring( const std::string &v ) { hoststring_ = v; has_hoststring_ = true; }

  void append_to( std::string &out ) const;
  bool merge_from( const std::string &data );

private:
  bool has_hoststring_ = false;
  std::string hoststring_;
};

class ResizeMessage {
public:
  std::int32_t width( void ) const { return width_; }
  std::int32_t height( void ) const { return height_; }
  bool has_width( void ) const { return has_width_; }
  bool has_height( void ) const { return has_height_; }
  void set_width( std::int32_t v ) { width_ = v; has_width_ = true; }
  void set_height( std::int32_t v ) { height_ = v; has_height_ = true; }

  void append_to( std::string &out ) const;
  bool merge_from( const std::string &data );

private:
  bool has_width_ = false;
  bool has_height_ = false;
  std::int32_t width_ = 0;
  std::int32_t height_ = 0;
};

class EchoAck {
public:
  std::uint64_t echo_ack_num( void ) const { return echo_ack_num_; }
  bool has_echo_ack_num( void ) const { return has_echo_ack_num_; }
  void set_echo_ack_num( std::uint64_t v ) { echo_ack_num_ = v; has_echo_ack_num_ = true; }

  void append_to( std::string &out ) const;
  bool merge_from( const std::string &data );

private:
  bool has_echo_ack_num_ = false;
  std::uint64_t echo_ack_num_ = 0;
};

/* The extensions of Instruction, named as protoc names them. */
struct HostBytesExtension {};
struct ResizeExtension {};
struct EchoAckExtension {};
inline constexpr HostBytesExtension hostbytes {};
inline constexpr ResizeExtension resize {};
inline constexpr EchoAckExtension echoack {};

class Instruction {
public:
  bool HasExtension( HostBytesExtension ) const { return has_hostbytes_; }
  bool HasExtension( ResizeExtension ) const { return has_resize_; }
  bool HasExtension( EchoAckExtension ) const { return has_echoack_; }
  const HostBytes &GetExtension( HostBytesExtension ) const { return hostbytes_; }
  const ResizeMessage &GetExtension( ResizeExtension ) const { return resize_; }
  const EchoAck &GetExtension( EchoAckExtension ) const { return echoack_; }
  HostBytes *MutableExtension( HostBytesExtension ) { has_hostbytes_ = true; return &hostbytes_; }
  ResizeMessage *MutableExtension( ResizeExtension ) { has_resize_ = true; return &resize_; }
  EchoAck *MutableExtension( EchoAckExtension ) { has_echoack_ = true; return &echoack_; }

  void append_to( std::string &out ) const;
  bool merge_from( const std::string &data );

private:
  bool has_hostbytes_ = false;
  bool has_resize_ = false;
  bool has_echoack_ = false;
  HostBytes hostbytes_;
  ResizeMessage resize_;
  EchoAck echoack_;
};

class HostMessage {
public:
  int instruction_size( void ) const { return static_cast<int>( instruction_.size() ); }
  const Instruction &instruction( int i ) const { return instruction_.at( static_cast<std::size_t>( i ) ); }
  Instruction *mutable_instruction( int i ) { return &instruction_.at( static_cast<std::size_t>( i ) ); }
  Instruction *add_instruction( void ) { instruction_.emplace_back(); return &instruction_.back(); }

  void Clear( void ) { instruction_.clear(); }
  std::string SerializeAsString( void ) const;
  /* Replaces the contents; false (and a cleared message) for malformed input. */
  bool ParseFromString( const std::string &data );

private:
  std::vector<Instruction> instruction_;
};

}  // namespace HostBuffers
