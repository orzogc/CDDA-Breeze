#pragma once
#ifndef CATA_SRC_HSV_COLOR_H
#define CATA_SRC_HSV_COLOR_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "color.h"
#include "hash_utils.h"
#include "json.h"

#if defined(TILES)
#include "sdl_wrappers.h"
#endif

struct RGBColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;

    constexpr RGBColor() = default;
    constexpr RGBColor( uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255 ) : r( r ), g( g ), b( b ), a( a ) {}
#if defined(TILES)
    explicit constexpr RGBColor( const SDL_Color &c ) : r( c.r ), g( c.g ), b( c.b ), a( c.a ) {}
    explicit constexpr operator SDL_Color() const {
        return SDL_Color{ r, g, b, a };
    }
#endif

    void serialize( JsonOut &jsout ) const;
    void deserialize( const JsonValue &jv );

    std::string to_hex() const;
    static RGBColor from_hex( const std::string &str );
    static std::optional<RGBColor> try_parse( const std::string &str );
    static std::pair<RGBColor, std::string> random_named( std::string fuzzy_match = "" );
    static std::unordered_map<RGBColor, std::string> get_all_named_colors();
    static void load_named_color( const JsonObject &jo );
    static void unload_names();
    std::string friendly_name() const;

    bool operator==( const RGBColor &other ) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    bool operator!=( const RGBColor &other ) const {
        return !( *this == other );
    }
};

struct RGBColorPair {
    RGBColor bg;
    RGBColor fg;
};

struct HSVColor {
    uint32_t H;
    uint16_t S;
    uint8_t V;
    uint8_t A;
};

RGBColor curses_color_to_RGB( const nc_color &color );
RGBColor hsv2rgb( HSVColor color );
HSVColor rgb2hsv( RGBColor color );
RGBColor tint_blend( const RGBColor &base, const RGBColor &tint );

namespace std
{
template<> struct hash<RGBColor> {
    std::size_t operator()( const RGBColor &color ) const noexcept {
        std::size_t hash = 0;
        cata::hash_combine( hash, color.r );
        cata::hash_combine( hash, color.g );
        cata::hash_combine( hash, color.b );
        cata::hash_combine( hash, color.a );
        return hash;
    }
};

template<> struct hash<HSVColor> {
    std::size_t operator()( const HSVColor &color ) const noexcept {
        std::size_t hash = 0;
        cata::hash_combine( hash, color.H );
        cata::hash_combine( hash, color.S );
        cata::hash_combine( hash, color.V );
        cata::hash_combine( hash, color.A );
        return hash;
    }
};
} // namespace std

#endif // CATA_SRC_HSV_COLOR_H
