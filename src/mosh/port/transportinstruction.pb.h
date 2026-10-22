// TransportBuffers (third_party/mosh/src/protobufs/transportinstruction.proto)
// without libprotobuf: the generated API mosh's network code uses. See
// protobuf_wire.h.

#pragma once

#include <cstdint>
#include <string>

namespace TransportBuffers {

class Instruction {
public:
  std::uint32_t protocol_version( void ) const { return protocol_version_; }
  std::uint64_t old_num( void ) const { return old_num_; }
  std::uint64_t new_num( void ) const { return new_num_; }
  std::uint64_t ack_num( void ) const { return ack_num_; }
  std::uint64_t throwaway_num( void ) const { return throwaway_num_; }
  const std::string &diff( void ) const { return diff_; }
  const std::string &chaff( void ) const { return chaff_; }

  bool has_protocol_version( void ) const { return has_ & PROTOCOL_VERSION; }
  bool has_old_num( void ) const { return has_ & OLD_NUM; }
  bool has_new_num( void ) const { return has_ & NEW_NUM; }
  bool has_ack_num( void ) const { return has_ & ACK_NUM; }
  bool has_throwaway_num( void ) const { return has_ & THROWAWAY_NUM; }
  bool has_diff( void ) const { return has_ & DIFF; }
  bool has_chaff( void ) const { return has_ & CHAFF; }

  void set_protocol_version( std::uint32_t v ) { protocol_version_ = v; has_ |= PROTOCOL_VERSION; }
  void set_old_num( std::uint64_t v ) { old_num_ = v; has_ |= OLD_NUM; }
  void set_new_num( std::uint64_t v ) { new_num_ = v; has_ |= NEW_NUM; }
  void set_ack_num( std::uint64_t v ) { ack_num_ = v; has_ |= ACK_NUM; }
  void set_throwaway_num( std::uint64_t v ) { throwaway_num_ = v; has_ |= THROWAWAY_NUM; }
  void set_diff( const std::string &v ) { diff_ = v; has_ |= DIFF; }
  void set_chaff( const std::string &v ) { chaff_ = v; has_ |= CHAFF; }

  void Clear( void ) { *this = Instruction(); }
  std::string SerializeAsString( void ) const;
  /* Replaces the contents; false (and a cleared message) for malformed input. */
  bool ParseFromString( const std::string &data );

private:
  enum : std::uint32_t {
    PROTOCOL_VERSION = 1, OLD_NUM = 2, NEW_NUM = 4, ACK_NUM = 8, THROWAWAY_NUM = 16, DIFF = 32, CHAFF = 64
  };
  std::uint32_t has_ = 0;
  std::uint32_t protocol_version_ = 0;
  std::uint64_t old_num_ = 0;
  std::uint64_t new_num_ = 0;
  std::uint64_t ack_num_ = 0;
  std::uint64_t throwaway_num_ = 0;
  std::string diff_;
  std::string chaff_;
};

}  // namespace TransportBuffers
