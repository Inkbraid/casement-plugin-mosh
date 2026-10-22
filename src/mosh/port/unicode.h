// UTF-8 and character widths for mosh's terminal code on Windows.
//
// mosh decodes UTF-8 with mbrtowc(), encodes it with wcrtomb() and measures
// characters with wcwidth(), all on wchar_t. On Windows wchar_t has 16 bits,
// too few for a code point, the C library's multibyte functions depend on
// the process's locale, and there is no wcwidth() at all. The ported code
// uses char32_t and these functions instead.

#pragma once

#include <cstddef>
#include <string>

/* Decodes the character at the start of s[0..n), like mbrtowc() in glibc's
   UTF-8 locales but without state between calls. Returns the number of
   bytes it used (0 for U+0000), (size_t)-1 for an invalid sequence
   (overlong forms, surrogates and values above U+10FFFF included), or
   (size_t)-2 when s[0..n) is only the start of a character. */
std::size_t mosh_utf8_decode( char32_t *out, const char *s, std::size_t n );

/* Appends the UTF-8 form of c; U+FFFD for values that aren't characters. */
void mosh_utf8_append( std::string &dest, char32_t c );

/* UTF-8 to code points, each invalid byte becoming U+FFFD. */
std::u32string mosh_utf8_to_u32( const std::string &s );

/* The number of columns c takes, as glibc's wcwidth() in C.UTF-8 says:
   -1 for control and unassigned code points, 0 for combining marks and
   other zero-width characters, 2 for wide (East Asian, most emoji) ones.
   mosh-server measures with glibc, and the client has to agree with it. */
int mosh_wcwidth( char32_t c );
