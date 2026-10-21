// Encoding and decoding of mosh's protocol buffers messages. Fields are
// written in field-number order, set fields only, as protobuf's generated
// code writes them. Reading follows protobuf's rules: unknown fields and
// fields with an unexpected wire type are skipped, a repeated scalar field
// keeps the last value, and a message field that occurs twice is merged.

#include "protobuf_wire.h"

#include "hostinput.pb.h"
#include "transportinstruction.pb.h"
#include "userinput.pb.h"

namespace mosh_protobuf {

namespace {

const int MAX_GROUP_DEPTH = 64;

}  // namespace

void put_varint( std::string &out, std::uint64_t value )
{
  while ( value >= 0x80 ) {
    out.push_back( static_cast<char>( ( value & 0x7F ) | 0x80 ) );
    value >>= 7;
  }
  out.push_back( static_cast<char>( value ) );
}

void put_tag( std::string &out, std::uint32_t field, WireType type )
{
  put_varint( out, ( std::uint64_t( field ) << 3 ) | std::uint64_t( type ) );
}

void put_uint( std::string &out, std::uint32_t field, std::uint64_t value )
{
  put_tag( out, field, VARINT );
  put_varint( out, value );
}

void put_int32( std::string &out, std::uint32_t field, std::int32_t value )
{
  put_tag( out, field, VARINT );
  put_varint( out, static_cast<std::uint64_t>( static_cast<std::int64_t>( value ) ) );
}

void put_bytes( std::string &out, std::uint32_t field, const std::string &value )
{
  put_tag( out, field, LENGTH_DELIMITED );
  put_varint( out, value.size() );
  out += value;
}

bool Reader::read_varint( std::uint64_t &value )
{
  value = 0;
  for ( int shift = 0; shift < 70; shift += 7 ) {
    if ( p_ == end_ ) {
      return fail();
    }
    const auto b = static_cast<unsigned char>( *p_++ );
    if ( shift < 64 ) {
      value |= std::uint64_t( b & 0x7F ) << shift;
    }
    if ( !( b & 0x80 ) ) {
      return true;
    }
  }
  return fail(); /* more than ten bytes */
}

bool Reader::next( std::uint32_t &field, WireType &type )
{
  if ( !ok_ || p_ == end_ ) {
    return false;
  }
  std::uint64_t tag;
  if ( !read_varint( tag ) ) {
    return false;
  }
  if ( tag > 0xFFFFFFFFu || ( tag >> 3 ) == 0 || ( tag & 7 ) > FIXED32 ) {
    return fail();
  }
  field = static_cast<std::uint32_t>( tag >> 3 );
  type = static_cast<WireType>( tag & 7 );
  return true;
}

bool Reader::read_bytes( std::string &value )
{
  std::uint64_t len;
  if ( !read_varint( len ) ) {
    return false;
  }
  if ( len > static_cast<std::uint64_t>( end_ - p_ ) ) {
    return fail();
  }
  value.assign( p_, static_cast<std::size_t>( len ) );
  p_ += len;
  return true;
}

bool Reader::skip( std::uint32_t field, WireType type, int depth )
{
  std::uint64_t ignored;
  std::string ignored_bytes;
  switch ( type ) {
  case VARINT:
    return read_varint( ignored );
  case FIXED64:
    if ( end_ - p_ < 8 ) {
      return fail();
    }
    p_ += 8;
    return true;
  case FIXED32:
    if ( end_ - p_ < 4 ) {
      return fail();
    }
    p_ += 4;
    return true;
  case LENGTH_DELIMITED:
    return read_bytes( ignored_bytes );
  case START_GROUP:
    if ( depth >= MAX_GROUP_DEPTH ) {
      return fail();
    }
    for ( ;; ) {
      std::uint32_t inner;
      WireType inner_type;
      if ( !next( inner, inner_type ) ) {
        return fail(); /* the group never ends */
      }
      if ( inner_type == END_GROUP ) {
        return inner == field ? true : fail();
      }
      if ( !skip( inner, inner_type, depth + 1 ) ) {
        return false;
      }
    }
  case END_GROUP:
    return fail(); /* without a group to end */
  }
  return fail();
}

}  // namespace mosh_protobuf

using namespace mosh_protobuf;

namespace {

/* Reads a varint field, or skips the field if it was sent with another wire type. */
bool read_uint( Reader &r, WireType type, std::uint32_t field, std::uint64_t &value, bool &has )
{
  if ( type != VARINT ) {
    return r.skip( field, type );
  }
  if ( !r.read_varint( value ) ) {
    return false;
  }
  has = true;
  return true;
}

bool read_bytes( Reader &r, WireType type, std::uint32_t field, std::string &value, bool &has )
{
  if ( type != LENGTH_DELIMITED ) {
    return r.skip( field, type );
  }
  if ( !r.read_bytes( value ) ) {
    return false;
  }
  has = true;
  return true;
}

template <class Message>
bool read_message( Reader &r, WireType type, std::uint32_t field, Message &message, bool &has )
{
  if ( type != LENGTH_DELIMITED ) {
    return r.skip( field, type );
  }
  std::string bytes;
  if ( !r.read_bytes( bytes ) || !message.merge_from( bytes ) ) {
    return false;
  }
  has = true;
  return true;
}

}  // namespace

