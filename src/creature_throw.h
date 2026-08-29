#pragma once
#ifndef CATA_SRC_CREATURE_THROW_H
#define CATA_SRC_CREATURE_THROW_H

#include <algorithm>
#include <cmath>

#include "calendar.h"
#include "enums.h"

namespace creature_throw
{

constexpr int min_stamina_cost = 100;
constexpr int max_stamina_cost = 1500;
constexpr int smaller_size_throw_min_str = 6;
constexpr int equal_size_throw_min_str = 12;
constexpr int larger_size_throw_min_str = 16;
constexpr int much_larger_size_throw_min_str = 20;
constexpr int obstacle_bash_reference_weight_grams = 60000;
constexpr int obstacle_bash_min_weight_percent = 10;
constexpr int obstacle_bash_max_weight_percent = 200;
constexpr time_duration thrown_creature_downed_duration = 3_turns;
constexpr time_duration shoved_creature_downed_duration = 3_turns;

inline int size_throw_strength_requirement( const creature_size thrower_size,
        const creature_size target_size )
{
    const int delta = static_cast<int>( target_size ) - static_cast<int>( thrower_size );
    if( delta <= -1 ) {
        return smaller_size_throw_min_str;
    }
    if( delta == 0 ) {
        return equal_size_throw_min_str;
    }
    if( delta == 1 ) {
        return larger_size_throw_min_str;
    }
    if( delta == 2 ) {
        return much_larger_size_throw_min_str;
    }
    return 999;
}

inline int required_throw_strength( const creature_size thrower_size,
                                    const creature_size target_size,
                                    const int target_weight_grams )
{
    const int size_req = size_throw_strength_requirement( thrower_size, target_size );
    const int weight_kg = std::max( 1, target_weight_grams / 1000 );
    const int weight_req = std::max( smaller_size_throw_min_str,
                                     static_cast<int>( std::ceil( weight_kg / 20.0f ) ) );
    return std::max( size_req, weight_req );
}

inline bool can_throw_grabbed_creature( const creature_size thrower_size,
                                        const int strength,
                                        const creature_size target_size,
                                        const int target_weight_grams )
{
    return strength >= required_throw_strength(
               thrower_size, target_size, target_weight_grams );
}

inline int flung_creature_bash_damage( const creature_size size, const int weight_grams,
                                       const float velocity )
{
    const float weight_ratio = static_cast<float>( std::max( 1, weight_grams ) ) /
                               obstacle_bash_reference_weight_grams;
    const int weight_percent = std::clamp(
                                   static_cast<int>( std::round( weight_ratio * 100.0f ) ),
                                   obstacle_bash_min_weight_percent,
                                   obstacle_bash_max_weight_percent );
    const float size_bonus = 1.0f + 0.15f * std::max(
                                 0, static_cast<int>( size ) -
                                 static_cast<int>( creature_size::medium ) );
    return std::max(
               1, static_cast<int>( std::round(
                                        velocity * weight_percent / 100.0f * size_bonus ) ) );
}

inline int grabbed_stamina_cost( const float throw_velocity,
                                 const int target_weight_grams,
                                 const int unarmed_skill,
                                 const int throwing_skill,
                                 const int dexterity )
{
    // Keep the CBN-style "distance matters" feel, while preserving Breeze's
    // weight and skill efficiency.  Grappling is a control option, not a stamina tax.
    const int base_cost = 100 + static_cast<int>( std::round( throw_velocity * 4.0f ) );
    const float weight_factor = std::clamp(
                                    static_cast<float>( std::max( 1, target_weight_grams ) ) / 70000.0f,
                                    0.65f, 2.00f );
    const float efficiency = std::clamp(
                                 1.0f - unarmed_skill * 0.05f -
                                 throwing_skill * 0.03f -
                                 std::max( 0, dexterity - 8 ) * 0.01f,
                                 0.40f, 1.15f );
    return std::clamp(
               static_cast<int>( std::round( base_cost * weight_factor * efficiency ) ),
               min_stamina_cost, max_stamina_cost );
}

inline float grabbed_throw_velocity( const int distance )
{
    return std::max( 1, distance ) * 10.0f;
}

} // namespace creature_throw

#endif // CATA_SRC_CREATURE_THROW_H
