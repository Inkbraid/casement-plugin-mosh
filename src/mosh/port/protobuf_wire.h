// The protocol buffers wire format, as far as mosh's three message files
// need it: varints, length-delimited fields and skipping what isn't known.
//
// mosh's messages (src/protobufs/*.proto in third_party/mosh) are small and
// fixed, so the plugin encodes and decodes them with the classes in
// hostinput.pb.h, transportinstruction.pb.h and userinput.pb.h, which offer
// the part of protoc's generated API that mosh's code calls, instead of
// linking libprotobuf. The bytes on the wire are the same as with protobuf
// 3.21 (the unit tests compare them with protoc's output).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mosh_protobuf {

enum WireType { VARINT = 0, FIXED64 = 1, LENGTH_DELIMITED = 2, START_GROUP = 3, END_GROUP = 4, FIXED32 = 5 };

void put_varint( std::string &out, std::uint64_t value );
void put_tag( std::string &out, std::uint32_t field, WireType type );
void put_uint( std::string &out, std::uint32_t field, std::uint64_t value );
/* int32 fields: negative values are sign-extended to ten bytes, as protobuf does. */
void put_int32( std::string &out, std::uint32_t field, std::int32_t value );
void put_bytes( std::string &out, std::uint32_t field, const std::string &value );

/* Reads fields one after another; any malformed input makes ok() false. */
class Reader {
public:
  Reader( const char *data, std::size_t size ) : p_( data ), end_( data + size ), ok_( true ) {}
  explicit Reader( const std::string &s ) : Reader( s.data(), s.size() ) {}

  bool ok( void ) const { return ok_; }
  bool at_end( void ) const { return p_ == end_; }

  /* The next field's number and wire type; false at the end or on an error. */
  bool next( std::uint32_t &field, WireType &type );

  bool read_varint( std::uint64_t &value );
  bool read_bytes( std::string &value );
  /* Skips a field's value (groups included). */
  bool skip( std::uint32_t field, WireType type, int depth = 0 );

private:
  bool fail( void ) { ok_ = false; return false; }

  const char *p_;
  const char *end_;
  bool ok_;
};

}  // namespace mosh_protobuf
