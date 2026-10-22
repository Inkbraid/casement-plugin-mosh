#include "unicode.h"

#include <cstdint>

namespace {

struct MoshWidthRange {
    std::uint32_t first;
    std::uint32_t last;
};

#include "wcwidth_table.h"

template <std::size_t N>
bool in_table( const MoshWidthRange ( &table )[ N ], std::uint32_t c )
{
    std::size_t lo = 0, hi = N;
    while ( lo < hi ) {
        std::size_t mid = lo + ( hi - lo ) / 2;
        if ( c < table[ mid ].first ) {
            hi = mid;
        } else if ( c > table[ mid ].last ) {
            lo = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

}  // namespace

std::size_t mosh_utf8_decode( char32_t *out, const char *s, std::size_t n )
{
    if ( n == 0 ) {
        return static_cast<std::size_t>( -2 );
    }
    const auto b0 = static_cast<unsigned char>( s[ 0 ] );
    if ( b0 < 0x80 ) {
        *out = b0;
        return b0 == 0 ? 0 : 1;
    }
    std::size_t len;
    std::uint32_t c;
    /* The allowed range of the second byte excludes overlong forms (E0, F0),
       surrogates (ED) and values above U+10FFFF (F4). */
    unsigned char lo = 0x80, hi = 0xBF;
    if ( b0 >= 0xC2 && b0 <= 0xDF ) {
        len = 2;
        c = b0 & 0x1F;
    } else if ( b0 >= 0xE0 && b0 <= 0xEF ) {
        len = 3;
        c = b0 & 0x0F;
        if ( b0 == 0xE0 ) {
            lo = 0xA0;
        } else if ( b0 == 0xED ) {
            hi = 0x9F;
        }
    } else if ( b0 >= 0xF0 && b0 <= 0xF4 ) {
        len = 4;
        c = b0 & 0x07;
        if ( b0 == 0xF0 ) {
            lo = 0x90;
        } else if ( b0 == 0xF4 ) {
            hi = 0x8F;
        }
    } else {
        return static_cast<std::size_t>( -1 );
    }
    for ( std::size_t i = 1; i < len; i++ ) {
        if ( i >= n ) {
            return static_cast<std::size_t>( -2 );
        }
        const auto b = static_cast<unsigned char>( s[ i ] );
        if ( b < lo || b > hi ) {
            return static_cast<std::size_t>( -1 );
        }
        lo = 0x80;
        hi = 0xBF;
        c = ( c << 6 ) | ( b & 0x3F );
    }
    *out = c;
    return len;
}

void mosh_utf8_append( std::string &dest, char32_t c )
{
    std::uint32_t u = c;
    if ( u > 0x10FFFF || ( u >= 0xD800 && u <= 0xDFFF ) ) {
        u = 0xFFFD;
    }
    if ( u < 0x80 ) {
        dest.push_back( static_cast<char>( u ) );
    } else if ( u < 0x800 ) {
        dest.push_back( static_cast<char>( 0xC0 | ( u >> 6 ) ) );
        dest.push_back( static_cast<char>( 0x80 | ( u & 0x3F ) ) );
    } else if ( u < 0x10000 ) {
        dest.push_back( static_cast<char>( 0xE0 | ( u >> 12 ) ) );
        dest.push_back( static_cast<char>( 0x80 | ( ( u >> 6 ) & 0x3F ) ) );
        dest.push_back( static_cast<char>( 0x80 | ( u & 0x3F ) ) );
    } else {
        dest.push_back( static_cast<char>( 0xF0 | ( u >> 18 ) ) );
        dest.push_back( static_cast<char>( 0x80 | ( ( u >> 12 ) & 0x3F ) ) );
        dest.push_back( static_cast<char>( 0x80 | ( ( u >> 6 ) & 0x3F ) ) );
        dest.push_back( static_cast<char>( 0x80 | ( u & 0x3F ) ) );
    }
}

std::u32string mosh_utf8_to_u32( const std::string &s )
{
    std::u32string out;
    std::size_t i = 0;
    while ( i < s.size() ) {
        char32_t c;
        std::size_t n = mosh_utf8_decode( &c, s.data() + i, s.size() - i );
        if ( n == static_cast<std::size_t>( -1 ) || n == static_cast<std::size_t>( -2 ) ) {
            out.push_back( 0xFFFD );
            i++;
        } else {
            out.push_back( c );
            i += n == 0 ? 1 : n;
        }
    }
    return out;
}

int mosh_wcwidth( char32_t c )
{
    const std::uint32_t u = c;
    if ( u > 0x10FFFF || in_table( mosh_nonprinting, u ) ) {
        return -1;
    }
    if ( in_table( mosh_zero_width, u ) ) {
        return 0;
    }
    if ( in_table( mosh_double_width, u ) ) {
        return 2;
    }
    return 1;
}
