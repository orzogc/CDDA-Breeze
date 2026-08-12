#include "hsv_color.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <sstream>
#include <vector>

#include "debug.h"
#include "rng.h"
#include "string_formatter.h"
#include "translations.h"

#if defined(TILES)
#include "sdl_utils.h"
#endif

static std::unordered_map<RGBColor, std::string> named_colors;
static std::unordered_map<RGBColor, std::string> similar_name_cache;

void RGBColor::load_named_color( const JsonObject &jo )
{
    const std::string name = jo.get_string( "name" );
    if( jo.has_member( "value" ) ) {
        RGBColor color;
        color.deserialize( jo.get_member( "value" ) );
        named_colors.insert_or_assign( color, name );
    }
}

void RGBColor::unload_names()
{
    named_colors.clear();
    similar_name_cache.clear();
}

static bool char_cmp_ignore_case( const char a, const char b )
{
    return std::tolower( static_cast<unsigned char>( a ) ) ==
           std::tolower( static_cast<unsigned char>( b ) );
}

std::pair<RGBColor, std::string> RGBColor::random_named( std::string fuzzy_match )
{
    if( named_colors.empty() ) {
        return { RGBColor{}, std::string() };
    }
    if( fuzzy_match.empty() ) {
        return random_entry( named_colors );
    }

    std::vector<decltype( named_colors )::value_type> candidates;
    std::copy_if( named_colors.begin(), named_colors.end(), std::back_inserter( candidates ),
    [&]( const std::pair<const RGBColor, std::string> &c ) {
        return std::search( c.second.begin(), c.second.end(), fuzzy_match.begin(), fuzzy_match.end(),
                            char_cmp_ignore_case ) != c.second.end();
    } );
    if( candidates.empty() ) {
        return random_entry( named_colors );
    }
    return random_entry( candidates );
}

std::unordered_map<RGBColor, std::string> RGBColor::get_all_named_colors()
{
    return named_colors;
}

std::string RGBColor::friendly_name() const
{
    const auto exact = named_colors.find( *this );
    if( exact != named_colors.end() ) {
        return exact->second;
    }
    const auto cached = similar_name_cache.find( *this );
    if( cached != similar_name_cache.end() ) {
        return cached->second;
    }
    if( named_colors.empty() ) {
        return _( "Unknown color" );
    }

    const auto dist = []( const RGBColor &a, const RGBColor &b ) {
        const int dr = static_cast<int>( a.r ) - static_cast<int>( b.r );
        const int dg = static_cast<int>( a.g ) - static_cast<int>( b.g );
        const int db = static_cast<int>( a.b ) - static_cast<int>( b.b );
        return dr * dr + dg * dg + db * db;
    };
    const auto nearest = std::min_element( named_colors.begin(), named_colors.end(),
    [&]( const auto &lhs, const auto &rhs ) {
        return dist( lhs.first, *this ) < dist( rhs.first, *this );
    } );
    const std::string name = string_format( _( "%s (Off-Brand)" ), nearest->second );
    similar_name_cache.emplace( *this, name );
    return name;
}

RGBColor curses_color_to_RGB( const nc_color &color )
{
#if defined(TILES)
    return RGBColor( curses_color_to_SDL( color ) );
#else
    ( void )color;
    return RGBColor{ 255, 255, 255, 255 };
#endif
}

static uint8_t median( const uint8_t a, const uint8_t b, const uint8_t c )
{
    if( ( a > b ) ^ ( a > c ) ) {
        return a;
    }
    if( ( b < a ) ^ ( b < c ) ) {
        return b;
    }
    return c;
}