/* TransportBuffers */

std::string TransportBuffers::Instruction::SerializeAsString( void ) const
{
  std::string out;
  if ( has_protocol_version() ) { put_uint( out, 1, protocol_version_ ); }
  if ( has_old_num() ) { put_uint( out, 2, old_num_ ); }
  if ( has_new_num() ) { put_uint( out, 3, new_num_ ); }
  if ( has_ack_num() ) { put_uint( out, 4, ack_num_ ); }
  if ( has_throwaway_num() ) { put_uint( out, 5, throwaway_num_ ); }
  if ( has_diff() ) { put_bytes( out, 6, diff_ ); }
  if ( has_chaff() ) { put_bytes( out, 7, chaff_ ); }
  return out;
}

bool TransportBuffers::Instruction::ParseFromString( const std::string &data )
{
  Clear();
  Reader r( data );
  std::uint32_t field;
  WireType type;
  while ( r.next( field, type ) ) {
    bool has = false;
    std::uint64_t v = 0;
    bool ok;
    switch ( field ) {
    case 1:
      ok = read_uint( r, type, field, v, has );
      if ( has ) { set_protocol_version( static_cast<std::uint32_t>( v ) ); }
      break;
    case 2:
      ok = read_uint( r, type, field, v, has );
      if ( has ) { set_old_num( v ); }
      break;
    case 3:
      ok = read_uint( r, type, field, v, has );
      if ( has ) { set_new_num( v ); }
      break;
    case 4:
      ok = read_uint( r, type, field, v, has );
      if ( has ) { set_ack_num( v ); }
      break;
    case 5:
      ok = read_uint( r, type, field, v, has );
      if ( has ) { set_throwaway_num( v ); }
      break;
    case 6:
      ok = read_bytes( r, type, field, diff_, has );
      if ( has ) { has_ |= DIFF; }
      break;
    case 7:
      ok = read_bytes( r, type, field, chaff_, has );
      if ( has ) { has_ |= CHAFF; }
      break;
    default:
      ok = r.skip( field, type );
      break;
    }
    if ( !ok ) {
      Clear();
      return false;
    }
  }
  if ( !r.ok() ) {
    Clear();
    return false;
  }
  return true;
}

/* HostBuffers */

void HostBuffers::HostBytes::append_to( std::string &out ) const
{
  if ( has_hoststring_ ) { put_bytes( out, 4, hoststring_ ); }
}

bool HostBuffers::HostBytes::merge_from( const std::string &data )
{
  Reader r( data );
  std::uint32_t field;
  WireType type;
  while ( r.next( field, type ) ) {
    bool ok = field == 4 ? read_bytes( r, type, field, hoststring_, has_hoststring_ ) : r.skip( field, type );
    if ( !ok ) {
      return false;
    }
  }
  return r.ok();
}

void HostBuffers::ResizeMessage::append_to( std::string &out ) const
{
  if ( has_width_ ) { put_int32( out, 5, width_ ); }
  if ( has_height_ ) { put_int32( out, 6, height_ ); }
}

bool HostBuffers::ResizeMessage::merge_from( const std::string &data )
{
  Reader r( data );
  std::uint32_t field;
  WireType type;
  while ( r.next( field, type ) ) {
    std::uint64_t v = 0;
    bool has = false;
    bool ok = ( field == 5 || field == 6 ) ? read_uint( r, type, field, v, has ) : r.skip( field, type );
    if ( has ) {
      ( field == 5 ? width_ : height_ ) = static_cast<std::int32_t>( static_cast<std::uint32_t>( v ) );
      ( field == 5 ? has_width_ : has_height_ ) = true;
    }
    if ( !ok ) {
      return false;
    }
  }
  return r.ok();
}

void HostBuffers::EchoAck::append_to( std::string &out ) const
{
  if ( has_echo_ack_num_ ) { put_uint( out, 8, echo_ack_num_ ); }
}

bool HostBuffers::EchoAck::merge_from( const std::string &data )
{
  Reader r( data );
  std::uint32_t field;
  WireType type;
  while ( r.next( field, type ) ) {
    bool ok = field == 8 ? read_uint( r, type, field, echo_ack_num_, has_echo_ack_num_ ) : r.skip( field, type );
    if ( !ok ) {
      return false;
    }
  }
  return r.ok();
}

void HostBuffers::Instruction::append_to( std::string &out ) const
{
  std::string inner;
  if ( has_hostbytes_ ) {
    hostbytes_.append_to( inner );
    put_bytes( out, 2, inner );
    inner.clear();
  }
  if ( has_resize_ ) {
    resize_.append_to( inner );
    put_bytes( out, 3, inner );
    inner.clear();
  }
  if ( has_echoack_ ) {
    echoack_.append_to( inner );
    put_bytes( out, 7, inner );
  }
}

