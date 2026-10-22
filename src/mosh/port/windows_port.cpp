#include "windows_port.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdint>

bool mosh_random_bytes( void *dest, std::size_t size )
{
  auto *p = static_cast<unsigned char *>( dest );
  while ( size > 0 ) {
    const ULONG chunk = static_cast<ULONG>( std::min<std::size_t>( size, 1u << 20 ) );
    if ( !BCRYPT_SUCCESS( BCryptGenRandom( nullptr, p, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG ) ) ) {
      return false;
    }
    p += chunk;
    size -= chunk;
  }
  return true;
}

namespace {

/* The message in UTF-8, or empty if the system has none in that language. */
std::string message( DWORD code, DWORD language )
{
  wchar_t *buffer = nullptr;
  DWORD n = FormatMessageW( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            nullptr, code, language, reinterpret_cast<wchar_t *>( &buffer ), 0, nullptr );
  std::string text;
  if ( n != 0 && buffer != nullptr ) {
    int len = WideCharToMultiByte( CP_UTF8, 0, buffer, static_cast<int>( n ), nullptr, 0, nullptr, nullptr );
    if ( len > 0 ) {
      text.resize( static_cast<std::size_t>( len ) );
      WideCharToMultiByte( CP_UTF8, 0, buffer, static_cast<int>( n ), &text[ 0 ], len, nullptr, nullptr );
    }
  }
  if ( buffer != nullptr ) {
    LocalFree( buffer );
  }
  return text;
}

}  // namespace

std::string mosh_error_string( int code )
{
  /* English if the system has it, as mosh's own messages are English. */
  std::string text = message( static_cast<DWORD>( code ), MAKELANGID( LANG_ENGLISH, SUBLANG_DEFAULT ) );
  if ( text.empty() ) {
    text = message( static_cast<DWORD>( code ), 0 );
  }
  while ( !text.empty() && ( text.back() == '\n' || text.back() == '\r' || text.back() == '.' || text.back() == ' ' ) ) {
    text.pop_back();
  }
  if ( text.empty() ) {
    text = "error " + std::to_string( code );
  }
  return text;
}