RGBColor hsv2rgb( HSVColor color )
{
    constexpr int E = ( 1 << 16 ) - 1;
    const uint32_t H = color.H;
    const uint16_t S = color.S;
    const uint8_t V = color.V;
    const uint8_t A = color.A;

    if( S == 0 || V == 0 ) {
        return RGBColor{ V, V, V, A };
    }

    uint8_t I;
    if( H < E ) {
        I = 0;
    } else if( H < 2 * E ) {
        I = 1;
    } else if( H < 3 * E ) {
        I = 2;
    } else if( H < 4 * E ) {
        I = 3;
    } else if( H < 5 * E ) {
        I = 4;
    } else {
        I = 5;
    }

    uint32_t F = H - static_cast<uint32_t>( E * I );
    if( F == 0 ) {
        ++F;
    }
    if( I % 2 != 0 ) {
        F = E - F;
    }

    const int d = ( ( S * V ) >> 16 ) + 1;
    const uint8_t m = static_cast<uint8_t>( V - d );
    const uint8_t c = static_cast<uint8_t>( ( ( F * static_cast<uint32_t>( d ) ) >> 16 ) + m );

    switch( I ) {
        case 0: return { V, c, m, A };
        case 1: return { c, V, m, A };
        case 2: return { m, V, c, A };
        case 3: return { m, c, V, A };
        case 4: return { c, m, V, A };
        case 5: return { V, m, c, A };
        default: return { 0, 0, 0, A };
    }
}

HSVColor rgb2hsv( RGBColor color )
{
    const uint8_t R = color.r;
    const uint8_t G = color.g;
    const uint8_t B = color.b;
    const uint8_t A = color.a;
    const uint8_t minv = std::min( { R, G, B } );
    const uint8_t maxv = std::max( { R, G, B } );
    const uint8_t med = median( R, G, B );
    const uint8_t V = maxv;
    const int d = maxv - minv;
    if( d == 0 || maxv == 0 ) {
        return HSVColor{ 0, 0, V, A };
    }

    const uint16_t S = static_cast<uint16_t>( ( ( d << 16 ) - 1 ) / V );
    int I;
    if( maxv == R && minv == B ) {
        I = 0;
    } else if( maxv == G && minv == B ) {
        I = 1;
    } else if( maxv == G && minv == R ) {
        I = 2;
    } else if( maxv == B && minv == R ) {
        I = 3;
    } else if( maxv == B && minv == G ) {
        I = 4;
    } else {
        I = 5;
    }

    constexpr int E = ( 1 << 16 ) - 1;
    int F = ( ( ( med - minv ) << 16 ) / d ) + 1;
    if( I % 2 != 0 ) {
        F = E - F;
    }
    return HSVColor{ static_cast<uint32_t>( E * I + F ), S, V, A };
}

RGBColor tint_blend( const RGBColor &base, const RGBColor &tint )
{
    HSVColor base_hsv = rgb2hsv( base );
    const HSVColor dest_hsv = rgb2hsv( tint );

    const auto lerp16 = []( const uint16_t a, const uint16_t b, const uint8_t t ) {
        return static_cast<uint16_t>( a + ( ( static_cast<int>( b ) - static_cast<int>( a ) ) * t ) / 255 );
    };
    const auto lerp8 = []( const uint8_t a, const uint8_t b, const uint8_t t ) {
        return static_cast<uint8_t>( a + ( ( static_cast<int>( b ) - static_cast<int>( a ) ) * t ) / 255 );
    };
    const auto overlay = []( const uint8_t b, const uint8_t blend ) {
        if( b > 127 ) {
            return static_cast<uint8_t>( std::clamp<int>( 255 - std::max( 255 - blend, 1 ) *
                                         ( ( 255 - b ) * 255 / 127 ) / 255, 0, 255 ) );
        }
        return static_cast<uint8_t>( std::clamp<int>( blend * ( b * 255 / 127 ) / 255, 0, 255 ) );
    };

    base_hsv.H = dest_hsv.H;
    base_hsv.S = lerp16( std::min( base_hsv.S, dest_hsv.S ), dest_hsv.S, 127 );
    base_hsv.V = lerp8( base_hsv.V, overlay( base_hsv.V, dest_hsv.V ), 127 );
    RGBColor out = hsv2rgb( base_hsv );
    out.a = base.a;
    return out;
}

void RGBColor::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    jsout.write( r );
    jsout.write( g );
    jsout.write( b );
    if( a != 255 ) {
        jsout.write( a );
    }
    jsout.end_array();
}

