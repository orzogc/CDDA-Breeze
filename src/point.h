#pragma once
#ifndef CATA_SRC_POINT_H
#define CATA_SRC_POINT_H

// The CATA_NO_STL macro is used by the cata clang-tidy plugin tests so they
// can include this header when compiling with -nostdinc++
#ifndef CATA_NO_STL

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <ostream>
#include <vector>

#include "cata_assert.h"
#else

#define cata_assert(...)

namespace std
{
class string;
class ostream;
}

#endif // CATA_NO_STL

class JsonArray;
class JsonOut;

// NOLINTNEXTLINE(cata-xy)
struct point {
    static constexpr int dimension = 2;

    int x = 0;
    int y = 0;
    constexpr point() = default;
    constexpr point( int X, int Y ) : x( X ), y( Y ) {}

    static point from_string( const std::string & );

    constexpr point operator+( const point &rhs ) const {
        return point( x + rhs.x, y + rhs.y );
    }
    point &operator+=( const point &rhs ) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    constexpr point operator-() const {
        return point( -x, -y );
    }
    constexpr point operator-( const point &rhs ) const {
        return point( x - rhs.x, y - rhs.y );
    }
    point &operator-=( const point &rhs ) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }
    constexpr point operator*( const int rhs ) const {
        return point( x * rhs, y * rhs );
    }
    friend constexpr point operator*( int lhs, const point &rhs ) {
        return rhs * lhs;
    }
    point &operator*=( const int rhs ) {
        x *= rhs;
        y *= rhs;
        return *this;
    }
    constexpr point operator/( const int rhs ) const {
        return point( x / rhs, y / rhs );
    }

#ifndef CATA_NO_STL
    point abs() const {
        return point( std::abs( x ), std::abs( y ) );
    }
#endif

    /**
     * Rotate point clockwise @param turns times, 90 degrees per turn,
     * around the center of a rectangle with the dimensions specified
     * by @param dim
     * By default rotates around the origin (0, 0).
     * NOLINTNEXTLINE(cata-use-named-point-constants) */
    point rotate( int turns, const point &dim = { 1, 1 } ) const;

    float distance( const point &rhs ) const;
    int distance_manhattan( const point &rhs ) const;

    std::string to_string() const;
    std::string to_string_writable() const;

    void serialize( JsonOut &jsout ) const;
    void deserialize( const JsonArray &jsin );

    friend inline constexpr bool operator<( const point &a, const point &b ) {
        return a.x < b.x || ( a.x == b.x && a.y < b.y );
    }
    friend inline constexpr bool operator==( const point &a, const point &b ) {
        return a.x == b.x && a.y == b.y;
    }
    friend inline constexpr bool operator!=( const point &a, const point &b ) {
        return !( a == b );
    }

#ifndef CATA_NO_STL
    friend std::ostream &operator<<( std::ostream &, const point & );
    friend std::istream &operator>>( std::istream &, point & );
#endif
};

inline int divide_round_to_minus_infinity( int n, int d )
{
    // The NOLINT comments here are to suppress a clang-tidy warning that seems
    // to be a clang-tidy bug.  I'd like to get rid of them if the bug is ever
    // fixed.  The warning comes via a project_remain call in
    // mission_companion.cpp.
    if( n >= 0 ) {
        return n / d; // NOLINT(clang-analyzer-core.DivideZero)
    }
    return ( n - d + 1 ) / d; // NOLINT(clang-analyzer-core.DivideZero)
}

inline point multiply_xy( const point &p, int f )
{
    return point( p.x * f, p.y * f );
}

inline point divide_xy_round_to_minus_infinity( const point &p, int d )
{
    return point( divide_round_to_minus_infinity( p.x, d ),
                  divide_round_to_minus_infinity( p.y, d ) );
}

// NOLINTNEXTLINE(cata-xy)
struct tripoint {
    static constexpr int dimension = 3;

    int x = 0;
    int y = 0;
    int z = 0;
    constexpr tripoint() = default;
    constexpr tripoint( int X, int Y, int Z ) : x( X ), y( Y ), z( Z ) {}
    constexpr tripoint( const point &p, int Z ) : x( p.x ), y( p.y ), z( Z ) {}

