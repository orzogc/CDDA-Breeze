#include "game.h" // IWYU pragma: associated

#include <algorithm>
#include <cstdlib>
#include <set>

#include "avatar.h"
#include "creature.h"
#include "creature_tracker.h"
#include "debug.h"
#include "map.h"
#include "messages.h"
#include "rng.h"
#include "sounds.h"
#include "tileray.h"
#include "translations.h"
#include "units_utility.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "vpart_range.h"

static const efftype_id effect_harnessed( "harnessed" );

bool game::grabbed_veh_move_helper( const tripoint &dp, bool via_ramp, bool stairs_move )
{
    if( stairs_move ) {
        return grabbed_veh_move_cross_z( dp, u.pos() );
    }
    if( via_ramp && dp.z != 0 ) {
        return grabbed_veh_move_cross_z( dp, u.pos() + dp );
    }
    return grabbed_veh_move( dp );
}

bool game::grabbed_veh_move_cross_z( const tripoint &dp, const tripoint &player_destination )
{
    const optional_vpart_position grabbed_vehicle_vp = m.veh_at( u.pos() + u.grab_point );
    if( !grabbed_vehicle_vp ) {
        add_msg( m_info, _( "跨层后找不到原先抓住的载具。" ) );
        u.grab( object_type::NONE );
        return false;
    }

    vehicle *grabbed_vehicle = &grabbed_vehicle_vp->vehicle();
    if( !grabbed_vehicle ||
        !grabbed_vehicle->handle_potential_theft( get_avatar() ) ) {
        u.grab( object_type::NONE );
        return false;
    }

    const int grabbed_part = grabbed_vehicle_vp->part_index();
    for( const vpart_reference &vpr : grabbed_vehicle->get_all_parts() ) {
        monster *mon = grabbed_vehicle->get_monster( vpr.part_index() );
        if( mon != nullptr && mon->has_effect( effect_harnessed ) ) {
            add_msg( m_info, _( "有动物仍连接在%s上，无法把它拖过跨层通道。" ),
                     grabbed_vehicle->disp_name() );
            u.grab( object_type::NONE );
            return false;
        }
    }

    grabbed_vehicle->invalidate_mass();
    if( dp.z > 0 ) {
        const int str_req = grabbed_vehicle->total_mass() / 10_kilogram;
        if( u.get_arm_str() < str_req ) {
            add_msg( m_bad, _( "你的力量不足以把%s拖到上一层。" ), grabbed_vehicle->disp_name() );
            u.grab( object_type::NONE );
            return true;
        }
    }

    std::set<tripoint> occupied_tiles;
    for( const vpart_reference &vpr : grabbed_vehicle->get_all_parts() ) {
        occupied_tiles.insert( m.getlocal( grabbed_vehicle->global_part_pos3( vpr.part_index() ) ) );
    }
    if( occupied_tiles.empty() ) {
        u.grab( object_type::NONE );
        return false;
    }

    const tripoint grabbed_part_pos = m.getlocal( grabbed_vehicle->global_part_pos3( grabbed_part ) );
    int min_x = occupied_tiles.begin()->x;
    int max_x = min_x;
    int min_y = occupied_tiles.begin()->y;
    int max_y = min_y;
    for( const tripoint &p : occupied_tiles ) {
        min_x = std::min( min_x, p.x );
        max_x = std::max( max_x, p.x );
        min_y = std::min( min_y, p.y );
        max_y = std::max( max_y, p.y );
    }

    const int width = max_x - min_x + 1;
    const int height = max_y - min_y + 1;
    const int longest_span = std::max( width, height );
    if( occupied_tiles.size() > 4 || longest_span > 4 || ( width > 1 && height > 1 ) ) {
        add_msg( m_info, _( "%s太宽或太长，无法安全通过跨层通道。" ), grabbed_vehicle->disp_name() );
        u.grab( object_type::NONE );
        return true;
    }

    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint &p : occupied_tiles ) {
        const tripoint target = p + dp;
        const bool occupied_by_creature = target == player_destination ||
                                          creatures.creature_at<Creature>( target ) != nullptr;
        if( !m.inbounds( target ) || !m.passable( target ) ||
            !m.has_floor_or_support( target ) || m.has_furn( target ) ||
            m.veh_at( target ) || occupied_by_creature ) {
            // [临时诊断] 定位跨层放置失败的具体检查项，问题定位后移除
            add_msg( m_info, _( "[vdrag诊断] target=%s dp=%s inbounds=%d passable=%d floor=%d furn=%d veh=%d creature=%d" ),
                     target.to_string(), dp.to_string(), m.inbounds( target ), m.passable( target ),
                     m.has_floor_or_support( target ), m.has_furn( target ),
                     m.veh_at( target ).has_value(), occupied_by_creature );
            add_msg( m_info, _( "跨层通道另一端没有足够空间放下%s。" ), grabbed_vehicle->disp_name() );
            u.grab( object_type::NONE );
            return true;
        }
    }

    m.displace_vehicle( *grabbed_vehicle, dp );
    m.rebuild_vehicle_level_caches();
    m.level_vehicle( *grabbed_vehicle );
    grabbed_vehicle->check_falling_or_floating();
    if( grabbed_vehicle->is_falling ) {
        add_msg( m_info, _( "%s开始坠落，你松开了它。" ), grabbed_vehicle->disp_name() );
        u.grab( object_type::NONE );
        m.drop_vehicle( dp );
        return false;
    }

    const tripoint new_grab_pos = grabbed_part_pos + dp;
    u.grab( object_type::VEHICLE, new_grab_pos - player_destination );
    add_msg( _( "你把%s拖过了跨层通道。" ), grabbed_vehicle->disp_name() );
    return false;
}