void RGBColor::deserialize( const JsonValue &jv )
{
    if( jv.test_string() ) {
        const std::optional<RGBColor> col = try_parse( jv.get_string() );
        if( col ) {
            *this = *col;
        } else {
            jv.throw_error( "Unknown color value" );
        }
        return;
    }
    if( !jv.test_array() ) {
        jv.throw_error( "Invalid color value, expected string or array" );
    }
    JsonArray arr = jv.get_array();
    if( arr.size() != 3 && arr.size() != 4 ) {
        jv.throw_error( "Invalid color value, expected 3 or 4 element array" );
    }
    r = static_cast<uint8_t>( std::clamp( arr.get_int( 0 ), 0, 255 ) );
    g = static_cast<uint8_t>( std::clamp( arr.get_int( 1 ), 0, 255 ) );
    b = static_cast<uint8_t>( std::clamp( arr.get_int( 2 ), 0, 255 ) );
    a = arr.size() == 4 ? static_cast<uint8_t>( std::clamp( arr.get_int( 3 ), 0, 255 ) ) : 255;
}

static std::optional<RGBColor> rgb_from_hex_string( std::string str )
{
    if( !str.empty() && str.front() == '#' ) {
        str.erase( str.begin() );
    }
    if( str.empty() || std::any_of( str.begin(), str.end(), []( const char c ) {
        return !std::isxdigit( static_cast<unsigned char>( c ) );
    } ) ) {
        return std::nullopt;
    }
    uint32_t d = 0;
    std::istringstream is( str );
    is >> std::hex >> d;
    switch( str.size() ) {
        case 3: {
            const uint8_t r = static_cast<uint8_t>( ( d >> 8 ) & 0x0f );
            const uint8_t g = static_cast<uint8_t>( ( d >> 4 ) & 0x0f );
            const uint8_t b = static_cast<uint8_t>( d & 0x0f );
            return RGBColor{ static_cast<uint8_t>( r | r << 4 ), static_cast<uint8_t>( g | g << 4 ),
                             static_cast<uint8_t>( b | b << 4 ), 255 };
        }
        case 4: {
            const uint8_t r = static_cast<uint8_t>( ( d >> 12 ) & 0x0f );
            const uint8_t g = static_cast<uint8_t>( ( d >> 8 ) & 0x0f );
            const uint8_t b = static_cast<uint8_t>( ( d >> 4 ) & 0x0f );
            const uint8_t a = static_cast<uint8_t>( d & 0x0f );
            return RGBColor{ static_cast<uint8_t>( r | r << 4 ), static_cast<uint8_t>( g | g << 4 ),
                             static_cast<uint8_t>( b | b << 4 ), static_cast<uint8_t>( a | a << 4 ) };
        }
        case 6:
            return RGBColor{ static_cast<uint8_t>( d >> 16 ), static_cast<uint8_t>( d >> 8 ),
                             static_cast<uint8_t>( d ), 255 };
        case 8:
            return RGBColor{ static_cast<uint8_t>( d >> 24 ), static_cast<uint8_t>( d >> 16 ),
                             static_cast<uint8_t>( d >> 8 ), static_cast<uint8_t>( d ) };
        default:
            return std::nullopt;
    }
}

std::string RGBColor::to_hex() const
{
    return a == 255 ? string_format( "#%02x%02x%02x", r, g, b ) :
           string_format( "#%02x%02x%02x%02x", r, g, b, a );
}

RGBColor RGBColor::from_hex( const std::string &str )
{
    return try_parse( str ).value_or( RGBColor{} );
}

std::optional<RGBColor> RGBColor::try_parse( const std::string &str )
{
    if( !str.empty() && str.front() == '#' ) {
        return rgb_from_hex_string( str );
    }
    if( !str.empty() && str.front() == '!' ) {
        if( named_colors.empty() ) {
            return std::nullopt;
        }
        return random_named( str.substr( 1 ) ).first;
    }

    const color_manager &cm = get_all_colors();
    const color_id nc_id = cm.name_to_id( str, report_color_error::no );
    if( nc_id != def_c_unset ) {
        return curses_color_to_RGB( cm.get( nc_id ) );
    }
    for( const auto &entry : named_colors ) {
        if( str.size() == entry.second.size() &&
            std::equal( str.begin(), str.end(), entry.second.begin(), char_cmp_ignore_case ) ) {
            return entry.first;
        }
    }
    return std::nullopt;
}
