// ClientBuffers (third_party/mosh/src/protobufs/userinput.proto) without
// libprotobuf: the generated API mosh's state synchronisation uses,
// extensions included. See protobuf_wire.h.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ClientBuffers {

class Keystroke {
public:
  const std::string &keys( void ) const { return keys_; }
  bool has_keys( void ) const { return has_keys_; }
  void set_keys( const std::string &v ) { keys_ = v; has_keys_ = true; }
  void set_keys( const char *v, std::size_t n ) { keys_.assign( v, n ); has_keys_ = true; }
  std::string *mutable_keys( void ) { has_keys_ = true; return &keys_; }

  void append_to( std::string &out ) const;
  bool merge_from( const std::string &data );

private:
  bool has_keys_ = false;
  std::string keys_;
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

/* The extensions of Instruction, named as protoc names them. */
struct KeystrokeExtension {};
struct ResizeExtension {};
inline constexpr KeystrokeExtension keystroke {};
inline constexpr ResizeExtension resize {};

class Instruction {
public:
  bool HasExtension( KeystrokeExtension ) const { return has_keystroke_; }
  bool HasExtension( ResizeExtension ) const { return has_resize_; }
  const Keystroke &GetExtension( KeystrokeExtension ) const { return keystroke_; }
  const ResizeMessage &GetExtension( ResizeExtension ) const { return resize_; }
  Keystroke *MutableExtension( KeystrokeExtension ) { has_keystroke_ = true; return &keystroke_; }
  ResizeMessage *MutableExtension( ResizeExtension ) { has_resize_ = true; return &resize_; }

  void append_to( std::string &out ) const;
  bool merge_from( const std::string &data );

private:
  bool has_keystroke_ = false;
  bool has_resize_ = false;
  Keystroke keystroke_;
  ResizeMessage resize_;
};

class UserMessage {
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

}  // namespace ClientBuffers