bool game::grabbed_veh_move( const tripoint &dp )
{
    const optional_vpart_position grabbed_vehicle_vp = m.veh_at( u.pos() + u.grab_point );
    if( !grabbed_vehicle_vp ) {
        add_msg( m_info, _( "No vehicle at grabbed point." ) );
        u.grab( object_type::NONE );
        return false;
    }
    vehicle *grabbed_vehicle = &grabbed_vehicle_vp->vehicle();
    if( !grabbed_vehicle ||
        !grabbed_vehicle->handle_potential_theft( get_avatar() ) ) {
        return false;
    }
    const int grabbed_part = grabbed_vehicle_vp->part_index();
    for( const vpart_reference &vpr : grabbed_vehicle->get_all_parts() ) {
        monster *mon = grabbed_vehicle->get_monster( vpr.part_index() );
        if( mon != nullptr && mon->has_effect( effect_harnessed ) ) {
            add_msg( m_info, _( "You cannot move this vehicle whilst your %s is harnessed!" ),
                     mon->get_name() );
            u.grab( object_type::NONE );
            return false;
        }
    }
    const vehicle *veh_under_player = veh_pointer_or_null( m.veh_at( u.pos() ) );
    if( grabbed_vehicle == veh_under_player ) {
        u.grab_point = -dp;
        return false;
    }

    tripoint dp_veh = -u.grab_point;
    const tripoint prev_grab = u.grab_point;
    tripoint next_grab = u.grab_point;

    bool zigzag = false;

    if( dp == prev_grab ) {
        // We are pushing in the direction of vehicle
        dp_veh = dp;
    } else if( std::abs( dp.x + dp_veh.x ) != 2 && std::abs( dp.y + dp_veh.y ) != 2 ) {
        // Not actually moving the vehicle, don't do the checks
        u.grab_point = -( dp + dp_veh );
        return false;
    } else if( ( dp.x == prev_grab.x || dp.y == prev_grab.y ) &&
               next_grab.x != 0 && next_grab.y != 0 ) {
        // Zig-zag (or semi-zig-zag) pull: player is diagonal to vehicle
        // and moves away from it, but not directly away
        dp_veh.x = dp.x == -dp_veh.x ? 0 : dp_veh.x;
        dp_veh.y = dp.y == -dp_veh.y ? 0 : dp_veh.y;

        next_grab = -dp_veh;
        zigzag = true;
    } else {
        // We are pulling the vehicle
        next_grab = -dp;
    }

    // Make sure the mass and pivot point are correct
    grabbed_vehicle->invalidate_mass();

    //vehicle movement: strength check
    int mc = 0;
    int str_req = grabbed_vehicle->total_mass() / 25_kilogram; //strength required to move vehicle.
    // ARM_STR governs dragging heavy things
    int str = u.get_arm_str();

    //if vehicle is rollable we modify str_req based on a function of movecost per wheel.

    // Vehicle just too big to grab & move; 41-45 lets folks have a bit of a window
    // (Roughly 1.1K kg = danger zone; cube vans are about the max)
    if( str_req > 45 ) {
        add_msg( m_info, _( "The %s is too bulky for you to move by hand." ),
                 grabbed_vehicle->name );
        return true; // No shoving around an RV.
    }

    const auto &wheel_indices = grabbed_vehicle->wheelcache;
    if( grabbed_vehicle->valid_wheel_config() ) {
        //determine movecost for terrain touching wheels
        const tripoint vehpos = grabbed_vehicle->global_pos3();
        for( int p : wheel_indices ) {
            const tripoint wheel_pos = vehpos + grabbed_vehicle->part( p ).precalc[0];
            const int mapcost = m.move_cost( wheel_pos, grabbed_vehicle );
            mc += str_req / wheel_indices.size() * mapcost;
        }
        //set strength check threshold
        //if vehicle has many or only one wheel (shopping cart), it is as if it had four.
        if( wheel_indices.size() > 4 || wheel_indices.size() == 1 ) {
            str_req = mc / 4 + 1;
        } else {
            str_req = mc / wheel_indices.size() + 1;
        }
    } else {
        str_req++;
        //if vehicle has no wheels str_req make a noise.
        if( str_req <= str ) {
            sounds::sound( grabbed_vehicle->global_pos3(), str_req * 2, sounds::sound_t::movement,
                           _( "a scraping noise." ), true, "misc", "scraping" );
        }
    }

    //final strength check and outcomes
    ///\ARM_STR determines ability to drag vehicles
    if( str_req <= str ) {
        //calculate exertion factor and movement penalty
        ///\EFFECT_STR increases speed of dragging vehicles
        u.moves -= 100 * str_req / std::max( 1, str );
        const int ex = dice( 1, 3 ) - 1 + str_req;
        if( ex > str + 1 ) {
            // Pain and movement penalty if exertion exceeds character strength
            add_msg( m_bad, _( "You strain yourself to move the %s!" ), grabbed_vehicle->name );
            u.moves -= 200;
            u.mod_pain( 1 );
        } else if( ex >= str ) {
            // Movement is slow if exertion nearly equals character strength
            add_msg( _( "It takes some time to move the %s." ), grabbed_vehicle->name );
            u.moves -= 200;
        }
    } else {
        u.moves -= 100;
        add_msg( m_bad, _( "You lack the strength to move the %s." ), grabbed_vehicle->name );
        return true;
    }

    std::string blocker_name = _( "errors in movement code" );
    const auto get_move_dir = [&]( const tripoint & dir, const tripoint & from ) {
        tileray mdir;

        mdir.init( dir.xy() );
        units::angle turn = normalize( mdir.dir() - grabbed_vehicle->face.dir() );
        if( grabbed_vehicle->is_on_ramp && turn == 180_degrees ) {
            add_msg( m_bad, _( "The %s can't be turned around while on a ramp." ), grabbed_vehicle->name );
            return tripoint_zero;
        }
        grabbed_vehicle->turn( turn );
        grabbed_vehicle->face = tileray( grabbed_vehicle->turn_dir );
        grabbed_vehicle->precalc_mounts( 1, mdir.dir(), grabbed_vehicle->pivot_point() );
        grabbed_vehicle->pos -= grabbed_vehicle->pivot_displacement();

        // Grabbed part has to stay at distance 1 to the player
        // and in roughly the same direction.
        const tripoint new_part_pos = grabbed_vehicle->global_pos3() +
                                      grabbed_vehicle->part( grabbed_part ).precalc[ 1 ];
        const tripoint expected_pos = u.pos() + dp + from;
        const tripoint actual_dir = tripoint( ( expected_pos - new_part_pos ).xy(), 0 );

        // Set player location to illegal value so it can't collide with vehicle.
        const tripoint player_prev = u.pos();
        u.setpos( tripoint_zero );
        std::vector<veh_collision> colls;
        const bool failed = grabbed_vehicle->collision( colls, actual_dir, true );
        u.setpos( player_prev );
        if( !colls.empty() ) {
            blocker_name = colls.front().target_name;
        }
        return failed ? tripoint_zero : actual_dir;
    };

    // First try the move as intended
    // But if that fails and the move is a zig-zag, try to recover:
    // Try to place the vehicle in the position player just left rather than "flattening" the zig-zag
    tripoint final_dp_veh = get_move_dir( dp_veh, next_grab );
    if( final_dp_veh == tripoint_zero && zigzag ) {
        final_dp_veh = get_move_dir( -prev_grab, -dp );
        next_grab = -dp;
    }

    if( final_dp_veh == tripoint_zero ) {
        add_msg( _( "The %s collides with %s." ), grabbed_vehicle->name, blocker_name );
        u.grab_point = prev_grab;
        return true;
    }

    u.grab_point = next_grab;

    m.displace_vehicle( *grabbed_vehicle, final_dp_veh );
    m.rebuild_vehicle_level_caches();

    if( grabbed_vehicle ) {
        m.level_vehicle( *grabbed_vehicle );
        grabbed_vehicle->check_falling_or_floating();
        if( grabbed_vehicle->is_falling ) {
            add_msg( _( "You let go of the %1$s as it starts to fall." ), grabbed_vehicle->disp_name() );
            u.grab( object_type::NONE );
            m.drop_vehicle( final_dp_veh );
            return true;
        }
    } else {
        debugmsg( "Grabbed vehicle disappeared" );
        return false;
    }

    for( int p : wheel_indices ) {
        if( one_in( 2 ) ) {
            tripoint wheel_p = grabbed_vehicle->global_part_pos3( grabbed_part );
            grabbed_vehicle->handle_trap( wheel_p, p );
        }
    }

    return false;

}