    static tripoint from_string( const std::string & );

    constexpr tripoint operator+( const tripoint &rhs ) const {
        return tripoint( x + rhs.x, y + rhs.y, z + rhs.z );
    }
    constexpr tripoint operator-( const tripoint &rhs ) const {
        return tripoint( x - rhs.x, y - rhs.y, z - rhs.z );
    }
    tripoint &operator+=( const tripoint &rhs ) {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }
    constexpr tripoint operator-() const {
        return tripoint( -x, -y, -z );
    }
    constexpr tripoint operator*( const int rhs ) const {
        return tripoint( x * rhs, y * rhs, z * rhs );
    }
    friend constexpr tripoint operator*( int lhs, const tripoint &rhs ) {
        return rhs * lhs;
    }
    tripoint &operator*=( const int rhs ) {
        x *= rhs;
        y *= rhs;
        z *= rhs;
        return *this;
    }
    constexpr tripoint operator/( const int rhs ) const {
        return tripoint( x / rhs, y / rhs, z / rhs );
    }
    /*** some point operators and functions ***/
    constexpr tripoint operator+( const point &rhs ) const {
        return tripoint( x + rhs.x, y + rhs.y, z );
    }
    friend constexpr tripoint operator+( const point &lhs, const tripoint &rhs ) {
        return rhs + lhs;
    }
    constexpr tripoint operator-( const point &rhs ) const {
        return tripoint( x - rhs.x, y - rhs.y, z );
    }
    tripoint &operator+=( const point &rhs ) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    tripoint &operator-=( const point &rhs ) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }
    tripoint &operator-=( const tripoint &rhs ) {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }

#ifndef CATA_NO_STL
    tripoint abs() const {
        return tripoint( std::abs( x ), std::abs( y ), std::abs( z ) );
    }
#endif

    constexpr point xy() const {
        return point( x, y );
    }

    /**
     * Rotates just the x,y component of the tripoint. See point::rotate()
     * NOLINTNEXTLINE(cata-use-named-point-constants) */
    tripoint rotate( int turns, const point &dim = { 1, 1 } ) const {
        return tripoint( xy().rotate( turns, dim ), z );
    }

    std::string to_string() const;
    std::string to_string_writable() const;

    void serialize( JsonOut &jsout ) const;
    void deserialize( const JsonArray &jsin );

#ifndef CATA_NO_STL
    friend std::ostream &operator<<( std::ostream &, const tripoint & );
    friend std::istream &operator>>( std::istream &, tripoint & );
#endif

    friend inline constexpr bool operator==( const tripoint &a, const tripoint &b ) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
    friend inline constexpr bool operator!=( const tripoint &a, const tripoint &b ) {
        return !( a == b );
    }
#ifndef CATA_NO_STL
    friend inline bool operator<( const tripoint &a, const tripoint &b ) {
        return std::tie( a.x, a.y, a.z ) < std::tie( b.x, b.y, b.z );
    }
#endif
};

inline tripoint multiply_xy( const tripoint &p, int f )
{
    return tripoint( p.x * f, p.y * f, p.z );
}

inline tripoint divide_xy_round_to_minus_infinity( const tripoint &p, int d )
{
    return tripoint( divide_round_to_minus_infinity( p.x, d ),
                     divide_round_to_minus_infinity( p.y, d ),
                     p.z );
}

static constexpr tripoint tripoint_zero{};
static constexpr point point_zero{};

static constexpr point point_north{ 0, -1 };
static constexpr point point_north_east{ 1, -1 };
static constexpr point point_east{ 1, 0 };
static constexpr point point_south_east{ 1, 1 };
static constexpr point point_south{ 0, 1 };
static constexpr point point_south_west{ -1, 1 };
static constexpr point point_west{ -1, 0 };
static constexpr point point_north_west{ -1, -1 };

static constexpr tripoint tripoint_north{ point_north, 0 };
static constexpr tripoint tripoint_north_east{ point_north_east, 0 };
static constexpr tripoint tripoint_east{ point_east, 0 };
static constexpr tripoint tripoint_south_east{ point_south_east, 0 };
static constexpr tripoint tripoint_south{ point_south, 0 };
static constexpr tripoint tripoint_south_west{ point_south_west, 0 };
static constexpr tripoint tripoint_west{ point_west, 0 };
static constexpr tripoint tripoint_north_west{ point_north_west, 0 };