bool HostBuffers::Instruction::merge_from( const std::string &data )
{
  Reader r( data );
  std::uint32_t field;
  WireType type;
  while ( r.next( field, type ) ) {
    bool ok;
    switch ( field ) {
    case 2: ok = read_message( r, type, field, hostbytes_, has_hostbytes_ ); break;
    case 3: ok = read_message( r, type, field, resize_, has_resize_ ); break;
    case 7: ok = read_message( r, type, field, echoack_, has_echoack_ ); break;
    default: ok = r.skip( field, type ); break;
    }
    if ( !ok ) {
      return false;
    }
  }
  return r.ok();
}

std::string HostBuffers::HostMessage::SerializeAsString( void ) const
{
  std::string out, inner;
  for ( const Instruction &i : instruction_ ) {
    inner.clear();
    i.append_to( inner );
    put_bytes( out, 1, inner );
  }
  return out;
}

bool HostBuffers::HostMessage::ParseFromString( const std::string &data )
{
  Clear();
  Reader r( data );
  std::uint32_t field;
  WireType type;
  while ( r.next( field, type ) ) {
    bool ok;
    if ( field == 1 && type == LENGTH_DELIMITED ) {
      std::string bytes;
      ok = r.read_bytes( bytes ) && add_instruction()->merge_from( bytes );
    } else {
      ok = r.skip( field, type );
    }
    if ( !ok ) {
      Clear();
      return false;
    }
  }
  if ( !r.ok() ) {
    Clear();
    return false;
  }
  return true;
}

/* ClientBuffers */

void ClientBuffers::Keystroke::append_to( std::string &out ) const
{
  if ( has_keys_ ) { put_bytes( out, 4, keys_ ); }
}

bool ClientBuffers::Keystroke::merge_from( const std::string &data )
{
  Reader r( data );
  std::uint32_t field;
  WireType type;
  while ( r.next( field, type ) ) {
    bool ok = field == 4 ? read_bytes( r, type, field, keys_, has_keys_ ) : r.skip( field, type );
    if ( !ok ) {
      return false;
    }
  }
  return r.ok();
}

void ClientBuffers::ResizeMessage::append_to( std::string &out ) const
{
  if ( has_width_ ) { put_int32( out, 5, width_ ); }
  if ( has_height_ ) { put_int32( out, 6, height_ ); }
}

bool ClientBuffers::ResizeMessage::merge_from( const std::string &data )
{
  Reader r( data );
  std::uint32_t field;
  WireType type;
  while ( r.next( field, type ) ) {
    std::uint64_t v = 0;
    bool has = false;
    bool ok = ( field == 5 || field == 6 ) ? read_uint( r, type, field, v, has ) : r.skip( field, type );
    if ( has ) {
      ( field == 5 ? width_ : height_ ) = static_cast<std::int32_t>( static_cast<std::uint32_t>( v ) );
      ( field == 5 ? has_width_ : has_height_ ) = true;
    }
    if ( !ok ) {
      return false;
    }
  }
  return r.ok();
}

void ClientBuffers::Instruction::append_to( std::string &out ) const
{
  std::string inner;
  if ( has_keystroke_ ) {
    keystroke_.append_to( inner );
    put_bytes( out, 2, inner );
    inner.clear();
  }
  if ( has_resize_ ) {
    resize_.append_to( inner );
    put_bytes( out, 3, inner );
  }
}

bool ClientBuffers::Instruction::merge_from( const std::string &data )
{
  Reader r( data );
  std::uint32_t field;
  WireType type;
  while ( r.next( field, type ) ) {
    bool ok;
    switch ( field ) {
    case 2: ok = read_message( r, type, field, keystroke_, has_keystroke_ ); break;
    case 3: ok = read_message( r, type, field, resize_, has_resize_ ); break;
    default: ok = r.skip( field, type ); break;
    }
    if ( !ok ) {
      return false;
    }
  }
  return r.ok();
}

std::string ClientBuffers::UserMessage::SerializeAsString( void ) const
{
  std::string out, inner;
  for ( const Instruction &i : instruction_ ) {
    inner.clear();
    i.append_to( inner );
    put_bytes( out, 1, inner );
  }
  return out;
}

bool ClientBuffers::UserMessage::ParseFromString( const std::string &data )
{
  Clear();
  Reader r( data );
  std::uint32_t field;
  WireType type;
  while ( r.next( field, type ) ) {
    bool ok;
    if ( field == 1 && type == LENGTH_DELIMITED ) {
      std::string bytes;
      ok = r.read_bytes( bytes ) && add_instruction()->merge_from( bytes );
    } else {
      ok = r.skip( field, type );
    }
    if ( !ok ) {
      Clear();
      return false;
    }
  }
  if ( !r.ok() ) {
    Clear();
    return false;
  }
  return true;
}
