// Windows services for mosh's ported sources. Declared here, without
// <windows.h>, so that the Windows headers' macros stay out of mosh's code.

#pragma once

#include <cstddef>
#include <string>

/* Fills dest with bytes from the system's cryptographic random number
   generator (BCryptGenRandom). False if that fails. */
bool mosh_random_bytes( void *dest, std::size_t size );

/* The system's message for a Windows or Winsock error code, without the
   trailing period and line break. */
std::string mosh_error_string( int code );