static constexpr tripoint tripoint_above{ 0, 0, 1 };
static constexpr tripoint tripoint_below{ 0, 0, -1 };

struct sphere {
    int radius = 0;
    tripoint center = tripoint_zero;

    sphere() = default;
    explicit sphere( const tripoint &center ) : radius( 1 ), center( center ) {}
    explicit sphere( const tripoint &center, int radius ) : radius( radius ), center( center ) {}
};

#ifndef CATA_NO_STL

/**
 * Following functions return points in a spiral pattern starting at center_x/center_y until it hits the radius. Clockwise fashion.
 * Credit to Tom J Nowell; http://stackoverflow.com/a/1555236/1269969
 */
std::vector<tripoint> closest_points_first( const tripoint &center, int max_dist );
std::vector<tripoint> closest_points_first( const tripoint &center, int min_dist, int max_dist );

std::vector<point> closest_points_first( const point &center, int max_dist );
std::vector<point> closest_points_first( const point &center, int min_dist, int max_dist );

static constexpr tripoint tripoint_min { INT_MIN, INT_MIN, INT_MIN };
static constexpr tripoint tripoint_max{ INT_MAX, INT_MAX, INT_MAX };

static constexpr point point_min{ tripoint_min.xy() };
static constexpr point point_max{ tripoint_max.xy() };

// Make point hashable so it can be used as an unordered_set or unordered_map key,
// or a component of one.
namespace std
{
template <>
struct hash<point> {
    std::size_t operator()( const point &k ) const noexcept {
        // We cast k.y to uint32_t because otherwise when promoting to uint64_t for binary `or` it
        // will sign extend and turn the upper 32 bits into all 1s.
        uint64_t x = static_cast<uint64_t>(k.x) << 32 | static_cast<uint32_t>(k.y);

        // Found through https://nullprogram.com/blog/2018/07/31/
        // Public domain source from https://xoshiro.di.unimi.it/splitmix64.c
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9U;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebU;
        x ^= x >> 31;
        return x;
    }
};
} // namespace std

// Make tripoint hashable so it can be used as an unordered_set or unordered_map key,
// or a component of one.
namespace std
{
template <>
struct hash<tripoint> {
    std::size_t operator()( const tripoint &k ) const noexcept {
        // We cast k.y to uint32_t because otherwise when promoting to uint64_t for binary `or` it
        // will sign extend and turn the upper 32 bits into all 1s.
        uint64_t x = static_cast<uint64_t>(k.x) << 32 | static_cast<uint32_t>(k.y);

        // Found through https://nullprogram.com/blog/2018/07/31/
        // Public domain source from https://xoshiro.di.unimi.it/splitmix64.c
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9U;
        x ^= x >> 27;

        // Sprinkle in z now.
        x ^= static_cast<uint64_t>(k.z);
        x *= 0x94d049bb133111ebU;
        x ^= x >> 31;
        return x;
    }
};
} // namespace std

static constexpr std::array<point, 4> four_adjacent_offsets{{
        point_north, point_east, point_south, point_west
    }};

static constexpr std::array<point, 4> neighborhood{ {
        point_south, point_east, point_west, point_north
    }};

static constexpr std::array<point, 4> offsets = {{
        point_south, point_east, point_west, point_north
    }
};

static constexpr std::array<point, 4> four_cardinal_directions{{
        point_west, point_east, point_north, point_south
    }};

static constexpr std::array<point, 5> five_cardinal_directions{{
        point_west, point_east, point_north, point_south, point_zero
    }};

static const std::array<tripoint, 8> eight_horizontal_neighbors = { {
        { tripoint_north_west },
        { tripoint_north },
        { tripoint_north_east },
        { tripoint_west },
        { tripoint_east },
        { tripoint_south_west },
        { tripoint_south },
        { tripoint_south_east },
    }
};

#endif // CATA_NO_STL

#endif // CATA_SRC_POINT_H
