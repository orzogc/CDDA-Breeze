#include "map.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <queue>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "active_item_cache.h"
#include "ammo.h"
#include "ammo_effect.h"
#include "avatar.h"
#include "basecamp.h"
#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "cata_assert.h"
#include "cata_type_traits.h"
#include "character.h"
#include "character_id.h"
#include "clzones.h"
#include "colony.h"
#include "color.h"
#include "construction.h"
#include "coordinate_conversions.h"
#include "creature.h"
#include "creature_tracker.h"
#include "cuboid_rectangle.h"
#include "cursesdef.h"
#include "damage.h"
#include "debug.h"
#include "do_turn.h"
#include "drawing_primitives.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "explosion.h"
#include "field.h"
#include "field_type.h"
#include "flag.h"
#include "fragment_cloud.h"
#include "fungal_effects.h"
#include "game.h"
#include "harvest.h"
#include "iexamine.h"
#include "item.h"
#include "item_factory.h"
#include "item_group.h"
#include "item_location.h"
#include "item_pocket.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "lightmap.h"
#include "line.h"
#include "magic_ter_furn_transform.h"
#include "map_iterator.h"
#include "map_memory.h"
#include "map_selector.h"
#include "mapbuffer.h"
#include "mapdata.h"
#include "mapgen.h"
#include "math_defines.h"
#include "memory_fast.h"
#include "messages.h"
#include "mongroup.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include <optional>
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "pathfinding.h"
#include "projectile.h"
#include "relic.h"
#include "ret_val.h"
#include "rng.h"
#include "safe_reference.h"
#include "scent_map.h"
#include "shadowcasting.h"
#include "sounds.h"
#include "string_formatter.h"
#include "submap.h"
#include "tileray.h"
#include "timed_event.h"
#include "translations.h"
#include "trap.h"
#include "ui_manager.h"
#include "units.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_selector.h"
#include "viewer.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "weather.h"
#include "weighted_list.h"

#include "material.h"
#include "ranged.h"

static const ammotype ammo_battery( "battery" );

static const diseasetype_id disease_bad_food( "bad_food" );

static const efftype_id effect_boomered( "boomered" );
static const efftype_id effect_crushed( "crushed" );
static const efftype_id effect_pet( "pet" );

static const field_type_str_id field_fd_clairvoyant( "fd_clairvoyant" );

static const flag_id json_flag_AVATAR_ONLY( "AVATAR_ONLY" );
static const flag_id json_flag_PRESERVE_SPAWN_OMT( "PRESERVE_SPAWN_OMT" );
static const flag_id json_flag_UNDODGEABLE( "UNDODGEABLE" );

static const item_group_id Item_spawn_data_default_zombie_clothes( "default_zombie_clothes" );
static const item_group_id Item_spawn_data_default_zombie_items( "default_zombie_items" );

static const itype_id itype_battery( "battery" );
static const itype_id itype_nail( "nail" );

static const material_id material_glass( "glass" );

static const mtype_id mon_zombie( "mon_zombie" );

static const oter_str_id oter_deep_rock( "deep_rock" );
static const oter_str_id oter_empty_rock( "empty_rock" );
static const oter_str_id oter_open_air( "open_air" );
static const oter_str_id oter_solid_earth( "solid_earth" );

static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_soil( "t_soil" );
static const ter_str_id ter_t_tree_birch_harvested( "t_tree_birch_harvested" );
static const ter_str_id ter_t_tree_dead( "t_tree_dead" );
static const ter_str_id ter_t_tree_deadpine( "t_tree_deadpine" );
static const ter_str_id ter_t_tree_hickory_dead( "t_tree_hickory_dead" );
static const ter_str_id ter_t_tree_willow_harvested( "t_tree_willow_harvested" );
static const ter_str_id ter_t_open_air("t_open_air");

static const trait_id trait_SCHIZOPHRENIC( "SCHIZOPHRENIC" );

static const trap_str_id tr_unfinished_construction( "tr_unfinished_construction" );

bool has_processed_conveyor_belt = false;
std::set<item*> processed_conveyor_belt_item_set;
std::set<Creature*>processed_conveyor_belt_creature_set;
std::set<tripoint> processed_conveyor_belt_tripoint_set;

#define dbg(x) DebugLog((x),D_MAP) << __FILE__ << ":" << __LINE__ << ": "

static cata::colony<item> nulitems;          // Returned when &i_at() is asked for an OOB value
static field              nulfield;          // Returned when &field_at() is asked for an OOB value
static level_cache        nullcache;         // Dummy cache for z-levels outside bounds

// Map stack methods.
map_stack::iterator map_stack::erase( map_stack::const_iterator it )
{
    return myorigin->i_rem( location, it );
}

void map_stack::insert( const item &newitem )
{
    myorigin->add_item_or_charges( location, newitem );
}

units::volume map_stack::max_volume() const
{
    if( !myorigin->inbounds( location ) ) {
        return 0_ml;
    } else if( myorigin->has_furn( location ) ) {
        return myorigin->furn( location ).obj().max_volume;
    }
    return myorigin->ter( location ).obj().max_volume;
}

// Map class methods.

map::map( int mapsize, bool zlev ) : my_MAPSIZE( mapsize ), my_HALF_MAPSIZE( mapsize / 2 ),
    zlevels( zlev )
{

    if( zlevels ) {
        grid.resize( static_cast<size_t>( my_MAPSIZE ) * my_MAPSIZE * OVERMAP_LAYERS, nullptr );
    } else {
        grid.resize( static_cast<size_t>( my_MAPSIZE ) * my_MAPSIZE, nullptr );
    }

    for( auto &ptr : pathfinding_caches ) {
        ptr = std::make_unique<pathfinding_cache>();
    }

    dbg( D_INFO ) << "map::map(): my_MAPSIZE: " << my_MAPSIZE << " z-levels enabled:" << zlevels;
    traplocs.resize( trap::count() );
}

map::~map()
{
    if( ( _main_requires_cleanup && !_main_cleanup_override ) ||
        ( _main_cleanup_override && *_main_cleanup_override ) ) {
        get_map().reset_vehicles_sm_pos();
        get_map().rebuild_vehicle_level_caches();
        g->load_npcs();
    }
}
// NOLINTNEXTLINE(performance-noexcept-move-constructor)
map &map::operator=( map && ) = default;

static submap null_submap;

void map::set_transparency_cache_dirty( const int zlev )
{
    if( inbounds_z( zlev ) ) {
        get_cache( zlev ).transparency_cache_dirty.set();
        get_cache( zlev ).r_hor_cache->invalidate();
        get_cache( zlev ).r_up_cache->invalidate();
    }
}

void map::set_transparency_cache_dirty( const tripoint &p, bool field )
{
    if( inbounds( p ) ) {
        const tripoint smp = ms_to_sm_copy( p );
        get_cache( smp.z ).transparency_cache_dirty.set( smp.x * MAPSIZE + smp.y );
        if( !field ) {
            get_cache( smp.z ).r_hor_cache->invalidate( p.xy() );
            get_cache( smp.z ).r_up_cache->invalidate( p.xy() );
            set_visitable_zones_cache_dirty();
        }
    }
}

void map::set_seen_cache_dirty( const tripoint &change_location )
{
    if( inbounds( change_location ) ) {
        level_cache &cache = get_cache( change_location.z );
        if( cache.seen_cache_dirty ) {
            return;
        }
        if( cache.seen_cache[change_location.x][change_location.y] != 0.0 ||
            cache.camera_cache[change_location.x][change_location.y] != 0.0 ) {
            cache.seen_cache_dirty = true;
        }
    }
}

void map::set_seen_cache_dirty( const int zlevel )
{
    if( inbounds_z( zlevel ) ) {
        level_cache &cache = get_cache( zlevel );
        cache.seen_cache_dirty = true;
    }
}

void map::set_outside_cache_dirty( const int zlev )
{
    if( inbounds_z( zlev ) ) {
        get_cache( zlev ).outside_cache_dirty = true;
    }
}

void map::set_floor_cache_dirty( const int zlev )
{
    if( inbounds_z( zlev ) ) {
        get_cache( zlev ).floor_cache_dirty = true;
    }
}

void map::set_memory_seen_cache_dirty( const tripoint &p )
{
    const int offset = p.x + p.y * MAPSIZE_Y;
    if( offset >= 0 && offset < MAPSIZE_X * MAPSIZE_Y ) {
        get_cache( p.z ).map_memory_seen_cache.reset( offset );
    }
}

void map::invalidate_map_cache( const int zlev )
{
    if( inbounds_z( zlev ) ) {
        level_cache &ch = get_cache( zlev );
        ch.floor_cache_dirty = true;
        ch.seen_cache_dirty = true;
        ch.outside_cache_dirty = true;
        set_transparency_cache_dirty( zlev );
    }
}

const_maptile map::maptile_at( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return const_maptile( &null_submap, point_zero );
    }

    return maptile_at_internal( p );
}

const_maptile map::maptile_at( const tripoint_bub_ms &p ) const
{
    return maptile_at( p.raw() );
}

maptile map::maptile_at( const tripoint &p )
{
    if( !inbounds( p ) ) {
        return maptile( &null_submap, point_zero );
    }

    return maptile_at_internal( p );
}

maptile map::maptile_at( const tripoint_bub_ms &p )
{
    return maptile_at( p.raw() );
}

 bool map::has_obstruction(const tripoint& from, const tripoint& to, bool check_ally)
{
    std::vector<tripoint> line = line_to(from, to);
    // @to is what we want to hit. we don't need to check for obstruction there.
    line.pop_back();
    const map& here = get_map();
    creature_tracker& creatures = get_creature_tracker();
    for (const tripoint& line_point : line) {
        if (here.impassable(line_point) || (check_ally && creatures.creature_at(line_point))) {
            return true;
        }
    }
    return false;
}

bool map::clear_shoot_reach(const tripoint& from, const tripoint& to, bool check_ally)
{
    std::vector<tripoint> path = line_to(from, to);
    path.pop_back();
    creature_tracker& creatures = get_creature_tracker();
    map& m = get_map();
    for (const tripoint& p : path) {
        Creature* inter = creatures.creature_at(p);
        if (check_ally && inter != nullptr) {
            return false;
        }
        if (get_map().impassable(p)) {
            if (m.is_transparent(p) && ( (square_dist(from.xy(), p.xy()) == 1 && m.coverage(p)<100) || m.coverage(p) <=50) ) {
                return true;
            }
            else {
                return false;
            }
        }
    }

    return true;
}

const_maptile map::maptile_at_internal( const tripoint &p ) const
{
    point l;
    const submap *const sm = get_submap_at( p, l );

    return const_maptile( sm, l );
}

maptile map::maptile_at_internal( const tripoint &p )
{
    point l;
    submap *const sm = get_submap_at( p, l );

    return maptile( sm, l );
}

// Vehicle functions

VehicleList map::get_vehicles()
{
    if( !zlevels ) {
        return get_vehicles( tripoint( 0, 0, abs_sub.z() ),
                             tripoint( SEEX * my_MAPSIZE, SEEY * my_MAPSIZE, abs_sub.z() ) );
    }

    return get_vehicles( tripoint( 0, 0, -OVERMAP_DEPTH ),
                         tripoint( SEEX * my_MAPSIZE, SEEY * my_MAPSIZE, OVERMAP_HEIGHT ) );
}

void map::rebuild_vehicle_level_caches()
{
    clear_vehicle_level_caches();

    for( int gridz = -OVERMAP_DEPTH; gridz <= OVERMAP_HEIGHT; gridz++ ) {
        // Cache all vehicles
        level_cache *ch = get_cache_lazy( gridz );

        if( ch ) {
            for( vehicle * const &elem : ch->vehicle_list ) {
                add_vehicle_to_cache( elem );
            }
        }
    }
}

void map::add_vehicle_to_cache( vehicle *veh )
{
    if( veh == nullptr ) {
        debugmsg( "Tried to add null vehicle to cache" );
        return;
    }

    // Get parts
    for( const vpart_reference &vpr : veh->get_all_parts_with_fakes() ) {
        if( vpr.part().removed ) {
            continue;
        }
        const tripoint p = veh->global_part_pos3( vpr.part() );
        level_cache &ch = get_cache( p.z );
        ch.set_veh_cached_parts( p, *veh, static_cast<int>( vpr.part_index() ) );
        if( inbounds( p ) ) {
            ch.set_veh_exists_at( p, true );
        }
    }
}

void map::clear_vehicle_point_from_cache( vehicle *veh, const tripoint &pt )
{
    if( veh == nullptr ) {
        debugmsg( "Tried to add null vehicle to cache" );
        return;
    }

    level_cache *ch = get_cache_lazy( pt.z );
    if( ch ) {
        if( inbounds( pt ) ) {
            ch->set_veh_exists_at( pt, false );
        }
        ch->clear_veh_from_veh_cached_parts( pt, veh );
    }
}

void map::clear_vehicle_level_caches( )
{
    for( int gridz = -OVERMAP_DEPTH; gridz <= OVERMAP_HEIGHT; gridz++ ) {
        level_cache *ch = get_cache_lazy( gridz );
        if( ch ) {
            ch->clear_vehicle_cache();
        }
    }
}

void map::remove_vehicle_from_cache( vehicle *veh, int zmin, int zmax )
{
    for( int gridz = zmin; gridz <= zmax; gridz++ ) {
        level_cache *const ch = get_cache_lazy( gridz );
        if( ch != nullptr ) {
            ch->vehicle_list.erase( veh );
            ch->zone_vehicles.erase( veh );
        }
        dirty_vehicle_list.erase( veh );
    }
}

namespace
{
void _add_vehicle_to_list( level_cache &ch, vehicle *veh )
{
    ch.vehicle_list.insert( veh );
    if( !veh->loot_zones.empty() ) {
        ch.zone_vehicles.insert( veh );
    }
}
} // namespace

void map::reset_vehicles_sm_pos()
{
    int const zmin = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    int const zmax = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    for( int z = zmin; z <= zmax; z++ ) {
        level_cache &ch = get_cache( z );
        for( int x = 0; x < getmapsize(); x++ ) {
            for( int y = 0; y < getmapsize(); y++ ) {
                tripoint const grid( x, y, z );
                submap *const sm = get_submap_at_grid( grid );
                if( sm != nullptr ) {
                    for( auto const &elem : sm->vehicles ) {
                        elem->sm_pos = grid;
                        add_vehicle_to_cache( &*elem );
                        _add_vehicle_to_list( ch, &*elem );
                    }
                }
            }
        }
    }
}

void map::clear_vehicle_list( const int zlev )
{
    auto *ch = get_cache_lazy( zlev );
    if( ch ) {
        ch->vehicle_list.clear();
        ch->zone_vehicles.clear();
    }
}

void map::update_vehicle_list( const submap *const to, const int zlev )
{
    // Update vehicle data
    level_cache &ch = get_cache( zlev );
    for( const auto &elem : to->vehicles ) {
        _add_vehicle_to_list( ch, elem.get() );
    }
}

std::unique_ptr<vehicle> map::detach_vehicle( vehicle *veh )
{
    if( veh == nullptr ) {
        debugmsg( "map::detach_vehicle was passed nullptr" );
        return std::unique_ptr<vehicle>();
    }

    int z = veh->sm_pos.z;
    if( z < -OVERMAP_DEPTH || z > OVERMAP_HEIGHT ) {
        debugmsg( "detach_vehicle got a vehicle outside allowed z-level range!  name=%s, submap:%d,%d,%d",
                  veh->name, veh->sm_pos.x, veh->sm_pos.y, veh->sm_pos.z );
        // Try to fix by moving the vehicle here
        z = veh->sm_pos.z = abs_sub.z();
    }

    // Unboard all passengers before detaching
    for( const vpart_reference &part : veh->get_avail_parts( VPFLAG_BOARDABLE ) ) {
        Character *passenger = part.get_passenger();
        if( passenger ) {
            unboard_vehicle( part, passenger );
        }
    }
    veh->invalidate_towing( true );
    submap *const current_submap = get_submap_at_grid( veh->sm_pos );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to detach vehicle at (%d,%d,%d) but the submap is not loaded", veh->sm_pos.x,
                  veh->sm_pos.y, veh->sm_pos.z );
        return std::unique_ptr<vehicle>();
    }

    level_cache &ch = get_cache( z );
    for( size_t i = 0; i < current_submap->vehicles.size(); i++ ) {
        if( current_submap->vehicles[i].get() == veh ) {
            for( const tripoint &pt : veh->get_points() ) {
                // FIXME: allow memorizing all objects in a tile and only clear
                // vehicle memory here.
                get_avatar().clear_memorized_tile( getabs( pt ) );
                set_memory_seen_cache_dirty( pt );
            }
            ch.vehicle_list.erase( veh );
            ch.zone_vehicles.erase( veh );
            std::unique_ptr<vehicle> result = std::move( current_submap->vehicles[i] );
            current_submap->vehicles.erase( current_submap->vehicles.begin() + i );
            if( veh->tracking_on ) {
                overmap_buffer.remove_vehicle( veh );
            }
            dirty_vehicle_list.erase( veh );
            rebuild_vehicle_level_caches();
            return result;
        }
    }
    debugmsg( "detach_vehicle can't find it!  name=%s, submap:%d,%d,%d", veh->name, veh->sm_pos.x,
              veh->sm_pos.y, veh->sm_pos.z );
    return std::unique_ptr<vehicle>();
}

void map::destroy_vehicle( vehicle *veh )
{
    detach_vehicle( veh );
}

void map::on_vehicle_moved( const int smz )
{
    set_outside_cache_dirty( smz );
    set_transparency_cache_dirty( smz );
    set_floor_cache_dirty( smz );
    set_floor_cache_dirty( smz + 1 );
    set_pathfinding_cache_dirty( smz );
}

void map::vehmove()
{
    // give vehicles movement points
    VehicleList vehicle_list;
    int minz = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    int maxz = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    const tripoint player_pos = get_player_character().pos();
    for( int zlev = minz; zlev <= maxz; ++zlev ) {
        level_cache *cache_lazy = get_cache_lazy( zlev );
        if( !cache_lazy ) {
            continue;
        }
        level_cache &cache = *cache_lazy;
        for( vehicle *veh : cache.vehicle_list ) {
            if( veh->is_following ) {
                veh->drive_to_local_target( getabs( player_pos ), true );
            } else if( veh->is_patrolling ) {
                veh->autopilot_patrol();
            }

            if (!veh->is_appliance()) {
                veh->gain_moves();              
            }

            veh->slow_leak();
            wrapped_vehicle w;
            w.v = veh;
            vehicle_list.push_back( w );
        }
    }

    // 15 equals 3 >50mph vehicles, or up to 15 slow (1 square move) ones
    // But 15 is too low for V12 death-bikes, let's put 100 here
    for( int count = 0; count < 100; count++ ) {
        if( !vehproceed( vehicle_list ) ) {
            break;
        }
    }
    // Process item removal on the vehicles that were modified this turn.
    // Use a copy because part_removal_cleanup can modify the container.
    auto temp = dirty_vehicle_list;
    for( vehicle * const &elem : temp ) {
        auto same_ptr = [ elem ]( const struct wrapped_vehicle & tgt ) {
            return elem == tgt.v;
        };
        if( std::find_if( vehicle_list.begin(), vehicle_list.end(), same_ptr ) !=
            vehicle_list.end() ) {
            elem->part_removal_cleanup();
        }
    }
    dirty_vehicle_list.clear();
    std::set<vehicle *> origins;
    for( int zlev = minz; zlev <= maxz; ++zlev ) {
        const level_cache *cache = get_cache_lazy( zlev );
        if( cache ) {
            for( vehicle *veh : cache->vehicle_list ) {
                origins.emplace( veh );
            }
        }
    }
    for( const std::pair<vehicle *const, bool> &veh_pair : vehicle::enumerate_vehicles( origins ) ) {
        veh_pair.first->idle( veh_pair.second );
    }

    // refresh vehicle zones for moved vehicles
    zone_manager::get_manager().cache_vzones( this );
}

bool map::vehproceed( VehicleList &vehicle_list )
{
    wrapped_vehicle *cur_veh = nullptr;
    float max_of_turn = 0.0f;
    // First horizontal movement
    for( wrapped_vehicle &vehs_v : vehicle_list ) {
        if( vehs_v.v->of_turn > max_of_turn ) {
            cur_veh = &vehs_v;
            max_of_turn = cur_veh->v->of_turn;
        }
    }

    // Then vertical-only movement
    if( cur_veh == nullptr ) {
        for( wrapped_vehicle &vehs_v : vehicle_list ) {
            if( vehs_v.v->is_falling || ( vehs_v.v->is_rotorcraft() && vehs_v.v->get_z_change() != 0 ) ) {
                cur_veh = &vehs_v;
                break;
            }
        }
    }

    if( cur_veh == nullptr ) {
        return false;
    }

    cur_veh->v = cur_veh->v->act_on_map();
    if( cur_veh->v == nullptr ) {
        vehicle_list = get_vehicles();
    }

    return true;
}

static bool sees_veh( const Creature &c, vehicle &veh, bool force_recalc )
{
    const auto &veh_points = veh.get_points( force_recalc );
    return std::any_of( veh_points.begin(), veh_points.end(), [&c]( const tripoint & pt ) {
        return c.sees( pt );
    } );
}

vehicle *map::move_vehicle( vehicle &veh, const tripoint &dp, const tileray &facing )
{
    if( dp == tripoint_zero ) {
        debugmsg( "Empty displacement vector" );
        return &veh;
    } else if( std::abs( dp.x ) > 1 || std::abs( dp.y ) > 1 || std::abs( dp.z ) > 1 ) {
        debugmsg( "Invalid displacement vector: %d, %d, %d", dp.x, dp.y, dp.z );
        return &veh;
    }
    // Split the movement into horizontal and vertical for easier processing
    if( dp.xy() != point_zero && dp.z != 0 ) {
        vehicle *const new_pointer = move_vehicle( veh, tripoint( dp.xy(), 0 ), facing );
        if( !new_pointer ) {
            return nullptr;
        }

        vehicle *const result = move_vehicle( *new_pointer, tripoint( 0, 0, dp.z ), facing );
        if( !result ) {
            return nullptr;
        }

        result->is_falling = false;
        return result;
    }
    const bool vertical = dp.z != 0;
    // Ensured by the splitting above
    cata_assert( vertical == ( dp.xy() == point_zero ) );

    const int target_z = dp.z + veh.sm_pos.z;
    // limit vehicles to at most OVERMAP_HEIGHT - 1; this mitigates getting to zlevel 10 easily
    // and causing `get_cache_ref( zlev + 1 );` call in map::build_sunlight_cache overflowing
    if( target_z < -OVERMAP_DEPTH || target_z > OVERMAP_HEIGHT - 1 ) {
        return &veh;
    }

    veh.precalc_mounts( 1, veh.skidding ? veh.turn_dir : facing.dir(), veh.pivot_point() );

    // cancel out any movement of the vehicle due only to a change in pivot
    tripoint dp1 = dp - veh.pivot_displacement();

    int impulse = 0;

    std::vector<veh_collision> collisions;

    // Find collisions
    // Velocity of car before collision
    // Split into vertical and horizontal movement
    const int &coll_velocity = vertical ? veh.vertical_velocity : veh.velocity;
    const int velocity_before = coll_velocity;
    if( velocity_before == 0 && !veh.is_rotorcraft() && !veh.is_flying_in_air() ) {
        debugmsg( "%s tried to move %s with no velocity",
                  veh.name, vertical ? "vertically" : "horizontally" );
        return &veh;
    }

    bool veh_veh_coll_flag = false;
    // Try to collide multiple times
    size_t collision_attempts = 10;
    do {
        collisions.clear();
        veh.collision( collisions, dp1, false );

        // Vehicle collisions
        std::map<vehicle *, std::vector<veh_collision> > veh_collisions;
        for( veh_collision &coll : collisions ) {
            if( coll.type != veh_coll_veh ) {
                continue;
            }

            veh_veh_coll_flag = true;
            // Only collide with each vehicle once
            veh_collisions[ static_cast<vehicle *>( coll.target ) ].push_back( coll );
        }

        for( auto &pair : veh_collisions ) {
            impulse += vehicle_vehicle_collision( veh, *pair.first, pair.second );
        }

        // Non-vehicle collisions
        for( const veh_collision &coll : collisions ) {
            if( coll.type == veh_coll_veh ) {
                continue;
            }
            int part_num = veh.get_non_fake_part( coll.part );

            const point &collision_point = veh.part( coll.part ).mount;
            const int coll_dmg = coll.imp;

            // Shock damage, if the target part is a rotor treat as an aimed hit.
            // don't try to deal damage to invalid part (probably removed or destroyed)
            if( part_num != -1 ) {
                if( veh.part_info( part_num ).rotor_diameter() > 0 ) {
                    veh.damage( *this, part_num, coll_dmg, damage_type::BASH, true );
                } else {
                    impulse += coll_dmg;
                    veh.damage( *this, part_num, coll_dmg, damage_type::BASH );
                    veh.damage_all( coll_dmg / 2, coll_dmg, damage_type::BASH, collision_point );
                }
            }
        }

        // prevent vehicle bouncing after the first collision
        if( vertical && velocity_before < 0 && coll_velocity > 0 ) {
            veh.vertical_velocity = 0; // also affects `coll_velocity` and thus exits the loop
        }

    } while( collision_attempts-- > 0 && coll_velocity != 0 &&
             sgn( coll_velocity ) == sgn( velocity_before ) &&
             !collisions.empty() && !veh_veh_coll_flag );

    const int velocity_after = coll_velocity;
    bool can_move = velocity_after != 0 && sgn( velocity_after ) == sgn( velocity_before );
    if( dp.z != 0 && veh.is_rotorcraft() ) {
        can_move = true;
    }
    units::angle coll_turn = 0_degrees;
    if( impulse > 0 ) {
        coll_turn = shake_vehicle( veh, velocity_before, facing.dir() );
        veh.stop_autodriving();
        const int volume = std::min<int>( 100, std::sqrt( impulse ) );
        // TODO: Center the sound at weighted (by impulse) average of collisions
        sounds::sound( veh.global_pos3(), volume, sounds::sound_t::combat, _( "crash!" ),
                       false, "smash_success", "hit_vehicle" );
    }

    if( veh_veh_coll_flag ) {
        // Break here to let the hit vehicle move away
        return nullptr;
    }

    // If not enough wheels, mess up the ground a bit.
    if( !vertical && !veh.valid_wheel_config() && !veh.is_in_water() && !veh.is_flying_in_air() &&
        dp.z == 0 ) {
        veh.velocity += veh.velocity < 0 ? 2000 : -2000;
        for( const tripoint &p : veh.get_points() ) {
            const ter_id &pter = ter( p );
            if( pter == t_dirt || pter == t_grass ) {
                ter_set( p, t_dirtmound );
            }
        }
    }

    const units::angle last_turn_dec = 1_degrees;
    if( veh.last_turn < 0_degrees ) {
        veh.last_turn += last_turn_dec;
        if( veh.last_turn > -last_turn_dec ) {
            veh.last_turn = 0_degrees;
        }
    } else if( veh.last_turn > 0_degrees ) {
        veh.last_turn -= last_turn_dec;
        if( veh.last_turn < last_turn_dec ) {
            veh.last_turn = 0_degrees;
        }
    }

    Character &player_character = get_player_character();
    const bool seen = sees_veh( player_character, veh, false );

    if( can_move || ( vertical && veh.is_falling ) ) {
        // Accept new direction
        if( veh.skidding ) {
            veh.face.init( veh.turn_dir );
        } else {
            veh.face = facing;
        }

        veh.move = facing;
        if( coll_turn != 0_degrees ) {
            veh.skidding = true;
            veh.turn( coll_turn );
        }
        veh.on_move();
        // Actually change position
        displace_vehicle( veh, dp1 );
        level_vehicle( veh );
    } else if( !vertical ) {
        veh.stop();
    }
    veh.check_falling_or_floating();
    // If the PC is in the currently moved vehicle, adjust the
    //  view offset.
    if( player_character.controlling_vehicle &&
        veh_pointer_or_null( veh_at( player_character.pos() ) ) == &veh ) {
        g->calc_driving_offset( &veh );
        if( veh.skidding && can_move ) {
            // TODO: Make skid recovery in air hard
            veh.possibly_recover_from_skid();
        }
    }
    // Now we're gonna handle traps we're standing on (if we're still moving).
    if( !vertical && can_move ) {
        const auto wheel_indices = veh.wheelcache; // Don't use a reference here, it causes a crash.

        // Values to deal with crushing items.
        // The math needs to be floating-point to work, so the values might as well be.
        const float vehicle_grounded_wheel_area = static_cast<int>( vehicle_wheel_traction( veh, true ) );
        const float weight_to_damage_factor = 0.05f; // Nobody likes a magic number.
        const float vehicle_mass_kg = to_kilogram( veh.total_mass() );

        for( const int &w : wheel_indices ) {
            const tripoint wheel_p = veh.global_part_pos3( w );
            if( one_in( 2 ) && displace_water( wheel_p ) ) {
                sounds::sound( wheel_p, 4,  sounds::sound_t::movement, _( "splash!" ), false,
                               "environment", "splash" );
            }

            veh.handle_trap( wheel_p, w );
            if( !has_flag( ter_furn_flag::TFLAG_SEALED, wheel_p ) ) {
                const float wheel_area =  veh.part( w ).wheel_area();

                // Damage is calculated based on the weight of the vehicle,
                // The area of it's wheels, and the area of the wheel running over the items.
                // This number is multiplied by weight_to_damage_factor to get reasonable results, damage-wise.
                const int wheel_damage = static_cast<int>( ( ( wheel_area / vehicle_grounded_wheel_area ) *
                                         vehicle_mass_kg ) * weight_to_damage_factor );

                //~ %1$s: vehicle name
                smash_items( wheel_p, wheel_damage, string_format( _( "weight of %1$s" ), veh.disp_name() ) );
            }
        }
    }
    if( veh.is_towing() ) {
        veh.do_towing_move();
        // veh.do_towing_move() may cancel towing, so we need to recheck is_towing here
        if( veh.is_towing() && veh.tow_data.get_towed()->tow_cable_too_far() ) {
            add_msg( m_info, _( "A towing cable snaps off of %s." ),
                     veh.tow_data.get_towed()->disp_name() );
            veh.tow_data.get_towed()->invalidate_towing( true );
        }
    }
    // Redraw scene, but only if the player is not engaged in an activity and
    // the vehicle was seen before or after the move.
    if( !player_character.activity && ( seen || sees_veh( player_character, veh, true ) ) ) {
        g->invalidate_main_ui_adaptor();
        ui_manager::redraw_invalidated();
        handle_key_blocking_activity();
    }
    return &veh;
}

float map::vehicle_vehicle_collision( vehicle &veh, vehicle &veh2,
                                      const std::vector<veh_collision> &collisions )
{
    if( &veh == &veh2 ) {
        debugmsg( "Vehicle %s collided with itself", veh.name );
        return 0.0f;
    }

    // Effects of colliding with another vehicle:
    //  transfers of momentum, skidding,
    //  parts are damaged/broken on both sides,
    //  remaining times are normalized
    const veh_collision &c = collisions[0];
    const bool vertical = veh.sm_pos.z != veh2.sm_pos.z;

    // Check whether avatar sees the collision, and log a message if so
    const avatar &you = get_avatar();
    const tripoint part1_pos = veh.global_part_pos3( c.part );
    const tripoint part2_pos = veh2.global_part_pos3( c.target_part );
    if( you.sees( part1_pos ) || you.sees( part2_pos ) ) {
        //~ %1$s: first vehicle name (without "the")
        //~ %2$s: first part name
        //~ %3$s: second vehicle display name (with "the")
        //~ %4$s: second part name
        add_msg( m_bad, _( "The %1$s's %2$s collides with %3$s's %4$s." ),
                 veh.name,  veh.part_info( c.part ).name(),
                 veh2.disp_name(), veh2.part_info( c.target_part ).name() );
    }

    // Used to calculate the epicenter of the collision.
    point epicenter1;
    point epicenter2;

    float dmg;
    // Vertical collisions will be simpler for a while (1D)
    if( !vertical ) {
        // For reference, a cargo truck weighs ~25300, a bicycle 690,
        //  and 38mph is 3800 'velocity'
        rl_vec2d velo_veh1 = veh.velo_vec();
        rl_vec2d velo_veh2 = veh2.velo_vec();
        const float m1 = to_kilogram( veh.total_mass() );
        const float m2 = to_kilogram( veh2.total_mass() );
        //Energy of vehicle1 and vehicle2 before collision
        float E = 0.5 * m1 * velo_veh1.magnitude() * velo_veh1.magnitude() +
                  0.5 * m2 * velo_veh2.magnitude() * velo_veh2.magnitude();

        // Collision_axis
        point cof1 = veh .rotated_center_of_mass();
        point cof2 = veh2.rotated_center_of_mass();
        int &x_cof1 = cof1.x;
        int &y_cof1 = cof1.y;
        int &x_cof2 = cof2.x;
        int &y_cof2 = cof2.y;
        rl_vec2d collision_axis_y;

        collision_axis_y.x = ( veh.global_pos3().x + x_cof1 ) - ( veh2.global_pos3().x + x_cof2 );
        collision_axis_y.y = ( veh.global_pos3().y + y_cof1 ) - ( veh2.global_pos3().y + y_cof2 );
        collision_axis_y = collision_axis_y.normalized();
        rl_vec2d collision_axis_x = collision_axis_y.rotated( M_PI / 2 );
        // imp? & delta? & final? reworked:
        // newvel1 =( vel1 * ( mass1 - mass2 ) + ( 2 * mass2 * vel2 ) ) / ( mass1 + mass2 )
        // as per http://en.wikipedia.org/wiki/Elastic_collision
        //velocity of veh1 before collision in the direction of collision_axis_y
        float vel1_y = collision_axis_y.dot_product( velo_veh1 );
        float vel1_x = collision_axis_x.dot_product( velo_veh1 );
        //velocity of veh2 before collision in the direction of collision_axis_y
        float vel2_y = collision_axis_y.dot_product( velo_veh2 );
        float vel2_x = collision_axis_x.dot_product( velo_veh2 );
        // e = 0 -> inelastic collision
        // e = 1 -> elastic collision
        float e = get_collision_factor( vel1_y / 100 - vel2_y / 100 );

        // Velocity after collision
        // vel1_x_a = vel1_x, because in x-direction we have no transmission of force
        float vel1_x_a = vel1_x;
        float vel2_x_a = vel2_x;
        // Transmission of force only in direction of collision_axix_y
        // Equation: partially elastic collision
        float vel1_y_a = ( m2 * vel2_y * ( 1 + e ) + vel1_y * ( m1 - m2 * e ) ) / ( m1 + m2 );
        float vel2_y_a = ( m1 * vel1_y * ( 1 + e ) + vel2_y * ( m2 - m1 * e ) ) / ( m1 + m2 );
        // Add both components; Note: collision_axis is normalized
        rl_vec2d final1 = collision_axis_y * vel1_y_a + collision_axis_x * vel1_x_a;
        rl_vec2d final2 = collision_axis_y * vel2_y_a + collision_axis_x * vel2_x_a;

        veh.move.init( final1.as_point() );
        if( final1.dot_product( veh.face_vec() ) < 0 ) {
            // Car is being pushed backwards. Make it move backwards
            veh.velocity = -final1.magnitude();
        } else {
            veh.velocity = final1.magnitude();
        }

        veh2.move.init( final2.as_point() );
        if( final2.dot_product( veh2.face_vec() ) < 0 ) {
            // Car is being pushed backwards. Make it move backwards
            veh2.velocity = -final2.magnitude();
        } else {
            veh2.velocity = final2.magnitude();
        }

        //give veh2 the initiative to proceed next before veh1
        float avg_of_turn = ( veh2.of_turn + veh.of_turn ) / 2.0f;
        if( avg_of_turn < 0.1f ) {
            avg_of_turn = 0.1f;
        }

        veh.of_turn = avg_of_turn * 0.9f;
        veh2.of_turn = avg_of_turn * 1.1f;

        //Energy after collision
        float E_a = 0.5 * m1 * final1.magnitude() * final1.magnitude() +
                    0.5 * m2 * final2.magnitude() * final2.magnitude();
        float d_E = E - E_a;  //Lost energy at collision -> deformation energy
        dmg = std::abs( d_E / 1000 / 2000 );  //adjust to balance damage
    } else {
        const float m1 = to_kilogram( veh.total_mass() );
        // Collision is perfectly inelastic for simplicity
        // Assume veh2 is standing still
        dmg = std::abs( veh.vertical_velocity / 100 ) * m1 / 10;
        veh.vertical_velocity = 0;
    }

    float dmg_veh1 = dmg * 0.5f;
    float dmg_veh2 = dmg * 0.5f;

    int coll_parts_cnt = 0; //quantity of colliding parts between veh1 and veh2
    for( const veh_collision &veh_veh_coll : collisions ) {
        if( &veh2 == static_cast<vehicle *>( veh_veh_coll.target ) ) {
            coll_parts_cnt++;
        }
    }

    // Prevent potential division by zero
    if( coll_parts_cnt == 0 ) {
        debugmsg( "Vehicle %s collided with bugs in the code", veh.name );
        return 0.0f;
    }

    const float dmg1_part = dmg_veh1 / coll_parts_cnt;
    const float dmg2_part = dmg_veh2 / coll_parts_cnt;

    //damage colliding parts (only veh1 and veh2 parts)
    for( const veh_collision &veh_veh_coll : collisions ) {
        if( &veh2 != static_cast<vehicle *>( veh_veh_coll.target ) ) {
            continue;
        }
        int coll_part = veh.get_non_fake_part( veh_veh_coll.part );
        int target_part = veh2.get_non_fake_part( veh_veh_coll.target_part );

        int coll_parm = veh.part_with_feature( coll_part, VPFLAG_ARMOR, true );
        if( coll_parm < 0 ) {
            coll_parm = coll_part;
        }
        int target_parm = veh2.part_with_feature( target_part, VPFLAG_ARMOR, true );
        if( target_parm < 0 ) {
            target_parm = target_part;
        }

        epicenter1 += veh.part( coll_parm ).mount;
        veh.damage( *this, coll_parm, dmg1_part, damage_type::BASH );

        epicenter2 += veh2.part( target_parm ).mount;
        veh2.damage( *this, target_parm, dmg2_part, damage_type::BASH );
    }

    epicenter2.x /= coll_parts_cnt;
    epicenter2.y /= coll_parts_cnt;

    if( dmg2_part > 100 ) {
        // Shake vehicle because of collision
        veh2.damage_all( dmg2_part / 2, dmg2_part, damage_type::BASH, epicenter2 );
    }

    if( dmg_veh1 > 800 ) {
        veh.skidding = true;
    }

    if( dmg_veh2 > 800 ) {
        veh2.skidding = true;
    }

    // Return the impulse of the collision
    return dmg_veh1;
}

bool map::check_vehicle_zones( const int zlev )
{
    for( vehicle *veh : get_cache( zlev ).zone_vehicles ) {
        if( veh->zones_dirty ) {
            return true;
        }
    }
    return false;
}

std::vector<zone_data *> map::get_vehicle_zones( const int zlev )
{
    std::vector<zone_data *> veh_zones;
    bool rebuild = false;
    for( vehicle *veh : get_cache( zlev ).zone_vehicles ) {
        if( veh->refresh_zones() ) {
            rebuild = true;
        }
        for( auto &zone : veh->loot_zones ) {
            veh_zones.emplace_back( &zone.second );
        }
    }
    if( rebuild ) {
        zone_manager::get_manager().cache_vzones();
    }
    return veh_zones;
}

void map::register_vehicle_zone( vehicle *veh, const int zlev )
{
    level_cache &ch = get_cache( zlev );
    ch.zone_vehicles.insert( veh );
}

bool map::deregister_vehicle_zone( zone_data &zone ) const
{
    if( const std::optional<vpart_reference> vp = veh_at( getlocal(
                zone.get_start_point() ) ).part_with_feature( "CARGO", false ) ) {
        auto bounds = vp->vehicle().loot_zones.equal_range( vp->mount() );
        for( auto it = bounds.first; it != bounds.second; it++ ) {
            if( &zone == &( it->second ) ) {
                vp->vehicle().loot_zones.erase( it );
                return true;
            }
        }
    }
    return false;
}

// 3D vehicle functions

VehicleList map::get_vehicles( const tripoint &start, const tripoint &end )
{
    const int chunk_sx = std::max( 0, ( start.x / SEEX ) - 1 );
    const int chunk_ex = std::min( my_MAPSIZE - 1, ( end.x / SEEX ) + 1 );
    const int chunk_sy = std::max( 0, ( start.y / SEEY ) - 1 );
    const int chunk_ey = std::min( my_MAPSIZE - 1, ( end.y / SEEY ) + 1 );
    const int chunk_sz = start.z;
    const int chunk_ez = end.z;
    VehicleList vehs;

    for( int cx = chunk_sx; cx <= chunk_ex; ++cx ) {
        for( int cy = chunk_sy; cy <= chunk_ey; ++cy ) {
            for( int cz = chunk_sz; cz <= chunk_ez; ++cz ) {
                submap *current_submap = get_submap_at_grid( { cx, cy, cz } );
                if( current_submap == nullptr ) {
                    debugmsg( "Tried to process vehicle at (%d,%d,%d) but the submap is not loaded", cx, cy, cz );
                    continue;
                }
                for( const auto &elem : current_submap->vehicles ) {
                    // Ensure the vehicle z-position is correct
                    elem->sm_pos.z = cz;
                    wrapped_vehicle w;
                    w.v = elem.get();
                    w.pos = w.v->global_pos3();
                    vehs.push_back( w );
                }
            }
        }
    }

    return vehs;
}

optional_vpart_position map::veh_at( const tripoint_abs_ms &p ) const
{
    return veh_at( getlocal( p ) );
}

optional_vpart_position map::veh_at( const tripoint &p ) const
{
    if( !inbounds( p ) || !const_cast<map *>( this )->get_cache( p.z ).get_veh_in_active_range() ) {
        return optional_vpart_position( std::nullopt );
    }

    int part_num = 1;
    vehicle *const veh = const_cast<map *>( this )->veh_at_internal( p, part_num );
    if( !veh ) {
        return optional_vpart_position( std::nullopt );
    }
    return optional_vpart_position( vpart_position( *veh, part_num ) );

}

optional_vpart_position map::veh_at( const tripoint_bub_ms &p ) const
{
    return veh_at( p.raw() );
}

const vehicle *map::veh_at_internal( const tripoint &p, int &part_num ) const
{
    // This function is called A LOT. Move as much out of here as possible.
    const level_cache &ch = get_cache( p.z );
    if( !ch.get_veh_in_active_range() || !ch.get_veh_exists_at( p ) ) {
        part_num = -1;
        return nullptr; // Clear cache indicates no vehicle. This should optimize a great deal.
    }

    std::pair<vehicle *, int> ret = ch.get_veh_cached_parts( p );
    if( ret.first ) {
        part_num = ret.second;
        return ret.first;
    }

    debugmsg( "vehicle part cache indicated vehicle not found: %d %d %d", p.x, p.y, p.z );
    part_num = -1;
    return nullptr;
}

vehicle *map::veh_at_internal( const tripoint &p, int &part_num )
{
    return const_cast<vehicle *>( const_cast<const map *>( this )->veh_at_internal( p, part_num ) );
}

void map::board_vehicle( const tripoint &pos, Character *p )
{
    if( p == nullptr ) {
        debugmsg( "map::board_vehicle: null player" );
        return;
    }

    const std::optional<vpart_reference> vp = veh_at( pos ).part_with_feature( VPFLAG_BOARDABLE,
            true );
    if( !vp ) {
        avatar *player_character = p->as_avatar();
        if( player_character != nullptr &&
            player_character->grab_point.x == 0 && player_character->grab_point.y == 0 ) {
            debugmsg( "map::board_vehicle: vehicle with unbroken and BOARDABLE part not found" );
        }
        return;
    }
    if( vp->part().has_flag( vehicle_part::passenger_flag ) ) {
        Character *psg = vp->vehicle().get_passenger( vp->part_index() );
        debugmsg( "map::board_vehicle: passenger (%s) is already there",
                  psg ? psg->get_name() : "<null>" );
        unboard_vehicle( pos );
    }
    vp->part().set_flag( vehicle_part::passenger_flag );
    vp->part().passenger_id = p->getID();
    vp->vehicle().invalidate_mass();

    p->setpos( pos );
    p->in_vehicle = true;
    if( p->is_avatar() ) {
        g->update_map( *p->as_avatar() );
    }
}

void map::unboard_vehicle( const vpart_reference &vp, Character *passenger, bool dead_passenger )
{
    // Mark the part as un-occupied regardless of whether there's a live passenger here.
    vp.part().remove_flag( vehicle_part::passenger_flag );
    vp.vehicle().invalidate_mass();

    if( !passenger ) {
        if( !dead_passenger ) {
            debugmsg( "map::unboard_vehicle: passenger not found" );
        }
        return;
    }
    passenger->in_vehicle = false;
    if( passenger->controlling_vehicle ) {
        // If the driver left, stop autodriving.
        vp.vehicle().stop_autodriving( false );
        // Only make vehicle go out of control if the driver is the one unboarding.
        vp.vehicle().skidding = true;
    }
    passenger->controlling_vehicle = false;
}

void map::unboard_vehicle( const tripoint &p, bool dead_passenger )
{
    const std::optional<vpart_reference> vp = veh_at( p ).part_with_feature( VPFLAG_BOARDABLE, false );
    Character *passenger = nullptr;
    if( !vp ) {
        debugmsg( "map::unboard_vehicle: vehicle not found" );
        // Try and force unboard the player anyway.
        passenger = get_creature_tracker().creature_at<Character>( p );
        if( passenger ) {
            passenger->in_vehicle = false;
            passenger->controlling_vehicle = false;
        }
        return;
    }
    passenger = vp->get_passenger();
    unboard_vehicle( *vp, passenger, dead_passenger );
}

bool map::displace_vehicle( vehicle &veh, const tripoint &dp, const bool adjust_pos,
                            const std::set<int> &parts_to_move )
{
    const tripoint_bub_ms src = veh.pos_bub();
    // handle vehicle ramps
    int ramp_offset = 0;
    if( adjust_pos ) {
        if( has_flag( ter_furn_flag::TFLAG_RAMP_UP, src + dp ) ) {
            ramp_offset += 1;
            veh.is_on_ramp = true;
        } else if( has_flag( ter_furn_flag::TFLAG_RAMP_DOWN, src + dp ) ) {
            ramp_offset -= 1;
            veh.is_on_ramp = true;
        }
    }

    const tripoint_bub_ms dst =
        src + ( adjust_pos ? ( dp + tripoint( 0, 0, ramp_offset ) ) : tripoint_zero );

    if( !inbounds( src ) ) {
        add_msg_debug( debugmode::DF_MAP,
                       "map::displace_vehicle: coordinates out of bounds %d,%d,%d->%d,%d,%d",
                       src.x(), src.y(), src.z(), dst.x(), dst.y(), dst.z() );
        return false;
    }

    point src_offset;
    point dst_offset;
    submap *src_submap = get_submap_at( src.raw(), src_offset );
    submap *const dst_submap = get_submap_at( dst.raw(), dst_offset );
    if( src_submap == nullptr || dst_submap == nullptr ) {
        debugmsg( "Tried to displace vehicle at (%d,%d) but the submap is not loaded", src_offset.x,
                  src_offset.y );
        return true;
    }
    std::set<int> smzs;

    // first, let's find our position in current vehicles vector
    size_t our_i = 0;
    bool found = false;
    for( submap *&smap : grid ) {
        for( size_t i = 0; i < smap->vehicles.size(); i++ ) {
            if( smap->vehicles[i].get() == &veh ) {
                our_i = i;
                src_submap = smap;
                found = true;
                break;
            }
        }
        if( found ) {
            break;
        }
    }

    if( !found ) {
        add_msg_debug( debugmode::DF_MAP, "displace_vehicle [%s] failed", veh.name );
        return false;
    }

    // move the vehicle
    // don't let it go off grid
    if( !inbounds( dst ) ) {
        veh.stop();
        // Silent debug
        dbg( D_ERROR ) << "map:displace_vehicle: Stopping vehicle, displaced dp=("
                       << dp.x << ", " << dp.y << ", " << dp.z << ")";
        return true;
    }

    Character &player_character = get_player_character();
    // Need old coordinates to check for remote control
    const bool remote = veh.remote_controlled( player_character );

    // record every passenger and pet inside
    std::vector<rider_data> riders = veh.get_riders();

    bool need_update = false;
    int z_change = 0;
    // Move passengers and pets
    bool complete = false;
    creature_tracker &creatures = get_creature_tracker();
    // loop until everyone has moved or for each passenger
    for( size_t i = 0; !complete && i < riders.size(); i++ ) {
        complete = true;
        for( rider_data &r : riders ) {
            if( r.moved ) {
                continue;
            }
            const int prt = r.prt;
            if( !parts_to_move.empty() && parts_to_move.find( prt ) == parts_to_move.end() ) {
                r.moved = true;
                continue;
            }
            Creature *psg = r.psg;
            const tripoint part_pos = veh.global_part_pos3( prt );
            if( psg == nullptr ) {
                debugmsg( "Empty passenger for part #%d at %d,%d,%d player at %d,%d,%d?",
                          prt, part_pos.x, part_pos.y, part_pos.z,
                          player_character.posx(), player_character.posy(), player_character.posz() );
                veh.part( prt ).remove_flag( vehicle_part::passenger_flag );
                r.moved = true;
                continue;
            }

            if( psg->pos() != part_pos ) {
                add_msg_debug( debugmode::DF_MAP, "Part/passenger position mismatch: part #%d at %d,%d,%d "
                               "passenger at %d,%d,%d", prt, part_pos.x, part_pos.y, part_pos.z,
                               psg->posx(), psg->posy(), psg->posz() );
            }
            const vehicle_part &veh_part = veh.part( prt );

            // ramps make everything super tricky
            int psg_offset_z = -ramp_offset;
            tripoint next_pos; // defaults to 0,0,0
            if( parts_to_move.empty() ) {
                next_pos = veh_part.precalc[1];
            }
            if( has_flag( ter_furn_flag::TFLAG_RAMP_UP, src + dp + next_pos ) ) {
                psg_offset_z += 1;
            } else if( has_flag( ter_furn_flag::TFLAG_RAMP_DOWN, src + dp + next_pos ) ) {
                psg_offset_z -= 1;
            }

            // Place passenger on the new part location
            tripoint_bub_ms psgp( dst + next_pos + tripoint( 0, 0, psg_offset_z ) );
            // someone is in the way so try again
            if( creatures.creature_at( psgp ) ) {
                complete = false;
                continue;
            }
            if( psg->is_avatar() ) {
                // If passenger is you, we need to update the map
                need_update = true;
                z_change = psgp.z() - part_pos.z;
            }

            psg->setpos( psgp.raw() );
            r.moved = true;
        }
    }

    veh.shed_loose_parts( &src, &dst );
    smzs = veh.advance_precalc_mounts( dst_offset, src.raw(), dp, ramp_offset,
                                       adjust_pos, parts_to_move );
    veh.update_active_fakes();

    if( src_submap != dst_submap ) {
        dst_submap->ensure_nonuniform();
        veh.set_submap_moved( tripoint( dst.x() / SEEX, dst.y() / SEEY, dst.z() ) );
        auto src_submap_veh_it = src_submap->vehicles.begin() + our_i;
        dst_submap->vehicles.push_back( std::move( *src_submap_veh_it ) );
        src_submap->vehicles.erase( src_submap_veh_it );
        invalidate_max_populated_zlev( dst.z() );
    }
    if( need_update ) {
        g->update_map( player_character );
    }
    add_vehicle_to_cache( &veh );

    if( z_change || src.z() != dst.z() ) {
        if( z_change ) {
            g->vertical_move( z_change, true );
            // vertical moves can flush the caches, so make sure we're still in the cache
            add_vehicle_to_cache( &veh );
        }
        update_vehicle_list( dst_submap, dst.z() );
        // delete the vehicle from the source z-level vehicle cache set if it is no longer on
        // that z-level
        if( src.z() != dst.z() ) {
            level_cache &ch2 = get_cache( src.z() );
            for( const vehicle *elem : ch2.vehicle_list ) {
                if( elem == &veh ) {
                    ch2.vehicle_list.erase( &veh );
                    ch2.zone_vehicles.erase( &veh );
                    break;
                }
            }
        }
        veh.check_is_heli_landed();
    }

    if( remote ) {
        // Has to be after update_map or coordinates won't be valid
        g->setremoteveh( &veh );
    }

    veh.zones_dirty = true; // invalidate zone positions

    for( int vsmz : smzs ) {
        on_vehicle_moved( dst.z() + vsmz );
    }
    return true;
}

void map::level_vehicle( vehicle &veh )
{
    int cnt = 0;
    while( !veh.level_vehicle() && cnt < ( 2 * OVERMAP_DEPTH ) ) {
        cnt++;
    }
}

bool map::displace_water( const tripoint &p )
{
    // Check for shallow water
    if( has_flag_ter( ter_furn_flag::TFLAG_SHALLOW_WATER, p ) ) {
        int dis_places = 0;
        int sel_place = 0;
        for( int pass = 0; pass < 2; pass++ ) {
            // we do 2 passes.
            // first, count how many non-water places around
            // then choose one within count and fill it with water on second pass
            if( pass != 0 ) {
                sel_place = rng( 0, dis_places - 1 );
                dis_places = 0;
            }
            for( const tripoint &temp : points_in_radius( p, 1 ) ) {
                if( temp != p
                    || impassable_ter_furn( temp )
                    || has_flag( ter_furn_flag::TFLAG_DEEP_WATER, temp ) ) {
                    continue;
                }
                if( has_flag_ter( ter_furn_flag::TFLAG_SHALLOW_WATER, p ) ||
                    has_flag_ter( ter_furn_flag::TFLAG_DEEP_WATER, p ) ) {
                    continue;
                }
                if( pass != 0 && dis_places == sel_place ) {
                    ter_set( temp, t_water_sh );
                    ter_set( temp, t_dirt );
                    return true;
                }

                dis_places++;
            }
        }
    }
    return false;
}

// End of 3D vehicle

void map::set( const tripoint &p, const ter_id &new_terrain, const furn_id &new_furniture )
{
    furn_set( p, new_furniture );
    ter_set( p, new_terrain );
}

std::string map::name( const tripoint &p )
{
    return has_furn( p ) ? furnname( p ) : tername( p );
}

std::string map::name( const tripoint_bub_ms &p )
{
    return name( p.raw() );
}

std::string map::disp_name( const tripoint &p )
{
    return string_format( _( "the %s" ), name( p ) );
}

std::string map::obstacle_name( const tripoint &p )
{
    if( const std::optional<vpart_reference> vp = veh_at( p ).obstacle_at_part() ) {
        return vp->info().name();
    }
    return name( p );
}

bool map::has_furn( const tripoint &p ) const
{
    return furn( p ) != f_null;
}

bool map::has_furn( const tripoint_bub_ms &p ) const
{
    return has_furn( p.raw() );
}

furn_id map::furn( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return f_null;
    }

    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried process furniture at (%d,%d) but the submap is not loaded", l.x, l.y );
        return f_null;
    }

    return current_submap->get_furn( l );
}

furn_id map::furn( const tripoint_bub_ms &p ) const
{
    return furn( p.raw() );
}

bool map::furn_set( const tripoint &p, const furn_id &new_furniture, const bool furn_reset,
                    bool avoid_creatures )
{
    if( !inbounds( p ) ) {
        debugmsg( "map::furn_set %s out of bounds", p.to_string() );
        return false;
    }
    if( avoid_creatures ) {
        Creature *c = get_creature_tracker().creature_at( tripoint_abs_ms( getabs( p ) ), true );
        if( c ) {
            return false;
        }
    }
    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to set furniture at (%d,%d) but the submap is not loaded", l.x, l.y );
        return false;
    }
    const furn_id new_target_furniture = new_furniture == f_clear ? f_null : new_furniture;
    const furn_id old_id = current_submap->get_furn( l );
    if( old_id == new_target_furniture ) {
        // Nothing changed
        return true;
    }

    current_submap->set_furn( l, new_target_furniture );

    // Set the dirty flags
    const furn_t &old_f = old_id.obj();
    const furn_t &new_f = new_target_furniture.obj();

    bool result = true;

    if( current_submap->is_open_air( l ) &&
        !new_f.has_flag( ter_furn_flag::TFLAG_ALLOW_ON_OPEN_AIR ) &&
        new_target_furniture != f_null ) {
        //待定
        /*debugmsg( "Setting furniture %s at %s where terrain is %s (which is_open_air)\n"
                  "If this is intentional, set the ALLOW_ON_OPEN_AIR flag on the furniture",
                  new_target_furniture.id().str(), p.to_string(), current_ter.id().str() );*/
        result = false;
    }

    if( new_f.has_flag( ter_furn_flag::TFLAG_PLANT ) ) {
        if( current_submap->get_ter( l ) == t_dirtmound ) {
            ter_set( p, t_dirt );
        }
    }

    avatar &player_character = get_avatar();
    // If player has grabbed this furniture and it's no longer grabbable, release the grab.
    if( player_character.get_grab_type() == object_type::FURNITURE &&
        !furn_reset &&
        player_character.pos() + player_character.grab_point == p && !new_f.is_movable() ) {
        add_msg( _( "The %s you were grabbing is destroyed!" ), old_f.name() );
        player_character.grab( object_type::NONE );
    }
    // If a creature was crushed under a rubble -> free it
    if( old_id == f_rubble && new_target_furniture == f_null ) {
        Creature *c = get_creature_tracker().creature_at( p );
        if( c ) {
            c->remove_effect( effect_crushed );
        }
    }
    if( !new_f.emissions.empty() ) {
        field_furn_locs.push_back( p );
    }
    if( old_f.transparent != new_f.transparent ) {
        set_transparency_cache_dirty( p );
        set_seen_cache_dirty( p );
    }

    if( old_f.has_flag( ter_furn_flag::TFLAG_INDOORS ) != new_f.has_flag(
            ter_furn_flag::TFLAG_INDOORS ) ) {
        set_outside_cache_dirty( p.z );
    }

    if( old_f.has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) != new_f.has_flag(
            ter_furn_flag::TFLAG_NO_FLOOR ) ) {
        set_floor_cache_dirty( p.z );
        set_seen_cache_dirty( p );
        set_visitable_zones_cache_dirty();
    }

    if( old_f.has_flag( ter_furn_flag::TFLAG_SUN_ROOF_ABOVE ) != new_f.has_flag(
            ter_furn_flag::TFLAG_SUN_ROOF_ABOVE ) ) {
        set_floor_cache_dirty( p.z + 1 );
    }

    invalidate_max_populated_zlev( p.z );

    set_memory_seen_cache_dirty( p );


    if ((old_f.movecost < 0) != (new_f.movecost < 0)) {
        set_visitable_zones_cache_dirty();
    }

    // TODO: Limit to changes that affect move cost, traps and stairs
    set_pathfinding_cache_dirty( p.z );

    // Make sure the furniture falls if it needs to
    support_dirty( p );
    tripoint above( p.xy(), p.z + 1 );
    // Make sure that if we supported something and no longer do so, it falls down
    support_dirty( above );

    return result;
}

bool map::furn_set( const tripoint_bub_ms &p, const furn_id &new_furniture, const bool furn_reset,
                    bool avoid_creatures )
{
    return furn_set( p.raw(), new_furniture, furn_reset, avoid_creatures );
}

bool map::can_move_furniture( const tripoint &pos, Character *you ) const
{
    if( !you ) {
        return false;
    }
    const furn_t &furniture_type = furn( pos ).obj();
    int required_str = furniture_type.move_str_req;

    // Object can not be moved (or nothing there)
    if( required_str < 0 ) {
        return false;
    }

    ///\EFFECT_STR determines what furniture the player can move
    int adjusted_str = you->str_cur;
    if( you->is_mounted() ) {
        auto *mons = you->mounted_creature.get();
        if( mons->has_flag( MF_RIDEABLE_MECH ) && mons->mech_str_addition() != 0 ) {
            adjusted_str = mons->mech_str_addition();
        }
    }
    return adjusted_str >= required_str;
}

std::string map::furnname( const tripoint &p )
{
    const furn_t &f = furn( p ).obj();
    if( f.has_flag( ter_furn_flag::TFLAG_PLANT ) ) {
        // Can't use item_stack::only_item() since there might be fertilizer
        map_stack items = i_at( p );
        const map_stack::iterator seed = std::find_if( items.begin(), items.end(), []( const item & it ) {
            return it.is_seed();
        } );
        if( seed == items.end() ) {
            debugmsg( "Missing seed for plant at (%d, %d, %d)", p.x, p.y, p.z );
            return "null";
        }
        const std::string &plant = seed->get_plant_name();
        return string_format( "%s (%s)", f.name(), plant );
    } else {
        return f.name();
    }
}

std::string map::furnname( const tripoint_bub_ms &p )
{
    return furnname( p.raw() );
}

/*
 * Get the terrain integer id. This is -not- a number guaranteed to remain
 * the same across revisions; it is a load order, and can change when mods
 * are loaded or removed. The old t_floor style constants will still work but
 * are -not- guaranteed; if a mod removes t_lava, t_lava will equal t_null;
 * New terrains added to the core game generally do not need this, it's
 * retained for high performance comparisons, save/load, and gradual transition
 * to string terrain.id
 */
ter_id map::ter( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return t_null;
    }

    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to process terrain at (%d,%d) but the submap is not loaded", l.x, l.y );
        return t_null;
    }

    return current_submap->get_ter( l );
}

ter_id map::ter( const tripoint_bub_ms &p ) const
{
    return ter( p.raw() );
}

uint8_t map::get_known_connections( const tripoint &p,
                                    const std::bitset<NUM_TERCONN> &connect_group,
                                    const std::map<tripoint, ter_id> &override ) const
{
    if( connect_group.none() ) {
        return 0;
    }

    const level_cache &ch = access_cache( p.z );
    uint8_t val = 0;
    std::function<bool( const tripoint & )> is_memorized;
    avatar &player_character = get_avatar();
#ifdef TILES
    if( use_tiles ) {
        is_memorized =
        [&]( const tripoint & q ) {
            return !player_character.get_memorized_tile( getabs( q ) ).tile.empty();
        };
    } else {
#endif
        is_memorized =
        [&]( const tripoint & q ) {
            return player_character.get_memorized_symbol( getabs( q ) );
        };
#ifdef TILES
    }
#endif

    const bool overridden = override.find( p ) != override.end();
    const bool is_transparent = ch.transparency_cache[p.x][p.y] > LIGHT_TRANSPARENCY_SOLID;

    // populate connection information
    for( int i = 0; i < 4; ++i ) {
        tripoint neighbour = p + offsets[i];
        if( !inbounds( neighbour ) ) {
            continue;
        }
        const auto neighbour_override = override.find( neighbour );
        const bool neighbour_overridden = neighbour_override != override.end();
        // if there's some non-memory terrain to show at the neighboring tile
        const bool may_connect = neighbour_overridden ||
                                 get_visibility( ch.visibility_cache[neighbour.x][neighbour.y],
                                         get_visibility_variables_cache() ) == visibility_type::CLEAR ||
                                 // or if an actual center tile is transparent or next to a memorized tile
                                 ( !overridden && ( is_transparent || is_memorized( neighbour ) ) );
        if( may_connect ) {
            const ter_t &neighbour_terrain = neighbour_overridden ?
                                             neighbour_override->second.obj() : ter( neighbour ).obj();
            if( neighbour_terrain.in_connect_groups( connect_group ) ) {
                val += 1 << i;
            }
        }
    }

    return val;
}

uint8_t map::get_known_rotates_to( const tripoint &p,
                                   const std::bitset<NUM_TERCONN> &rotate_to_group,
                                   const std::map<tripoint, ter_id> &override ) const
{
    if( rotate_to_group.none() ) {
        return CHAR_MAX;
    }

    uint8_t val = 0;

    // populate connection information
    for( int i = 0; i < 4; ++i ) {
        tripoint neighbour = p + offsets[i];
        if( !inbounds( neighbour ) ) {
            continue;
        }
        const auto neighbour_override = override.find( neighbour );
        const bool neighbour_overridden = neighbour_override != override.end();

        const ter_t &neighbour_terrain = neighbour_overridden ?
                                         neighbour_override->second.obj() : ter( neighbour ).obj();
        if( neighbour_terrain.in_connect_groups( rotate_to_group ) ) {
            val += 1 << i;
        }
    }

    return val;
}

uint8_t map::get_known_connections_f( const tripoint &p,
                                      const std::bitset<NUM_TERCONN> &connect_group,
                                      const std::map<tripoint, furn_id> &override ) const
{
    if( connect_group.none() ) {
        return 0;
    }

    const level_cache &ch = access_cache( p.z );
    uint8_t val = 0;
    std::function<bool( const tripoint & )> is_memorized;
    avatar &player_character = get_avatar();
#ifdef TILES
    if( use_tiles ) {
        is_memorized = [&]( const tripoint & q ) {
            return !player_character.get_memorized_tile( getabs( q ) ).tile.empty();
        };
    } else {
#endif
        is_memorized = [&]( const tripoint & q ) {
            return player_character.get_memorized_symbol( getabs( q ) );
        };
#ifdef TILES
    }
#endif

    const bool overridden = override.find( p ) != override.end();
    const bool is_transparent = ch.transparency_cache[p.x][p.y] > LIGHT_TRANSPARENCY_SOLID;

    // populate connection information
    for( int i = 0; i < 4; ++i ) {
        tripoint pt = p + offsets[i];
        if( !inbounds( pt ) ) {
            continue;
        }
        const auto neighbour_override = override.find( pt );
        const bool neighbour_overridden = neighbour_override != override.end();
        // if there's some non-memory terrain to show at the neighboring tile
        const bool may_connect = neighbour_overridden ||
                                 get_visibility( ch.visibility_cache[pt.x][pt.y],
                                         get_visibility_variables_cache() ) ==
                                 visibility_type::CLEAR ||
                                 // or if an actual center tile is transparent or
                                 // next to a memorized tile
                                 ( !overridden && ( is_transparent || is_memorized( pt ) ) );
        if( may_connect ) {
            const furn_t &neighbour_furn = neighbour_overridden ?
                                           neighbour_override->second.obj() : furn( pt ).obj();
            if( neighbour_furn.in_connect_groups( connect_group ) ) {
                val += 1 << i;
            }
        }
    }

    return val;
}

uint8_t map::get_known_rotates_to_f( const tripoint &p,
                                     const std::bitset<NUM_TERCONN> &rotate_to_group,
                                     const std::map<tripoint, ter_id> &override,
                                     const std::map<tripoint, furn_id> &override_f ) const
{
    if( rotate_to_group.none() ) {
        return CHAR_MAX;
    }

    uint8_t val = 0;

    // populate connection information
    for( int i = 0; i < 4; ++i ) {
        tripoint pt = p + offsets[i];
        if( !inbounds( pt ) ) {
            continue;
        }
        const auto neighbour_override = override.find( pt );
        const bool neighbour_overridden = neighbour_override != override.end();

        const auto neighbour_override_f = override_f.find( pt );
        const bool neighbour_overridden_f = neighbour_override_f != override_f.end();

        const ter_t &neighbour = neighbour_overridden ?
                                 neighbour_override->second.obj() : ter( pt ).obj();

        const furn_t &neighbour_f = neighbour_overridden_f ?
                                    neighbour_override_f->second.obj() : furn( pt ).obj();

        if( neighbour.in_connect_groups( rotate_to_group ) ||
            neighbour_f.in_connect_groups( rotate_to_group ) ) {
            val += 1 << i;
        }
    }

    return val;
}

/*
 * Get the results of harvesting this tile's furniture or terrain
 */
const harvest_id &map::get_harvest( const tripoint &pos ) const
{
    const auto furn_here = furn( pos );
    if( !furn_here->has_flag( ter_furn_flag::TFLAG_HARVESTED ) ) {
        const harvest_id &harvest = furn_here->get_harvest();
        if( ! harvest.is_null() ) {
            return harvest;
        }
    }

    const auto ter_here = ter( pos );
    if( ter_here->has_flag( ter_furn_flag::TFLAG_HARVESTED ) ) {
        return harvest_id::NULL_ID();
    }

    return ter_here->get_harvest();
}

const std::set<std::string> &map::get_harvest_names( const tripoint &pos ) const
{
    static const std::set<std::string> null_harvest_names = {};
    const auto furn_here = furn( pos );
    if( furn_here->can_examine( pos ) ) {
        if( furn_here->has_flag( ter_furn_flag::TFLAG_HARVESTED ) ) {
            return null_harvest_names;
        }

        return furn_here->get_harvest_names();
    }

    const auto ter_here = ter( pos );
    if( ter_here->has_flag( ter_furn_flag::TFLAG_HARVESTED ) ) {
        return null_harvest_names;
    }

    return ter_here->get_harvest_names();
}

/*
 * Get the terrain transforms_into id (what will the terrain transforms into)
 */
ter_id map::get_ter_transforms_into( const tripoint &p ) const
{
    return ter( p ).obj().transforms_into.id();
}

/**
 * Examines the tile pos, with character as the "examinator"
 * Casts Character to player because player/NPC split isn't done yet
 */
void map::examine( Character &you, const tripoint &pos ) const
{
    const furn_t furn_here = furn( pos ).obj();
    if( furn_here.can_examine( pos ) ) {
        furn_here.examine( dynamic_cast<Character &>( you ), pos );
    } else {
        ter( pos ).obj().examine( dynamic_cast<Character &>( you ), pos );
    }
}

bool map::is_harvestable( const tripoint &pos ) const
{
    const harvest_id &harvest_here = get_harvest( pos );
    return !harvest_here.is_null() && !harvest_here->empty();
}

/*
 * set terrain via string; this works for -any- terrain id
 */
bool map::ter_set( const tripoint &p, const ter_id &new_terrain, bool avoid_creatures )
{
    if( !inbounds( p ) ) {
        return false;
    }
    if( avoid_creatures ) {
        Creature *c = get_creature_tracker().creature_at( tripoint_abs_ms( getabs( p ) ), true );
        if( c ) {
            return false;
        }
    }

    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to set terrain at (%d,%d) but the submap is not loaded", l.x, l.y );
        return true;
    }
    const ter_id old_id = current_submap->get_ter( l );
    if( old_id == new_terrain ) {
        // Nothing changed
        return false;
    }

    current_submap->set_ter( l, new_terrain );

    // Set the dirty flags
    const ter_t &old_t = old_id.obj();
    const ter_t &new_t = new_terrain.obj();

    if( current_submap->is_open_air( l ) ) {
        const furn_id &current_furn = current_submap->get_furn( l );
        if( current_furn != f_null &&
            !current_furn->has_flag( ter_furn_flag::TFLAG_ALLOW_ON_OPEN_AIR ) ) {
            // 待定
            /*debugmsg( "Setting terrain %s at %s where furniture is %s.  Terrain is_open_air\n"
                      "If this is intentional, set the ALLOW_ON_OPEN_AIR flag on the furniture",
                      new_terrain.id().str(), p.to_string(), current_furn.id().str() );*/
        }
    }

    // HACK: Hack around ledges in traplocs or else it gets NASTY in z-level mode
    if( old_t.trap != tr_null && old_t.trap != tr_ledge ) {
        auto &traps = traplocs[old_t.trap.to_i()];
        const auto iter = std::find( traps.begin(), traps.end(), p );
        if( iter != traps.end() ) {
            traps.erase( iter );
        }
    }
    if( new_t.trap != tr_null && new_t.trap != tr_ledge ) {
        traplocs[new_t.trap.to_i()].push_back( p );
    }
    if( !new_t.emissions.empty() ) {
        field_ter_locs.push_back( p );
    }
    if( old_t.transparent != new_t.transparent ) {
        set_transparency_cache_dirty( p );
        set_seen_cache_dirty( p );
    }

    if( old_t.has_flag( ter_furn_flag::TFLAG_INDOORS ) != new_t.has_flag(
            ter_furn_flag::TFLAG_INDOORS ) ) {
        set_outside_cache_dirty( p.z );
    }

    if( new_t.has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) != old_t.has_flag(
            ter_furn_flag::TFLAG_NO_FLOOR ) ) {
        set_floor_cache_dirty( p.z );
        // It's a set, not a flag
        support_cache_dirty.insert( p );
        set_seen_cache_dirty( p );
    }

    if( new_t.has_flag( "SPAWN_WITH_LIQUID" ) ) {
        if( new_t.has_flag( "FRESH_WATER" ) ) {
            item water( "water", calendar::start_of_cataclysm );
            // TODO: Move all numeric values to json
            water.charges = rng( 40, 240 );
            if( new_t.has_flag( ter_furn_flag::TFLAG_MURKY ) ) {
                water.poison = rng( 1, 6 );
                water.get_comestible()->parasites = 5;
                water.get_comestible()->contamination = { { disease_bad_food, 5 } };
            }
            add_item( p, water );
        }
    }

    invalidate_max_populated_zlev( p.z );

    set_memory_seen_cache_dirty( p );


    if ((old_t.movecost == 0) != (new_t.movecost == 0)) {
        set_visitable_zones_cache_dirty();
    }
    // TODO: Limit to changes that affect move cost, traps and stairs
    set_pathfinding_cache_dirty( p.z );

    tripoint above( p.xy(), p.z + 1 );
    // Make sure that if we supported something and no longer do so, it falls down
    support_dirty( above );

    return true;
}

bool map::ter_set( const tripoint_bub_ms &p, const ter_id &new_terrain, bool avoid_creatures )
{
    return ter_set( p.raw(), new_terrain, avoid_creatures );
}

std::string map::tername( const tripoint &p ) const
{
    return ter( p ).obj().name();
}

std::string map::features( const tripoint &p ) const
{
    std::string result;
    const auto add = [&]( const std::string & text ) {
        if( !result.empty() ) {
            result += " ";
        }
        result += text;
    };
    const auto add_if = [&]( const bool cond, const std::string & text ) {
        if( cond ) {
            add( text );
        }
    };
    // This is used in an info window that is 46 characters wide, and is expected
    // to take up one line.  So, make sure it does that.
    // FIXME: can't control length of localized text.
    add_if( is_bashable( p ), _( "Smashable." ) );
    add_if( has_flag( ter_furn_flag::TFLAG_DIGGABLE, p ), _( "Diggable." ) );
    add_if( has_flag( ter_furn_flag::TFLAG_PLOWABLE, p ), _( "Plowable." ) );
    add_if( has_flag( ter_furn_flag::TFLAG_ROUGH, p ), _( "Rough." ) );
    add_if( has_flag( ter_furn_flag::TFLAG_UNSTABLE, p ), _( "Unstable." ) );
    add_if( has_flag( ter_furn_flag::TFLAG_SHARP, p ), _( "Sharp." ) );
    add_if( has_flag( ter_furn_flag::TFLAG_FLAT, p ), _( "Flat." ) );
    add_if( has_flag( ter_furn_flag::TFLAG_EASY_DECONSTRUCT, p ), _( "Simple." ) );
    add_if( has_flag( ter_furn_flag::TFLAG_MOUNTABLE, p ), _( "Mountable." ) );
    add_if( has_flag( ter_furn_flag::TFLAG_FLAMMABLE, p ) ||
            has_flag( ter_furn_flag::TFLAG_FLAMMABLE_ASH, p ) ||
            has_flag( ter_furn_flag::TFLAG_FLAMMABLE_HARD, p ), _( "Flammable." ) );
    return result;
}

int map::move_cost_internal( const furn_t &furniture, const ter_t &terrain, const field &field,
                             const vehicle *veh,
                             const int vpart ) const
{   
    if( terrain.movecost == 0 || ( furniture.id && furniture.movecost < 0 ) ||
        field.total_move_cost() < 0 ) {
        return 0;
    }

    if( veh != nullptr ) {
        const vpart_position vp( const_cast<vehicle &>( *veh ), vpart );
        if( vp.obstacle_at_part() ) {
            return 0;
        } else if( vp.part_with_feature( VPFLAG_AISLE, true ) ) {
            return 2;
        } else {
            return 8;
        }
    }
    int movecost = std::max( terrain.movecost + field.total_move_cost(), 0 );

    if( furniture.id ) {
        if( furniture.has_flag( "BRIDGE" ) ) {
            movecost = 2 + std::max( furniture.movecost, 0 );
        } else {
            movecost += std::max( furniture.movecost, 0 );
        }
    }

    return movecost;
}

bool map::is_wall_adjacent( const tripoint &center ) const
{
    for( const tripoint &p : points_in_radius( center, 1 ) ) {
        if( p != center && impassable( p ) ) {
            return true;
        }
    }
    return false;
}

bool map::is_open_air( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return false;
    }
    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        return false;
    }
    return current_submap->is_open_air( l );
}

// Move cost: 3D

int map::move_cost( const tripoint &p, const vehicle *ignored_vehicle ) const
{
    // To save all of the bound checks and submaps fetching, we extract it
    // here instead of using furn(), field_at() and ter().
    if( !inbounds( p ) ) {
        return 0;
    }
    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        return 0;
    }

    const furn_t &furniture = current_submap->get_furn( l ).obj();
    const ter_t &terrain = current_submap->get_ter( l ).obj();
    const field &field = current_submap->get_field( l );
    const optional_vpart_position vp = veh_at( p );
    vehicle *const veh = ( !vp || &vp->vehicle() == ignored_vehicle ) ? nullptr : &vp->vehicle();
    const int part = veh ? vp->part_index() : -1;

    return move_cost_internal( furniture, terrain, field, veh, part );
}

int map::move_cost( const tripoint_bub_ms &p, const vehicle *ignored_vehicle ) const
{
    return move_cost( p.raw(), ignored_vehicle );
}

bool map::impassable( const tripoint &p ) const
{
    return !passable( p );
}

bool map::impassable( const tripoint_bub_ms &p ) const
{
    return impassable( p.raw() );
}

bool map::passable( const tripoint &p ) const
{
    return move_cost( p ) != 0;
}

bool map::passable( const tripoint_bub_ms &p ) const
{
    return passable( p.raw() );
}

int map::move_cost_ter_furn( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return 0;
    }

    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried process terrain at (%d,%d) but the submap is not loaded", l.x, l.y );
        return 0;
    }

    const int tercost = current_submap->get_ter( l ).obj().movecost;
    if( tercost == 0 ) {
        return 0;
    }

    const int furncost = current_submap->get_furn( l ).obj().movecost;
    if( furncost < 0 ) {
        return 0;
    }

    const int cost = tercost + furncost;
    return cost > 0 ? cost : 0;
}

bool map::impassable_ter_furn( const tripoint &p ) const
{
    return !passable_ter_furn( p );
}

bool map::passable_ter_furn( const tripoint &p ) const
{
    return move_cost_ter_furn( p ) != 0;
}

int map::combined_movecost( const tripoint &from, const tripoint &to,
                            const vehicle *ignored_vehicle,
                            const int modifier, const bool flying, const bool via_ramp ) const
{
    static constexpr std::array<int, 4> mults = { 0, 50, 71, 100 };
    const int cost1 = move_cost( from, ignored_vehicle );
    const int cost2 = move_cost( to, ignored_vehicle );
    // Multiply cost depending on the number of differing axes
    // 0 if all axes are equal, 100% if only 1 differs, 141% for 2, 200% for 3
    size_t match = ( from.x != to.x ) + ( from.y != to.y ) + ( from.z != to.z );
    if( flying || from.z == to.z ) {
        return ( cost1 + cost2 + modifier ) * mults[match] / 2;
    }

    // Inter-z-level movement by foot (not flying)
    if( !valid_move( from, to, false, via_ramp ) ) {
        return 0;
    }

    // TODO: Penalize for using stairs
    return ( cost1 + cost2 + modifier ) * mults[match] / 2;
}

bool map::valid_move( const tripoint &from, const tripoint &to,
                      const bool bash, const bool flying, const bool via_ramp ) const
{
    // Used to account for the fact that older versions of GCC can trip on the if statement here.
    cata_assert( to.z > std::numeric_limits<int>::min() );
    // Note: no need to check inbounds here, because maptile_at will do that
    // If oob tile is supplied, the maptile_at will be an unpassable "null" tile
    if( std::abs( from.x - to.x ) > 1 || std::abs( from.y - to.y ) > 1 ||
        std::abs( from.z - to.z ) > 1 ) {
        return false;
    }

    if( from.z == to.z ) {
        // But here we need to, to prevent bashing critters
        return passable( to ) || ( bash && inbounds( to ) );
    } else if( !zlevels ) {
        return false;
    }

    const bool going_up = from.z < to.z;

    const tripoint &up_p = going_up ? to : from;
    const tripoint &down_p = going_up ? from : to;

    const const_maptile up = maptile_at( up_p );
    const ter_t &up_ter = up.get_ter_t();
    if( up_ter.id.is_null() ) {
        return false;
    }
    // Checking for ledge is a workaround for the case when mapgen doesn't
    // actually make a valid ledge drop location with zlevels on, this forces
    // at least one zlevel drop and if down_ter is impassable it's probably
    // inside a wall, we could workaround that further but it's unnecessary.
    const bool up_is_ledge = tr_at( up_p ) == tr_ledge;

    if( up_ter.movecost == 0 ) {
        // Unpassable tile
        return false;
    }

    const const_maptile down = maptile_at( down_p );
    const ter_t &down_ter = down.get_ter_t();
    if( down_ter.id.is_null() ) {
        return false;
    }

    if( !up_is_ledge && down_ter.movecost == 0 ) {
        // Unpassable tile
        return false;
    }

    if( !up_ter.has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) &&
        !up_ter.has_flag( ter_furn_flag::TFLAG_GOES_DOWN ) &&
        !up_is_ledge &&
        !via_ramp ) {
        // Can't move from up to down
        if( std::abs( from.x - to.x ) == 1 || std::abs( from.y - to.y ) == 1 ) {
            // Break the move into two - vertical then horizontal
            tripoint midpoint( down_p.xy(), up_p.z );
            return valid_move( down_p, midpoint, bash, flying, via_ramp ) &&
                   valid_move( midpoint, up_p, bash, flying, via_ramp );
        }
        return false;
    }

    if( !flying && !down_ter.has_flag( ter_furn_flag::TFLAG_GOES_UP ) &&
        !down_ter.has_flag( ter_furn_flag::TFLAG_RAMP ) &&
        !up_is_ledge && !via_ramp ) {
        // Can't safely reach the lower tile
        return false;
    }

    if( bash ) {
        return true;
    }

    int part_up;
    const vehicle *veh_up = veh_at_internal( up_p, part_up );
    if( veh_up != nullptr ) {
        // TODO: Hatches below the vehicle, passable frames
        return false;
    }

    int part_down;
    const vehicle *veh_down = veh_at_internal( down_p, part_down );
    if( veh_down != nullptr && veh_down->roof_at_part( part_down ) >= 0 ) {
        // TODO: OPEN (and only open) hatches from above
        return false;
    }

    // Currently only furniture can block movement if everything else is OK
    // TODO: Vehicles with boards in the given spot
    return up.get_furn_t().movecost >= 0;
}

// End of move cost

double map::ranged_target_size( const tripoint &p ) const
{
    if( impassable( p ) ) {
        return 1.0;
    }

    if( !has_floor( p ) ) {
        return 0.0;
    }

    // TODO: Handle cases like shrubs, trees, furniture, sandbags...
    return 0.1;
}

int map::climb_difficulty( const tripoint &p ) const
{
    if( p.z > OVERMAP_HEIGHT || p.z < -OVERMAP_DEPTH ) {
        debugmsg( "climb_difficulty on out of bounds point: %d, %d, %d", p.x, p.y, p.z );
        return INT_MAX;
    }

    int best_difficulty = INT_MAX;
    int blocks_movement = 0;
    if( has_flag( ter_furn_flag::TFLAG_LADDER, p ) ) {
        // Really easy, but you have to stand on the tile
        return 1;
    } else if( has_flag( ter_furn_flag::TFLAG_RAMP, p ) ||
               has_flag( ter_furn_flag::TFLAG_RAMP_UP, p ) ||
               has_flag( ter_furn_flag::TFLAG_RAMP_DOWN, p ) ) {
        // We're on something stair-like, so halfway there already
        best_difficulty = 7;
    }

    for( const tripoint &pt : points_in_radius( p, 1 ) ) {
        if( impassable_ter_furn( pt ) ) {
            // TODO: Non-hardcoded climbability
            best_difficulty = std::min( best_difficulty, 10 );
            blocks_movement++;
        } else if( veh_at( pt ) ) {
            // Vehicle tiles are quite good for climbing
            // TODO: Penalize spiked parts?
            best_difficulty = std::min( best_difficulty, 7 );
        }

        if( best_difficulty > 5 && has_flag( ter_furn_flag::TFLAG_CLIMBABLE, pt ) ) {
            best_difficulty = 5;
        }
    }

    // TODO: Make this more sensible - check opposite sides, not just movement blocker count
    return std::max( 0, best_difficulty - blocks_movement );
}

bool map::has_floor( const tripoint &p ) const
{
    if( !zlevels || p.z < -OVERMAP_DEPTH + 1 || p.z > OVERMAP_HEIGHT ) {
        return true;
    }

    if( !inbounds( p ) ) {
        return true;
    }

    return !ter( p )->has_flag( ter_furn_flag::TFLAG_NO_FLOOR );
}

bool map::supports_above( const tripoint &p ) const
{
    const const_maptile tile = maptile_at( p );
    const ter_t &ter = tile.get_ter_t();
    if( ter.movecost == 0 ) {
        return true;
    }

    const furn_id frn_id = tile.get_furn();
    if( frn_id != f_null ) {
        const furn_t &frn = frn_id.obj();
        if( frn.movecost < 0 ) {
            return true;
        }
    }

    return veh_at( p ).has_value();
}

bool map::has_floor_or_support( const tripoint &p ) const
{
    const tripoint below( p.xy(), p.z - 1 );
    return !valid_move( p, below, false, true );
}

void map::drop_everything( const tripoint &p )
{
    if( has_floor( p ) ) {
        return;
    }

    drop_furniture( p );
    drop_items( p );
    drop_vehicle( p );
    drop_fields( p );
}

void map::drop_furniture( const tripoint &p )
{
    const furn_id frn = furn( p );
    if( frn == f_null ) {
        return;
    }

    enum support_state {
        SS_NO_SUPPORT = 0,
        SS_BAD_SUPPORT, // TODO: Implement bad, shaky support
        SS_GOOD_SUPPORT,
        SS_FLOOR, // Like good support, but bash floor instead of tile below
        SS_CREATURE
    };

    // Checks if the tile:
    // has floor (supports unconditionally)
    // has support below
    // has unsupporting furniture below (bad support, things should "slide" if possible)
    // has no support and thus allows things to fall through
    const auto check_tile = [this]( const tripoint & pt ) {
        if( has_floor( pt ) ) {
            return SS_FLOOR;
        }

        tripoint below_dest( pt.xy(), pt.z - 1 );
        if( supports_above( below_dest ) ) {
            return SS_GOOD_SUPPORT;
        }

        const furn_id frn_id = furn( below_dest );
        if( frn_id != f_null ) {
            const furn_t &frn = frn_id.obj();
            // Allow crushing tiny/nocollide furniture
            if( !frn.has_flag( ter_furn_flag::TFLAG_TINY ) &&
                !frn.has_flag( ter_furn_flag::TFLAG_NOCOLLIDE ) ) {
                return SS_BAD_SUPPORT;
            }
        }

        if( get_creature_tracker().creature_at( below_dest ) != nullptr ) {
            // Smash a critter
            return SS_CREATURE;
        }

        return SS_NO_SUPPORT;
    };

    tripoint current( p.xy(), p.z + 1 );
    support_state last_state = SS_NO_SUPPORT;
    while( last_state == SS_NO_SUPPORT ) {
        current.z--;
        // Check current tile
        last_state = check_tile( current );
    }

    if( current == p ) {
        // Nothing happened
        if( last_state != SS_FLOOR ) {
            support_dirty( current );
        }

        return;
    }

    furn_set( p, f_null );
    furn_set( current, frn );

    // If it's sealed, we need to drop items with it
    const furn_t &frn_obj = frn.obj();
    if( frn_obj.has_flag( ter_furn_flag::TFLAG_SEALED ) && has_items( p ) ) {
        map_stack old_items = i_at( p );
        map_stack new_items = i_at( current );
        for( const item &it : old_items ) {
            new_items.insert( it );
        }

        i_clear( p );
    }

    // Approximate weight/"bulkiness" based on strength to drag
    int weight;
    if( frn_obj.has_flag( ter_furn_flag::TFLAG_TINY ) ||
        frn_obj.has_flag( ter_furn_flag::TFLAG_NOCOLLIDE ) ) {
        weight = 5;
    } else {
        weight = frn_obj.is_movable() ? frn_obj.move_str_req : 20;
    }

    if( frn_obj.has_flag( ter_furn_flag::TFLAG_ROUGH ) ||
        frn_obj.has_flag( ter_furn_flag::TFLAG_SHARP ) ) {
        weight += 5;
    }

    // TODO: Balance this.
    int dmg = weight * ( p.z - current.z );

    if( last_state == SS_FLOOR ) {
        // Bash the same tile twice - once for furniture, once for the floor
        bash( current, dmg, false, false, true );
        bash( current, dmg, false, false, true );
    } else if( last_state == SS_BAD_SUPPORT || last_state == SS_GOOD_SUPPORT ) {
        bash( current, dmg, false, false, false );
        tripoint below( current.xy(), current.z - 1 );
        bash( below, dmg, false, false, false );
    } else if( last_state == SS_CREATURE ) {
        const std::string &furn_name = frn_obj.name();
        bash( current, dmg, false, false, false );
        tripoint below( current.xy(), current.z - 1 );
        Creature *critter = get_creature_tracker().creature_at( below );
        if( critter == nullptr ) {
            debugmsg( "drop_furniture couldn't find creature at %d,%d,%d",
                      below.x, below.y, below.z );
            return;
        }

        critter->add_msg_player_or_npc( m_bad, _( "Falling %s hits you!" ),
                                        _( "Falling %s hits <npcname>" ),
                                        furn_name );
        // TODO: A chance to dodge/uncanny dodge
        Character *pl = critter->as_character();
        monster *mon = critter->as_monster();
        if( pl != nullptr ) {
            pl->deal_damage( nullptr, bodypart_id( "torso" ), damage_instance( damage_type::BASH, rng( dmg / 3,
                             dmg ), 0,
                             0.5f ) );
            pl->deal_damage( nullptr, bodypart_id( "head" ),  damage_instance( damage_type::BASH, rng( dmg / 3,
                             dmg ), 0,
                             0.5f ) );
            pl->deal_damage( nullptr, bodypart_id( "leg_l" ), damage_instance( damage_type::BASH, rng( dmg / 2,
                             dmg ), 0,
                             0.4f ) );
            pl->deal_damage( nullptr, bodypart_id( "leg_r" ), damage_instance( damage_type::BASH, rng( dmg / 2,
                             dmg ), 0,
                             0.4f ) );
            pl->deal_damage( nullptr, bodypart_id( "arm_l" ), damage_instance( damage_type::BASH, rng( dmg / 2,
                             dmg ), 0,
                             0.4f ) );
            pl->deal_damage( nullptr, bodypart_id( "arm_r" ), damage_instance( damage_type::BASH, rng( dmg / 2,
                             dmg ), 0,
                             0.4f ) );
        } else if( mon != nullptr ) {
            // TODO: Monster's armor and size - don't crush hulks with chairs
            mon->apply_damage( nullptr, bodypart_id( "torso" ), rng( dmg, dmg * 2 ) );
        }
    }

    // Re-queue for another check, in case bash destroyed something
    support_dirty( current );
}

void map::drop_items( const tripoint &p )
{
    if( !has_items( p ) ) {
        return;
    }

    map_stack items = i_at( p );
    // TODO: Make items check the volume tile below can accept
    // rather than disappearing if it would be overloaded

    tripoint below( p );
    int height_fallen = 0;

    while( !has_floor( below ) ) {
        below.z--;
        height_fallen++;
    }

    if( below == p ) {
        return;
    }

    float damage_total = 0.0f;
    for (item& i : items) {
        units::mass wt_dropped = i.weight();
        float item_density = i.get_base_material().density();
        float damage = 5 * to_kilogram(wt_dropped) * height_fallen * item_density;
        damage_total += damage;

        // 如果掉落的地点有运输器，那么物品会直接掉落到运输器上，没有，直接掉落到地面上
        const optional_vpart_position &vp = veh_at(below);
        if (vp && vp->vehicle().part(0).info().has_flag("CONVEYOR_BELT")) {
            vp->vehicle().add_item_new(0,i);
        }
        else {
            add_item_or_charges(below, i);       
        }



        // Bash creature standing below
        Creature* creature_below = get_creature_tracker().creature_at(below);
        if (creature_below) {
            // creature's dodge modifier
            float dodge_mod = creature_below->dodge_roll();
            // if item dropped by character their throwing skill modifier -1 if not dropped by a character
            float throwing_mod = i.dropped_char_stats.throwing == -1.0f ? 0.0f : 5 *
                i.dropped_char_stats.throwing;

            // values calibrated so that %hit chance starts from 60% going up and down according to the two modifiers
            float hit_mod = (throwing_mod + 18) / (dodge_mod + 15);

            int creature_hit_chance = rng(0, 100);
            creature_hit_chance /= hit_mod * occupied_tile_fraction(creature_below->get_size());

            if (creature_hit_chance < 15) {
                add_msg(_("落下的 %s 击中了 %s 的头部！"), i.tname(), creature_below->get_name());
                creature_below->deal_damage(nullptr, bodypart_id("head"), damage_instance(damage_type::BASH,
                    damage));
            }
            else if (creature_hit_chance < 30) {
                add_msg(_("落下的 %s 击中了 %s 的躯干！"), i.tname(), creature_below->get_name());
                creature_below->deal_damage(nullptr, bodypart_id("torso"), damage_instance(damage_type::BASH,
                    damage));
            }
            else if (creature_hit_chance < 65) {
                add_msg(_("落下的 %s 击中了 %s 的左臂！"), i.tname(), creature_below->get_name());
                creature_below->deal_damage(nullptr, bodypart_id("arm_l"), damage_instance(damage_type::BASH,
                    damage));
            }
            else if (creature_hit_chance < 100) {
                add_msg(_("落下的 %s 击中了 %s 的右臂！"), i.tname(), creature_below->get_name());
                creature_below->deal_damage(nullptr, bodypart_id("arm_r"), damage_instance(damage_type::BASH,
                    damage));
            }
            else {
                add_msg(_("落下的 %s 未命中 %s ！"), i.tname(), creature_below->get_name());
            }
        }
        
        // Bash items at bottom since currently bash_items only bash glass items      
        int chance = static_cast<int>(200 * i.bash_resist(true) / damage + 1);
        if (one_in(chance)) {
            i.inc_damage();
        }
    }

    // Bash terain, furniture and vehicles on tile below
    bash(below, damage_total / 2);
    i_clear( p );
}

void map::drop_vehicle( const tripoint &p )
{
    const optional_vpart_position vp = veh_at( p );
    if( !vp ) {
        return;
    }
    vp->vehicle().is_falling = true;
    set_seen_cache_dirty( p );
}

void map::drop_fields( const tripoint &p )
{
    field &fld = field_at( p );
    if( fld.field_count() == 0 ) {
        return;
    }

    const tripoint below = p + tripoint_below;
    for( const auto &iter : fld ) {
        const field_entry &entry = iter.second;
        // For now only drop cosmetic fields, which don't warrant per-turn check
        // Active fields "drop themselves"
        if( entry.get_field_type()->accelerated_decay ) {
            add_field( below, entry.get_field_type(), entry.get_field_intensity(), entry.get_field_age() );
            remove_field( p, entry.get_field_type() );
        }
    }
}

void map::support_dirty( const tripoint &p )
{
    if( zlevels ) {
        support_cache_dirty.insert( p );
    }
}

void map::process_falling()
{
    if( !zlevels ) {
        support_cache_dirty.clear();
        return;
    }

    if( !support_cache_dirty.empty() ) {
        add_msg_debug( debugmode::DF_MAP, "Checking %d tiles for falling objects",
                       support_cache_dirty.size() );
        // We want the cache to stay constant, but falling can change it
        std::set<tripoint> last_cache = std::move( support_cache_dirty );
        support_cache_dirty.clear();
        for( const tripoint &p : last_cache ) {
            drop_everything( p );
        }
    }
}

bool map::has_flag( const std::string &flag, const tripoint &p ) const
{
    return has_flag_ter_or_furn( flag, p ); // Does bound checking
}

bool map::can_put_items( const tripoint &p ) const
{
    if( can_put_items_ter_furn( p ) ) {
        return true;
    }
    const optional_vpart_position vp = veh_at( p );
    return static_cast<bool>( vp.part_with_feature( "CARGO", true ) );
}

bool map::can_put_items( const tripoint_bub_ms &p ) const
{
    return can_put_items( p.raw() );
}

bool map::can_put_items_ter_furn( const tripoint &p ) const
{
    return !has_flag( ter_furn_flag::TFLAG_NOITEM, p ) && !has_flag( ter_furn_flag::TFLAG_SEALED, p );
}

bool map::can_put_items_ter_furn( const tripoint_bub_ms &p ) const
{
    return can_put_items_ter_furn( p.raw() );
}

bool map::has_flag_ter( const std::string &flag, const tripoint &p ) const
{
    return ter( p ).obj().has_flag( flag );
}

bool map::has_flag_furn( const std::string &flag, const tripoint &p ) const
{
    return furn( p ).obj().has_flag( flag );
}

bool map::has_flag_ter_or_furn( const std::string &flag, const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return false;
    }

    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to process terrain at (%d,%d) but the submap is not loaded", l.x, l.y );
        return false;
    }

    return current_submap->get_ter( l ).obj().has_flag( flag ) ||
           current_submap->get_furn( l ).obj().has_flag( flag );
}

bool map::has_flag( const ter_furn_flag flag, const tripoint &p ) const
{
    return has_flag_ter_or_furn( flag, p ); // Does bound checking
}

bool map::has_flag( const ter_furn_flag flag, const tripoint_bub_ms &p ) const
{
    return has_flag( flag, p.raw() );
}

bool map::has_flag_ter( const ter_furn_flag flag, const tripoint &p ) const
{
    return ter( p ).obj().has_flag( flag );
}

bool map::has_flag_furn( const ter_furn_flag flag, const tripoint &p ) const
{
    return furn( p ).obj().has_flag( flag );
}

bool map::has_flag_furn( const ter_furn_flag flag, const tripoint_bub_ms &p ) const
{
    return furn( p ).obj().has_flag( flag );
}

bool map::has_flag_ter_or_furn( const ter_furn_flag flag, const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return false;
    }

    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to process terrain at (%d,%d) but the submap is not loaded", l.x, l.y );
        return false;
    }

    return current_submap->get_ter( l ).obj().has_flag( flag ) ||
           current_submap->get_furn( l ).obj().has_flag( flag );
}

bool map::has_flag_ter_or_furn( const ter_furn_flag flag, const tripoint_bub_ms &p ) const
{
    return has_flag_ter_or_furn( flag, p.raw() );
}

// End of 3D flags

// Bashable - common function

int map::bash_rating_internal( const int str, const furn_t &furniture,
                               const ter_t &terrain, const bool allow_floor,
                               const vehicle *veh, const int part ) const
{
    bool furn_smash = false;
    bool ter_smash = false;
    ///\EFFECT_STR determines what furniture can be smashed
    if( furniture.id && furniture.bash.str_max != -1 ) {
        furn_smash = true;
        ///\EFFECT_STR determines what terrain can be smashed
    } else if( terrain.bash.str_max != -1 && ( !terrain.bash.bash_below || allow_floor ) ) {
        ter_smash = true;
    }

    if( veh != nullptr && vpart_position( const_cast<vehicle &>( *veh ), part ).obstacle_at_part() ) {
        // Monsters only care about rating > 0, NPCs should want to path around cars instead
        return 2; // Should probably be a function of part hp (+armor on tile)
    }

    int bash_min = 0;
    int bash_max = 0;
    if( furn_smash ) {
        bash_min = furniture.bash.str_min;
        bash_max = furniture.bash.str_max;
    } else if( ter_smash ) {
        bash_min = terrain.bash.str_min;
        bash_max = terrain.bash.str_max;
    } else {
        return -1;
    }

    ///\EFFECT_STR increases smashing damage
    if( str < bash_min ) {
        return 0;
    } else if( str >= bash_max ) {
        return 10;
    }

    int ret = ( 10 * ( str - bash_min ) ) / ( bash_max - bash_min );
    // Round up to 1, so that desperate NPCs can try to bash down walls
    return std::max( ret, 1 );
}

// 3D bashable

bool map::is_bashable( const tripoint &p, const bool allow_floor ) const
{
    if( !inbounds( p ) ) {
        DebugLog( D_WARNING, D_MAP ) << "Looking for out-of-bounds is_bashable at "
                                     << p.x << ", " << p.y << ", " << p.z;
        return false;
    }

    if( veh_at( p ).obstacle_at_part() ) {
        return true;
    }

    if( has_furn( p ) && furn( p ).obj().bash.str_max != -1 ) {
        return true;
    }

    const map_bash_info &ter_bash = ter( p ).obj().bash;
    return ter_bash.str_max != -1 && ( !ter_bash.bash_below || allow_floor );
}

bool map::is_bashable_ter( const tripoint &p, const bool allow_floor ) const
{
    const map_bash_info &ter_bash = ter( p ).obj().bash;
    return ter_bash.str_max != -1 && ( !ter_bash.bash_below || allow_floor );
}

bool map::is_bashable_furn( const tripoint &p ) const
{
    return has_furn( p ) && furn( p ).obj().bash.str_max != -1;
}

bool map::is_bashable_ter_furn( const tripoint &p, const bool allow_floor ) const
{
    return is_bashable_furn( p ) || is_bashable_ter( p, allow_floor );
}

int map::bash_strength( const tripoint &p, const bool allow_floor ) const
{
    if( has_furn( p ) && furn( p ).obj().bash.str_max != -1 ) {
        return furn( p ).obj().bash.str_max;
    }

    const map_bash_info &ter_bash = ter( p ).obj().bash;
    if( ter_bash.str_max != -1 && ( !ter_bash.bash_below || allow_floor ) ) {
        return ter_bash.str_max;
    }

    return -1;
}

int map::bash_resistance( const tripoint &p, const bool allow_floor ) const
{
    if( has_furn( p ) && furn( p ).obj().bash.str_min != -1 ) {
        return furn( p ).obj().bash.str_min;
    }

    const map_bash_info &ter_bash = ter( p ).obj().bash;
    if( ter_bash.str_min != -1 && ( !ter_bash.bash_below || allow_floor ) ) {
        return ter_bash.str_min;
    }

    return -1;
}

int map::bash_rating( const int str, const tripoint &p, const bool allow_floor ) const
{
    if( !inbounds( p ) ) {
        DebugLog( D_WARNING, D_MAP ) << "Looking for out-of-bounds is_bashable at "
                                     << p.x << ", " << p.y << ", " << p.z;
        return -1;
    }

    if( str <= 0 ) {
        return -1;
    }

    const furn_t &furniture = furn( p ).obj();
    const ter_t &terrain = ter( p ).obj();
    const optional_vpart_position vp = veh_at( p );
    vehicle *const veh = vp ? &vp->vehicle() : nullptr;
    const int part = vp ? vp->part_index() : -1;
    return bash_rating_internal( str, furniture, terrain, allow_floor, veh, part );
}

// End of 3D bashable

void map::make_rubble( const tripoint &p, const furn_id &rubble_type, const bool items,
                       const ter_id &floor_type, bool overwrite )
{
    if( overwrite ) {
        ter_set( p, floor_type );
        furn_set( p, rubble_type );
    } else {
        // First see if there is existing furniture to destroy
        if( is_bashable_furn( p ) ) {
            destroy_furn( p, true );
        }
        // Leave the terrain alone unless it interferes with furniture placement
        if( impassable( p ) && is_bashable_ter( p ) ) {
            destroy( p, true );
        }
        // Check again for new terrain after potential destruction
        if( impassable( p ) ) {
            ter_set( p, floor_type );
        }

        if( !is_open_air( p ) ) {
            furn_set( p, rubble_type );
        }
    }

    if( !items || is_open_air( p ) ) {
        return;
    }

    //Still hardcoded, but a step up from the old stuff due to being in only one place
    if( rubble_type == f_wreckage ) {
        item chunk( "steel_chunk", calendar::turn );
        item scrap( "scrap", calendar::turn );
        add_item_or_charges( p, chunk );
        add_item_or_charges( p, scrap );
        if( one_in( 5 ) ) {
            item pipe( "pipe", calendar::turn );
            item wire( "wire", calendar::turn );
            add_item_or_charges( p, pipe );
            add_item_or_charges( p, wire );
        }
    } else if( rubble_type == f_rubble_rock ) {
        item rock( "rock", calendar::turn );
        int rock_count = rng( 1, 3 );
        for( int i = 0; i < rock_count; i++ ) {
            add_item_or_charges( p, rock );
        }
    } else if( rubble_type == f_rubble ) {
        item splinter( "splinter", calendar::turn );
        int splinter_count = rng( 2, 8 );
        for( int i = 0; i < splinter_count; i++ ) {
            add_item_or_charges( p, splinter );
        }
        spawn_item( p, itype_nail, 1, rng( 20, 50 ) );
    }
}

bool map::is_water_shallow_current( const tripoint &p ) const
{
    return has_flag( ter_furn_flag::TFLAG_CURRENT, p ) &&
           !has_flag( ter_furn_flag::TFLAG_DEEP_WATER, p );
}

bool map::is_divable( const tripoint &p ) const
{
    return has_flag( ter_furn_flag::TFLAG_SWIMMABLE, p ) &&
           has_flag( ter_furn_flag::TFLAG_DEEP_WATER, p );
}

bool map::is_outside( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return true;
    }

    const auto &outside_cache = get_cache_ref( p.z ).outside_cache;
    return outside_cache[p.x][p.y];
}

bool map::is_last_ter_wall( const bool no_furn, const point &p,
                            const point &max, const direction dir ) const
{
    point mov;
    switch( dir ) {
        case direction::NORTH:
            mov.y = -1;
            break;
        case direction::SOUTH:
            mov.y = 1;
            break;
        case direction::WEST:
            mov.x = -1;
            break;
        case direction::EAST:
            mov.x = 1;
            break;
        default:
            break;
    }
    point p2( p );
    bool result = true;
    bool loop = true;
    while( loop && ( ( dir == direction::NORTH && p2.y >= 0 ) ||
                     ( dir == direction::SOUTH && p2.y < max.y ) ||
                     ( dir == direction::WEST  && p2.x >= 0 ) ||
                     ( dir == direction::EAST  && p2.x < max.x ) ) ) {
        if( no_furn && has_furn( p2 ) ) {
            loop = false;
            result = false;
        } else if( !has_flag_ter( ter_furn_flag::TFLAG_FLAT, p2 ) ) {
            loop = false;
            if( !has_flag_ter( ter_furn_flag::TFLAG_WALL, p2 ) ) {
                result = false;
            }
        }
        p2.x += mov.x;
        p2.y += mov.y;
    }
    return result;
}

bool map::tinder_at( const tripoint &p )
{
    for( const item &i : i_at( p ) ) {
        if( i.has_flag( flag_TINDER ) ) {
            return true;
        }
    }
    return false;
}

bool map::flammable_items_at( const tripoint &p, int threshold )
{
    if( !has_items( p ) ||
        ( has_flag( ter_furn_flag::TFLAG_SEALED, p ) &&
          !has_flag( ter_furn_flag::TFLAG_ALLOW_FIELD_EFFECT, p ) ) ) {
        // Sealed containers don't allow fire, so shouldn't allow setting the fire either
        return false;
    }

    for( const item &i : i_at( p ) ) {
        if( i.flammable( threshold ) ) {
            return true;
        }
    }

    return false;
}

bool map::flammable_items_at( const tripoint_bub_ms &p, int threshold )
{
    return flammable_items_at( p.raw(), threshold );
}

bool map::is_flammable( const tripoint &p )
{

    if( has_flag( ter_furn_flag::TFLAG_FLAMMABLE, p ) ) {
        return true;
    }

    if( has_flag( ter_furn_flag::TFLAG_FLAMMABLE_ASH, p ) ) {
        return true;
    }

    if( get_field_intensity( p, fd_web ) > 0 ) {
        return true;
    }

    if( flammable_items_at( p ) ) {
        return true;
    }

    return false;
}

bool map::is_flammable( const tripoint_bub_ms &p )
{
    return is_flammable( p.raw() );
}

void map::decay_fields_and_scent( const time_duration &amount )
{
    // TODO: Make this happen on all z-levels

    // Decay scent separately, so that later we can use field count to skip empty submaps
    get_scent().decay();

    // Coordinate code copied from lightmap calculations
    // TODO: Z
    const int smz = abs_sub.z();
    const auto &outside_cache = get_cache_ref( smz ).outside_cache;
    for( int smx = 0; smx < my_MAPSIZE; ++smx ) {
        for( int smy = 0; smy < my_MAPSIZE; ++smy ) {
            submap *cur_submap = get_submap_at_grid( { smx, smy, smz } );
            if( cur_submap == nullptr ) {
                debugmsg( "Tried to process field at (%d,%d,%d) but the submap is not loaded", smx, smy, smz );
                continue;
            }
            int to_proc = cur_submap->field_count;
            if( to_proc < 1 ) {
                if( to_proc < 0 ) {
                    cur_submap->field_count = 0;
                    dbg( D_ERROR ) << "map::decay_fields_and_scent: submap at "
                                   << ( abs_sub + point( smx, smy ) ).to_string()
                                   << "has " << to_proc << " field_count";
                }
                get_cache( smz ).field_cache.reset( smx + ( smy * MAPSIZE ) );
                // This submap has no fields
                continue;
            }

            for( int sx = 0; sx < SEEX; ++sx ) {
                if( to_proc < 1 ) {
                    // This submap had some fields, but all got proc'd already
                    break;
                }

                for( int sy = 0; sy < SEEY; ++sy ) {
                    const point p( sx + smx * SEEX, sy + smy * SEEY );

                    const field &fields = cur_submap->get_field( { sx, sy} );
                    if( !outside_cache[p.x][p.y] ) {
                        to_proc -= fields.field_count();
                        continue;
                    }

                    for( const auto &fp : fields ) {
                        to_proc--;
                        field_entry cur = fp.second;
                        const field_type_id type = cur.get_field_type();
                        const int decay_amount_factor =  type.obj().decay_amount_factor;
                        if( decay_amount_factor != 0 ) {
                            const time_duration decay_amount = amount / decay_amount_factor;
                            cur.set_field_age( cur.get_field_age() + decay_amount );
                        }
                    }
                }
            }

            if( to_proc > 0 ) {
                cur_submap->field_count = cur_submap->field_count - to_proc;
                dbg( D_ERROR ) << "map::decay_fields_and_scent: submap at "
                               << abs_sub.to_string()
                               << "has " << cur_submap->field_count - to_proc << "fields, but "
                               << cur_submap->field_count << " field_count";
            }
        }
    }
}

point map::random_outdoor_tile() const
{
    std::vector<point> options;
    for( const tripoint &p : points_on_zlevel() ) {
        if( is_outside( p.xy() ) ) {
            options.push_back( p.xy() );
        }
    }
    return random_entry( options, point_north_west );
}

bool map::has_adjacent_furniture_with( const tripoint &p,
                                       const std::function<bool( const furn_t & )> &filter ) const
{
    for( const tripoint &adj : points_in_radius( p, 1 ) ) {
        if( has_furn( adj ) && filter( furn( adj ).obj() ) ) {
            return true;
        }
    }

    return false;
}

bool map::has_nearby_fire( const tripoint &p, int radius ) const
{
    for( const tripoint &pt : points_in_radius( p, radius ) ) {
        if( has_field_at( pt, fd_fire ) ) {
            return true;
        }
        if( has_flag_ter_or_furn( ter_furn_flag::TFLAG_USABLE_FIRE, p ) ) {
            return true;
        }
    }
    return false;
}

bool map::has_nearby_table( const tripoint_bub_ms &p, int radius ) const
{
    for( const tripoint_bub_ms &pt : points_in_radius( p, radius ) ) {
        if( has_flag( ter_furn_flag::TFLAG_FLAT_SURF, pt ) ) {
            return true;
        }
        const optional_vpart_position vp = veh_at( pt );
        if( vp && vp->part_with_feature( "FLAT_SURF", true ) ) {
            return true;
        }
    }
    return false;
}

bool map::has_nearby_chair( const tripoint &p, int radius ) const
{
    for( const tripoint &pt : points_in_radius( p, radius ) ) {
        const optional_vpart_position vp = veh_at( pt );
        if( has_flag( ter_furn_flag::TFLAG_CAN_SIT, pt ) ) {
            return true;
        }
        if( vp && vp->vehicle().has_part( "SEAT" ) ) {
            return true;
        }
    }
    return false;
}

bool map::has_nearby_ter( const tripoint &p, const ter_id &type, int radius ) const
{
    for( const tripoint &pt : points_in_radius( p, radius ) ) {
        if( ter( pt ) == type ) {
            return true;
        }
    }
    return false;
}

bool map::terrain_moppable( const tripoint_bub_ms &p )
{
    // Moppable items ( spills )
    if( !has_flag( ter_furn_flag::TFLAG_LIQUIDCONT, p ) ) {
        map_stack items = i_at( p );
        auto found = std::find_if( items.begin(), items.end(), []( const item & it ) {
            return it.made_of( phase_id::LIQUID );
        } );

        if( found != items.end() ) {
            return true;
        }
    }

    // Moppable fields ( blood )
    for( const std::pair<const field_type_id, field_entry> &pr : field_at( p ) ) {
        if( pr.second.get_field_type().obj().phase == phase_id::LIQUID ) {
            return true;
        }
    }

    // Moppable vehicles ( blood splatter )
    if( const optional_vpart_position vp = veh_at( p ) ) {
        vehicle *const veh = &vp->vehicle();
        std::vector<int> parts_here = veh->parts_at_relative( vp->mount(), true );
        for( int elem : parts_here ) {
            if( veh->part( elem ).blood > 0 ) {
                return true;
            }

            vehicle_stack items = veh->get_items( elem );
            auto found = std::find_if( items.begin(), items.end(), []( const item & it ) {
                return it.made_of( phase_id::LIQUID );
            } );

            if( found != items.end() ) {
                return true;
            }
        }
    }

    return false;
}

bool map::mop_spills( const tripoint_bub_ms &p )
{
    bool retval = false;

    if( !has_flag( ter_furn_flag::TFLAG_LIQUIDCONT, p ) ) {
        map_stack items = i_at( p );
        auto new_end = std::remove_if( items.begin(), items.end(), []( const item & it ) {
            return it.made_of( phase_id::LIQUID );
        } );
        retval = new_end != items.end();
        while( new_end != items.end() ) {
            new_end = items.erase( new_end );
        }
    }

    field &fld = field_at( p );
    for( const auto &it : fld ) {
        if( it.first->phase == phase_id::LIQUID ) {
            remove_field( p, it.first );
            retval = true;
        }
    }

    if( const optional_vpart_position vp = veh_at( p ) ) {
        vehicle *const veh = &vp->vehicle();
        std::vector<int> parts_here = veh->parts_at_relative( vp->mount(), true );
        for( int &elem : parts_here ) {
            if( veh->part( elem ).blood > 0 ) {
                veh->part( elem ).blood = 0;
                retval = true;
            }
            //remove any liquids that somehow didn't fall through to the ground
            vehicle_stack here = veh->get_items( elem );
            auto new_end = std::remove_if( here.begin(), here.end(), []( const item & it ) {
                return it.made_of( phase_id::LIQUID );
            } );
            retval |= ( new_end != here.end() );
            while( new_end != here.end() ) {
                new_end = here.erase( new_end );
            }
        }
    } // if veh != 0
    return retval;
}

int map::collapse_check( const tripoint &p ) const
{
    const bool collapses = has_flag( ter_furn_flag::TFLAG_COLLAPSES, p );
    const bool supports_roof = has_flag( ter_furn_flag::TFLAG_SUPPORTS_ROOF, p );

    int num_supports = p.z == OVERMAP_DEPTH ? 0 : -5;
    // if there's support below, things are less likely to collapse
    if( p.z > -OVERMAP_DEPTH ) {
        const tripoint &pbelow = tripoint( p.xy(), p.z - 1 );
        for( const tripoint &tbelow : points_in_radius( pbelow, 1 ) ) {
            if( has_flag( ter_furn_flag::TFLAG_SUPPORTS_ROOF, tbelow ) ) {
                num_supports += 1;
                if( has_flag( ter_furn_flag::TFLAG_WALL, tbelow ) ) {
                    num_supports += 2;
                }
                if( tbelow == pbelow ) {
                    num_supports += 2;
                }
                if (has_flag(ter_furn_flag::TFLAG_SINGLE_SUPPORT, p)) {
                    num_supports = 0;
                }
            }
        }
    }

    for( const tripoint &t : points_in_radius( p, 1 ) ) {
        if( p == t ) {
            continue;
        }

        if( collapses ) {
            if( has_flag( ter_furn_flag::TFLAG_COLLAPSES, t ) ) {
                num_supports++;
            } else if( has_flag( ter_furn_flag::TFLAG_SUPPORTS_ROOF, t ) ) {
                num_supports += 2;
            }
        } else if( supports_roof ) {
            if( has_flag( ter_furn_flag::TFLAG_SUPPORTS_ROOF, t ) ) {
                if( has_flag( ter_furn_flag::TFLAG_WALL, t ) ) {
                    num_supports += 4;
                } else if( !has_flag( ter_furn_flag::TFLAG_COLLAPSES, t ) ) {
                    num_supports += 3;
                }
            }
        }
        if (has_flag(ter_furn_flag::TFLAG_SINGLE_SUPPORT, p)) {
            num_supports = 0;
        }
    }

    return 1.7 * num_supports;
}

// there is still some odd behavior here and there and you can get floating chunks of
// unsupported floor, but this is much better than it used to be
void map::collapse_at( const tripoint &p, const bool silent, const bool was_supporting,
                       const bool destroy_pos )
{
    const bool supports = was_supporting || has_flag( ter_furn_flag::TFLAG_SUPPORTS_ROOF, p );
    const bool wall = was_supporting || has_flag( ter_furn_flag::TFLAG_WALL, p );
    // don't bash again if the caller already bashed here
    if( destroy_pos ) {
        destroy( p, silent );
        crush( p );
        make_rubble( p );
    }
    const bool still_supports = has_flag( ter_furn_flag::TFLAG_SUPPORTS_ROOF, p );

    // If something supporting the roof collapsed, see what else collapses
    if( supports && !still_supports ) {
        for( const tripoint &t : points_in_radius( p, 1 ) ) {
            // If z-levels are off, tz == t, so we end up skipping a lot of stuff to avoid bugs.
            const tripoint &tz = tripoint( t.xy(), t.z + 1 );
            // if nothing above us had the chance of collapsing, move on
            if( !one_in( collapse_check( tz ) ) ) {
                continue;
            }
            // if a wall collapses, walls without support from below risk collapsing and
            //propagate the collapse upwards
            if( zlevels && wall && p == t && has_flag( ter_furn_flag::TFLAG_WALL, tz ) ) {
                collapse_at( tz, silent );
            }
            // floors without support from below risk collapsing into open air and can propagate
            // the collapse horizontally but not vertically
            if( p != t && ( has_flag( ter_furn_flag::TFLAG_SUPPORTS_ROOF, t ) &&
                            has_flag( ter_furn_flag::TFLAG_COLLAPSES, t ) ) ) {
                collapse_at( t, silent );
            }
            // this tile used to support a roof, now it doesn't, which means there is only
            // open air above us
            if( zlevels ) {
                // ensure that the layer below this one is not a wall, otherwise you have a ledge dropping onto
                // a wall which doesn't make sense.
                if( !has_flag( ter_furn_flag::TFLAG_WALL, t ) ) {
                    furn_set( tz, f_null );
                    ter_set( tz, t_open_air );
                    Creature *critter = get_creature_tracker().creature_at( tz );
                    if( critter != nullptr ) {
                        creature_on_trap( *critter );
                    }
                }
            }
        }
    }
    // it would be great to check if collapsing ceilings smashed through the floor, but
    // that's not handled for now
}

void map::bash_items_new(const tripoint& p, bash_params& params)
{
    if (!has_items(p) || has_flag_ter_or_furn(ter_furn_flag::TFLAG_PLANT, p)) {
        return;
    }

    std::vector<item> smashed_contents;
    map_stack bashed_items = i_at(p);
    bool smashed_glass = false;
    for (auto bashed_item = bashed_items.begin(); bashed_item != bashed_items.end(); ) {
        // the check for active suppresses Molotovs smashing themselves with their own explosion
        int glass_portion = bashed_item->made_of(material_glass);
        float glass_fraction = glass_portion / static_cast<float>(bashed_item->type->mat_portion_total);
        if (glass_portion && !bashed_item->active && rng_float(0.0f, 1.0f) < glass_fraction * 0.5f) {
            params.did_bash = true;
            smashed_glass = true;
            for (const item* bashed_content : bashed_item->all_items_top()) {
                smashed_contents.emplace_back(*bashed_content);
            }
            bashed_item = bashed_items.erase(bashed_item);
        }
        else {
            ++bashed_item;
        }
    }
    // Now plunk in the contents of the smashed items.
    spawn_items(p, smashed_contents);

    // Add a glass sound even when something else also breaks
    if (smashed_glass && !params.silent) {
        sounds::sound(p, 12, sounds::sound_t::combat, _("glass shattering."), false,
            "smash_success", "smash_glass_contents");
    }
}

void map::bash_vehicle_new(const tripoint& p, bash_params& params)
{
    // Smash vehicle if present
    if (const optional_vpart_position vp = veh_at(p)) {
        vp->vehicle().damage(*this, vp->part_index(), params.strength, damage_type::BASH);
        if (!params.silent) {
            sounds::sound(p, 18, sounds::sound_t::combat, _("crash!"), false,
                "smash_success", "hit_vehicle");
        }

        params.did_bash = true;
        params.success = true;
        params.bashed_solid = true;
    }
}

void map::bash_field_new(const tripoint& p, bash_params& params)
{
    std::vector<field_type_id> to_remove;
    for (const std::pair<const field_type_id, field_entry>& fd : field_at(p)) {
        if (fd.first->bash_info.str_min > -1) {
            params.did_bash = true;
            params.bashed_solid = true; // To prevent bashing furniture/vehicles
            to_remove.push_back(fd.first);
        }
    }
    for (field_type_id fd : to_remove) {
        remove_field(p, fd);
    }
}


void map::smash_trap(const tripoint& p, int power, const std::string& cause_message)
{
    const trap& tr = get_map().tr_at(p);
    if (tr.is_null()) {
        return;
    }

    const bool is_explosive_trap = !tr.is_benign() && tr.vehicle_data.do_explosion;

    if ( !(power>=30 ) || !is_explosive_trap) {
        return;
    }
    // make a fake NPC to trigger the trap
    npc dummy;
    dummy.set_fake(true);
    dummy.name = cause_message;
    dummy.setpos(p);
    tr.trigger(p ,dummy);
}





void map::smash_items( const tripoint &p, const int power, const std::string &cause_message )
{
    if( !has_items( p ) || has_flag_ter_or_furn( ter_furn_flag::TFLAG_PLANT, p ) ) {
        return;
    }

    // Keep track of how many items have been damaged, and what the first one is
    bool item_was_damaged = false;
    int items_damaged = 0;
    int items_destroyed = 0;
    std::string damaged_item_name;

    std::vector<item> contents;
    map_stack items = i_at( p );
    for( auto i = items.begin(); i != items.end(); ) {
        if( i->made_of( phase_id::LIQUID ) ) {
            i++;
            continue;
        }

       
        if (i->has_flag(flag_EXPLOSION_SMASHED) ) {
            i++;
            continue;
        }





        if( i->active ) {
            // Get the explosion item actor
            if( i->type->get_use( "explosion" ) != nullptr ) {
                const explosion_iuse *actor = dynamic_cast<const explosion_iuse *>(
                                                  i->type->get_use( "explosion" )->get_actor_ptr() );
                if( actor != nullptr ) {
                    // If we're looking at another bomb, don't blow it up early for now.
                    // i++ here because we aren't iterating in the loop header.
                    i++;
                    continue;
                }
            }
        }

        const float material_factor = i->chip_resistance( true );
        if( power < material_factor ) {
            i++;
            continue;
        }

        // The volume check here pretty much only influences corpses and very large items
        const float volume_factor = std::max<float>( 40, i->volume() / units::legacy_volume_factor );
        float damage_chance = 10.0f * power / volume_factor;
        // Example:
        // Power 40 (just below C4 epicenter) vs plank
        // damage_chance = 10 * 40 / 40 = 10, material_factor = 8
        // Will deal 1 damage, then 20% chance for another point
        // Power 20 (grenade minus shrapnel) vs glass bottle
        // 10 * 20 / 40 = 5 vs 1
        // 5 damage (destruction)

        const bool by_charges = i->count_by_charges();
        // See if they were damaged
        if( by_charges ) {
            damage_chance *= i->charges_per_volume( 250_ml );
            while( ( damage_chance > material_factor ||
                     x_in_y( damage_chance, material_factor ) ) &&
                   i->charges > 0 ) {
                i->charges--;
                damage_chance -= material_factor;
                // We can't increment items_damaged directly because a single item can be damaged more than once
                item_was_damaged = true;
            }
        } else {
            const field_type_id type_blood = i->is_corpse() ? i->get_mtype()->bloodType() : fd_null;
            while( ( damage_chance > material_factor ||
                     x_in_y( damage_chance, material_factor ) ) &&
                   i->damage() < i->max_damage() ) {
                i->inc_damage( damage_type::BASH );
                add_splash( type_blood, p, 1, damage_chance );
                damage_chance -= material_factor;
                item_was_damaged = true;
            }
        }

        // If an item was damaged, increment the counter and set it as most recently damaged.
        if( item_was_damaged ) {

            // If this is the first item to be damaged, store its name in damaged_item_name.
            if( items_damaged == 0 ) {
                damaged_item_name = i->tname();
            }
            // Increment the counter, and reset the flag.
            items_damaged++;
            item_was_damaged = false;
        }

        // Remove them if they were damaged too much
        if( i->damage() == i->max_damage() || ( by_charges && i->charges == 0 ) ) {
            // But save the contents, except for irremovable gunmods
            for( item *elem : i->all_items_top() ) {
                if( !elem->is_irremovable() ) {
                    contents.emplace_back( *elem );
                }
            }

            i = i_rem( p, i );
            items_destroyed++;
        } else {
            i++;
        }
    }

    // Let the player know that the item was damaged if they can see it.
    if( items_destroyed > 1 ) {
        add_msg_if_player_sees( p, m_bad, _( "The %s destroys several items!" ), cause_message );
    } else if( items_destroyed == 1 && items_damaged == 1 )  {
        //~ %1$s: the cause of destruction, %2$s: destroyed item name
        add_msg_if_player_sees( p, m_bad, _( "The %1$s destroys the %2$s!" ), cause_message,
                                damaged_item_name );
    } else if( items_damaged > 1 ) {
        add_msg_if_player_sees( p, m_bad, _( "The %s damages several items." ), cause_message );
    } else if( items_damaged == 1 )  {
        //~ %1$s: the cause of damage, %2$s: damaged item name
        add_msg_if_player_sees( p, m_bad, _( "The %1$s damages the %2$s." ), cause_message,
                                damaged_item_name );
    }

    for( const item &it : contents ) {
        add_item_or_charges( p, it );
    }
}

ter_id map::get_roof( const tripoint &p, const bool allow_air ) const
{
    // This function should not be called from the 2D mode
    // Just use t_dirt instead
    cata_assert( zlevels );

    if( p.z <= -OVERMAP_DEPTH ) {
        // Could be magma/"void" instead
        return t_rock_floor;
    }

    const ter_t &ter_there = ter( p ).obj();
    const ter_str_id &roof = ter_there.roof;
    if( !roof ) {
        // No roof
        // Not acceptable if the tile is not passable
        if( !allow_air ) {
            return t_dirt;
        }

        return t_open_air;
    }

    ter_id new_ter = roof.id();
    if( new_ter == t_null ) {
        debugmsg( "map::get_new_floor: %d,%d,%d has invalid roof type %s",
                  p.x, p.y, p.z, roof.c_str() );
        return t_dirt;
    }

    if( p.z == -1 && new_ter == t_rock_floor ) {
        // HACK: A hack to work around not having a "solid earth" tile
        new_ter = t_dirt;
    }

    return new_ter;
}

// Check if there is supporting furniture cardinally adjacent to the bashed furniture
// For example, a washing machine behind the bashed door
static bool furn_is_supported( const map &m, const tripoint &p )
{
    static constexpr std::array<int8_t, 4> cx = { 0, -1, 0, 1};
    static constexpr std::array<int8_t, 4> cy = { -1,  0, 1, 0};

    for( int i = 0; i < 4; i++ ) {
        const point adj( p.xy() + point( cx[i], cy[i] ) );
        if( m.has_furn( tripoint( adj, p.z ) ) &&
            m.furn( tripoint( adj, p.z ) ).obj().has_flag( ter_furn_flag::TFLAG_BLOCKSDOOR ) ) {
            return true;
        }
    }

    return false;
}

void map::bash_ter_furn_new(const tripoint& p, bash_params& params) {

    int sound_volume = 0;
    std::string soundfxid;
    std::string soundfxvariant;
    const ter_t& terid = ter(p).obj();
    const furn_t& furnid = furn(p).obj();
    bool smash_furn = false;
    bool smash_ter = false;
    const map_bash_info* bash = nullptr;

    bool success = false;

    if (has_furn(p) && furnid.bash.str_max != -1) {
        bash = &furnid.bash;
        smash_furn = true;
    }
    else if (ter(p).obj().bash.str_max != -1) {
        bash = &ter(p).obj().bash;
        smash_ter = true;
    }

    // Floor bashing check
    // Only allow bashing floors when we want to bash floors and we're in z-level mode
    // Unless we're destroying, then it gets a little weird
    if (smash_ter && bash->bash_below && (!zlevels || !params.bash_floor)) {
        if (!params.destroy) { // NOLINT(bugprone-branch-clone)
            smash_ter = false;
            bash = nullptr;
        }
        else if (!bash->ter_set && zlevels) {
            // HACK: A hack for destroy && !bash_floor
            // We have to check what would we create and cancel if it is what we have now
            tripoint below(p.xy(), p.z - 1);
            const auto roof = get_roof(below, false);
            if (roof == ter(p)) {
                smash_ter = false;
                bash = nullptr;
            }
        }
        else if (!bash->ter_set && ter(p) == t_dirt) {
            // As above, except for no-z-levels case
            smash_ter = false;
            bash = nullptr;
        }
    }

    // TODO: what if silent is true?
    if (has_flag(ter_furn_flag::TFLAG_ALARMED, p) &&
        !get_timed_events().queued(timed_event_type::WANTED)) {
        sounds::sound(p, 40, sounds::sound_t::alarm, _("an alarm go off!"),
            false, "environment", "alarm");
        Character& player_character = get_player_character();
        // Blame nearby player
        if (rl_dist(player_character.pos(), p) <= 3) {
            get_event_bus().send<event_type::triggers_alarm>(player_character.getID());
            get_timed_events().add(timed_event_type::WANTED, calendar::turn + 30_minutes, 0,
                tripoint_abs_ms(getabs(p)));
        }
    }

    if (bash == nullptr || (bash->destroy_only && !params.destroy)) {
        // Nothing bashable here
        if (impassable(p)) {
            if (!params.silent) {
                sounds::sound(p, 18, sounds::sound_t::combat, _("thump!"),
                    false, "smash_fail", "default");
            }

            params.did_bash = true;
            params.bashed_solid = true;
        }

        return;
    }

    int smin = bash->str_min;
    int smax = bash->str_max;
    int sound_vol = bash->sound_vol;
    int sound_fail_vol = bash->sound_fail_vol;
    if (!params.destroy) {
        if (bash->str_min_blocked != -1 || bash->str_max_blocked != -1) {
            if (furn_is_supported(*this, p)) {
                if (bash->str_min_blocked != -1) {
                    smin = bash->str_min_blocked;
                }
                if (bash->str_max_blocked != -1) {
                    smax = bash->str_max_blocked;
                }
            }
        }

        if (bash->str_min_supported != -1 || bash->str_max_supported != -1) {
            tripoint below(p.xy(), p.z - 1);
            if (!zlevels || has_flag(ter_furn_flag::TFLAG_SUPPORTS_ROOF, below)) {
                if (bash->str_min_supported != -1) {
                    smin = bash->str_min_supported;
                }
                if (bash->str_max_supported != -1) {
                    smax = bash->str_max_supported;
                }
            }
        }
        // Linear interpolation from str_min to str_max
        const int resistance = smin + (params.roll * (smax - smin));
        if (params.strength >= resistance) {
            success = true;
        }
    }

    if (smash_furn) {
        soundfxvariant = furnid.id.str();
    }
    else {
        soundfxvariant = terid.id.str();
    }

    if (!params.destroy && !success) {
        if (sound_fail_vol == -1) {
            sound_volume = 12;
        }
        else {
            sound_volume = sound_fail_vol;
        }

        params.did_bash = true;
        if (!params.silent) {
            sounds::sound(p, sound_volume, sounds::sound_t::combat, bash->sound_fail, false,
                "smash_fail", soundfxvariant);
        }

        return;
    }

    // Clear out any partially grown seeds
    if (has_flag_ter_or_furn(ter_furn_flag::TFLAG_PLANT, p)) {
        i_clear(p);
    }

    if ((smash_furn && has_flag_furn(ter_furn_flag::TFLAG_FUNGUS, p)) ||
        (smash_ter && has_flag_ter(ter_furn_flag::TFLAG_FUNGUS, p))) {
        fungal_effects().create_spores(p);
    }

    if (params.destroy) {
        sound_volume = smin * 2;
    }
    else {
        if (sound_vol == -1) {
            sound_volume = std::min(static_cast<int>(smin * 1.5), smax);
        }
        else {
            sound_volume = sound_vol;
        }
    }

    soundfxid = "smash_success";
    const translation& sound = bash->sound;
    // Set this now in case the ter_set below changes this
    const bool will_collapse = smash_ter &&
        has_flag(ter_furn_flag::TFLAG_SUPPORTS_ROOF, p) && !has_flag(ter_furn_flag::TFLAG_INDOORS, p);
    const bool tent = smash_furn && !bash->tent_centers.empty();

    // Special code to collapse the tent if destroyed
    if (tent) {
        // Get ids of possible centers
        std::set<furn_id> centers;
        for (const auto& cur_id : bash->tent_centers) {
            if (cur_id.is_valid()) {
                centers.insert(cur_id);
            }
        }

        std::optional<std::pair<tripoint, furn_id>> tentp;

        // Find the center of the tent
        // First check if we're not currently bashing the center
        if (centers.count(furn(p)) > 0) {
            tentp.emplace(p, furn(p));
        }
        else {
            for (const tripoint& pt : points_in_radius(p, bash->collapse_radius)) {
                const furn_id& f_at = furn(pt);
                // Check if we found the center of the current tent
                if (centers.count(f_at) > 0) {
                    tentp.emplace(pt, f_at);
                    break;
                }
            }
        }
        // Didn't find any tent center, wreck the current tile
        if (!tentp) {
            spawn_items(p, item_group::items_from(bash->drop_group, calendar::turn));
            furn_set(p, bash->furn_set);
        }
        else {
            // Take the tent down
            const int rad = tentp->second.obj().bash.collapse_radius;
            for (const tripoint& pt : points_in_radius(tentp->first, rad)) {
                const auto frn = furn(pt);
                if (frn == f_null) {
                    continue;
                }

                const map_bash_info* recur_bash = &frn.obj().bash;
                // Check if we share a center type and thus a "tent type"
                for (const auto& cur_id : recur_bash->tent_centers) {
                    if (centers.count(cur_id.id()) > 0) {
                        // Found same center, wreck current tile
                        spawn_items(p, item_group::items_from(recur_bash->drop_group, calendar::turn));
                        furn_set(pt, recur_bash->furn_set);
                        break;
                    }
                }
            }
        }
        soundfxvariant = "smash_cloth";
    }
    else if (smash_furn) {
        furn_set(p, bash->furn_set);
        for (item& it : i_at(p)) {
            it.on_drop(p, *this);
        }
        // HACK: Hack alert.
        // Signs have cosmetics associated with them on the submap since
        // furniture can't store dynamic data to disk. To prevent writing
        // mysteriously appearing for a sign later built here, remove the
        // writing from the submap.
        delete_signage(p);
    }
    else if (!smash_ter) {
        // Handle error earlier so that we can assume smash_ter is true below
        debugmsg("data/json/terrain.json does not have %s.bash.ter_set set!",
            ter(p).obj().id.c_str());
    }
    else if (params.bashing_from_above && bash->ter_set_bashed_from_above) {
        // If this terrain is being bashed from above and this terrain
        // has a valid post-destroy bashed-from-above terrain, set it
        ter_set(p, bash->ter_set_bashed_from_above);
    }
    else if (bash->ter_set) {
        // If the terrain has a valid post-destroy terrain, set it
        ter_set(p, bash->ter_set);
    }
    else {
        tripoint below(p.xy(), p.z - 1);
        const ter_t& ter_below = ter(below).obj();
        if (bash->bash_below && ter_below.has_flag(ter_furn_flag::TFLAG_SUPPORTS_ROOF)) {
            // When bashing the tile below, don't allow bashing the floor
            bash_params params_below = params; // Make a copy
            params_below.bashing_from_above = true;
            bash_ter_furn(below, params_below);
        }

        furn_set(p, f_null);
        ter_set(p, t_open_air);
    }

    if (!tent) {
        spawn_items(p, item_group::items_from(bash->drop_group, calendar::turn));
    }

    if (smash_ter && ter(p) == t_open_air && zlevels) {
        tripoint below(p.xy(), p.z - 1);
        const auto roof = get_roof(below, params.bash_floor && ter(below).obj().movecost != 0);
        ter_set(p, roof);
    }

    if (bash->explosive > 0) {
        explosion_handler::explosion(nullptr, p, bash->explosive, 0.8, false);
    }

    if (will_collapse && !has_flag(ter_furn_flag::TFLAG_SUPPORTS_ROOF, p)) {
        collapse_at(p, params.silent, true, bash->explosive > 0);
    }

    params.did_bash = true;
    params.success |= success; // Not always true, so that we can tell when to stop destroying
    params.bashed_solid = true;
    if (!sound.empty() && !params.silent) {
        sounds::sound(p, sound_volume, sounds::sound_t::combat, sound, false,
            soundfxid, soundfxvariant);
    }
}











void map::bash_ter_furn( const tripoint &p, bash_params &params )
{
    int sound_volume = 0;
    std::string soundfxid;
    std::string soundfxvariant;
    const ter_t &terid = ter( p ).obj();
    const furn_t &furnid = furn( p ).obj();
    bool smash_furn = false;
    bool smash_ter = false;
    const map_bash_info *bash = nullptr;

    bool success = false;

    if( has_furn( p ) && furnid.bash.str_max != -1 ) {
        bash = &furnid.bash;
        smash_furn = true;
    } else if( ter( p ).obj().bash.str_max != -1 ) {
        bash = &ter( p ).obj().bash;
        smash_ter = true;
    }

    // Floor bashing check
    // Only allow bashing floors when we want to bash floors and we're in z-level mode
    // Unless we're destroying, then it gets a little weird
    if( smash_ter && bash->bash_below && ( !zlevels || !params.bash_floor ) ) {
        if( !params.destroy ) { // NOLINT(bugprone-branch-clone)
            smash_ter = false;
            bash = nullptr;
        } else if( !bash->ter_set && zlevels ) {
            // HACK: A hack for destroy && !bash_floor
            // We have to check what would we create and cancel if it is what we have now
            tripoint below( p.xy(), p.z - 1 );
            const auto roof = get_roof( below, false );
            if( roof == ter( p ) ) {
                smash_ter = false;
                bash = nullptr;
            }
        } else if( !bash->ter_set && ter( p ) == t_dirt ) {
            // As above, except for no-z-levels case
            smash_ter = false;
            bash = nullptr;
        }
    }

    // TODO: what if silent is true?
    if( has_flag( ter_furn_flag::TFLAG_ALARMED, p ) &&
        !get_timed_events().queued( timed_event_type::WANTED ) ) {
        sounds::sound( p, 40, sounds::sound_t::alarm, _( "an alarm go off!" ),
                       false, "environment", "alarm" );
        Character &player_character = get_player_character();
        // Blame nearby player
        if( rl_dist( player_character.pos(), p ) <= 3 ) {
            get_event_bus().send<event_type::triggers_alarm>( player_character.getID() );
            get_timed_events().add( timed_event_type::WANTED, calendar::turn + 30_minutes, 0,
                                    tripoint_abs_ms( getabs( p ) ) );
        }
    }

    if( bash == nullptr || ( bash->destroy_only && !params.destroy ) ) {
        // Nothing bashable here
        if( impassable( p ) ) {
            if( !params.silent ) {
                sounds::sound( p, 18, sounds::sound_t::combat, _( "thump!" ),
                               false, "smash_fail", "default" );
            }

            params.did_bash = true;
            params.bashed_solid = true;
        }

        return;
    }

    int smin = bash->str_min;
    int smax = bash->str_max;
    int sound_vol = bash->sound_vol;
    int sound_fail_vol = bash->sound_fail_vol;
    if( !params.destroy ) {
        if( bash->str_min_blocked != -1 || bash->str_max_blocked != -1 ) {
            if( furn_is_supported( *this, p ) ) {
                if( bash->str_min_blocked != -1 ) {
                    smin = bash->str_min_blocked;
                }
                if( bash->str_max_blocked != -1 ) {
                    smax = bash->str_max_blocked;
                }
            }
        }

        if( bash->str_min_supported != -1 || bash->str_max_supported != -1 ) {
            tripoint below( p.xy(), p.z - 1 );
            if( !zlevels || has_flag( ter_furn_flag::TFLAG_SUPPORTS_ROOF, below ) ) {
                if( bash->str_min_supported != -1 ) {
                    smin = bash->str_min_supported;
                }
                if( bash->str_max_supported != -1 ) {
                    smax = bash->str_max_supported;
                }
            }
        }
        // Linear interpolation from str_min to str_max
        const int resistance = smin + ( params.roll * ( smax - smin ) );
        if( params.strength >= resistance ) {
            success = true;
        }
    }

    if( smash_furn ) {
        soundfxvariant = furnid.id.str();
    } else {
        soundfxvariant = terid.id.str();
    }

    if( !params.destroy && !success ) {
        if( sound_fail_vol == -1 ) {
            sound_volume = 12;
        } else {
            sound_volume = sound_fail_vol;
        }

        params.did_bash = true;
        if( !params.silent ) {
            sounds::sound( p, sound_volume, sounds::sound_t::combat, bash->sound_fail, false,
                           "smash_fail", soundfxvariant );
        }

        return;
    }

    // Clear out any partially grown seeds
    if( has_flag_ter_or_furn( ter_furn_flag::TFLAG_PLANT, p ) ) {
        i_clear( p );
    }

    if( ( smash_furn && has_flag_furn( ter_furn_flag::TFLAG_FUNGUS, p ) ) ||
        ( smash_ter && has_flag_ter( ter_furn_flag::TFLAG_FUNGUS, p ) ) ) {
        fungal_effects().create_spores( p );
    }

    if( params.destroy ) {
        sound_volume = smin * 2;
    } else {
        if( sound_vol == -1 ) {
            sound_volume = std::min( static_cast<int>( smin * 1.5 ), smax );
        } else {
            sound_volume = sound_vol;
        }
    }

    soundfxid = "smash_success";
    const translation &sound = bash->sound;
    // Set this now in case the ter_set below changes this
    const bool will_collapse = smash_ter &&
                               has_flag( ter_furn_flag::TFLAG_SUPPORTS_ROOF, p ) && !has_flag( ter_furn_flag::TFLAG_INDOORS, p );
    const bool tent = smash_furn && !bash->tent_centers.empty();

    // Special code to collapse the tent if destroyed
    if( tent ) {
        // Get ids of possible centers
        std::set<furn_id> centers;
        for( const auto &cur_id : bash->tent_centers ) {
            if( cur_id.is_valid() ) {
                centers.insert( cur_id );
            }
        }

        std::optional<std::pair<tripoint, furn_id>> tentp;

        // Find the center of the tent
        // First check if we're not currently bashing the center
        if( centers.count( furn( p ) ) > 0 ) {
            tentp.emplace( p, furn( p ) );
        } else {
            for( const tripoint &pt : points_in_radius( p, bash->collapse_radius ) ) {
                const furn_id &f_at = furn( pt );
                // Check if we found the center of the current tent
                if( centers.count( f_at ) > 0 ) {
                    tentp.emplace( pt, f_at );
                    break;
                }
            }
        }
        // Didn't find any tent center, wreck the current tile
        if( !tentp ) {
            spawn_items( p, item_group::items_from( bash->drop_group, calendar::turn ) );
            furn_set( p, bash->furn_set );
        } else {
            // Take the tent down
            const int rad = tentp->second.obj().bash.collapse_radius;
            for( const tripoint &pt : points_in_radius( tentp->first, rad ) ) {
                const auto frn = furn( pt );
                if( frn == f_null ) {
                    continue;
                }

                const map_bash_info *recur_bash = &frn.obj().bash;
                // Check if we share a center type and thus a "tent type"
                for( const auto &cur_id : recur_bash->tent_centers ) {
                    if( centers.count( cur_id.id() ) > 0 ) {
                        // Found same center, wreck current tile
                        spawn_items( p, item_group::items_from( recur_bash->drop_group, calendar::turn ) );
                        furn_set( pt, recur_bash->furn_set );
                        break;
                    }
                }
            }
        }
        soundfxvariant = "smash_cloth";
    } else if( smash_furn ) {
        furn_set( p, bash->furn_set );
        for( item &it : i_at( p ) )  {
            it.on_drop( p, *this );
        }
        // HACK: Hack alert.
        // Signs have cosmetics associated with them on the submap since
        // furniture can't store dynamic data to disk. To prevent writing
        // mysteriously appearing for a sign later built here, remove the
        // writing from the submap.
        delete_signage( p );
    } else if( !smash_ter ) {
        // Handle error earlier so that we can assume smash_ter is true below
        debugmsg( "data/json/terrain.json does not have %s.bash.ter_set set!",
                  ter( p ).obj().id.c_str() );
    } else if( params.bashing_from_above && bash->ter_set_bashed_from_above ) {
        // If this terrain is being bashed from above and this terrain
        // has a valid post-destroy bashed-from-above terrain, set it
        ter_set( p, bash->ter_set_bashed_from_above );
    } else if( bash->ter_set ) {
        // If the terrain has a valid post-destroy terrain, set it
        ter_set( p, bash->ter_set );
    } else {
        tripoint below( p.xy(), p.z - 1 );
        const ter_t &ter_below = ter( below ).obj();
        if( bash->bash_below && ter_below.has_flag( ter_furn_flag::TFLAG_SUPPORTS_ROOF ) ) {
            // When bashing the tile below, don't allow bashing the floor
            bash_params params_below = params; // Make a copy
            params_below.bashing_from_above = true;
            bash_ter_furn( below, params_below );
        }

        furn_set( p, f_null );
        ter_set( p, t_open_air );
    }

    if( !tent ) {
        spawn_items( p, item_group::items_from( bash->drop_group, calendar::turn ) );
    }

    if( smash_ter && ter( p ) == t_open_air && zlevels ) {
        tripoint below( p.xy(), p.z - 1 );
        const auto roof = get_roof( below, params.bash_floor && ter( below ).obj().movecost != 0 );
        ter_set( p, roof );
    }

    if( bash->explosive > 0 ) {
        explosion_handler::explosion( nullptr, p, bash->explosive, 0.8, false );
    }

    if( will_collapse && !has_flag( ter_furn_flag::TFLAG_SUPPORTS_ROOF, p ) ) {
        collapse_at( p, params.silent, true, bash->explosive > 0 );
    }

    params.did_bash = true;
    params.success |= success; // Not always true, so that we can tell when to stop destroying
    params.bashed_solid = true;
    if( !sound.empty() && !params.silent ) {
        sounds::sound( p, sound_volume, sounds::sound_t::combat, sound, false,
                       soundfxid, soundfxvariant );
    }
}

bash_params map::bash( const tripoint &p, const int str,
                       bool silent, bool destroy, bool bash_floor,
                       const vehicle *bashing_vehicle )
{
    bash_params bsh{
        str, silent, destroy, bash_floor, static_cast<float>( rng_float( 0, 1.0f ) ), false, false, false, false
    };
    if( !inbounds( p ) ) {
        return bsh;
    }

    bool bashed_sealed = false;
    if( has_flag( ter_furn_flag::TFLAG_SEALED, p ) ) {
        bash_ter_furn( p, bsh );
        bashed_sealed = true;
    }

    bash_field( p, bsh );

    // Don't bash items inside terrain/furniture with SEALED flag
    if( !bashed_sealed ) {
        bash_items( p, bsh );
    }
    // Don't bash the vehicle doing the bashing
    const vehicle *veh = veh_pointer_or_null( veh_at( p ) );
    if( veh != nullptr && veh != bashing_vehicle ) {
        bash_vehicle( p, bsh );
    }

    // If we still didn't bash anything solid (a vehicle) or a tile with SEALED flag, bash ter/furn
    if( !bsh.bashed_solid && !bashed_sealed ) {
        bash_ter_furn( p, bsh );
    }

    return bsh;
}









void map::bash_items( const tripoint &p, bash_params &params )
{
    if( !has_items( p ) || has_flag_ter_or_furn( ter_furn_flag::TFLAG_PLANT, p ) ) {
        return;
    }

    std::vector<item> smashed_contents;
    map_stack bashed_items = i_at( p );
    bool smashed_glass = false;
    for( auto bashed_item = bashed_items.begin(); bashed_item != bashed_items.end(); ) {
        // the check for active suppresses Molotovs smashing themselves with their own explosion
        int glass_portion = bashed_item->made_of( material_glass );
        float glass_fraction = glass_portion / static_cast<float>( bashed_item->type->mat_portion_total );
        if( glass_portion && !bashed_item->active && rng_float( 0.0f, 1.0f ) < glass_fraction * 0.5f ) {
            params.did_bash = true;
            smashed_glass = true;
            for( const item *bashed_content : bashed_item->all_items_top() ) {
                smashed_contents.emplace_back( *bashed_content );
            }
            bashed_item = bashed_items.erase( bashed_item );
        } else {
            ++bashed_item;
        }
    }
    // Now plunk in the contents of the smashed items.
    spawn_items( p, smashed_contents );

    // Add a glass sound even when something else also breaks
    if( smashed_glass && !params.silent ) {
        sounds::sound( p, 12, sounds::sound_t::combat, _( "glass shattering." ), false,
                       "smash_success", "smash_glass_contents" );
    }
}

void map::bash_vehicle( const tripoint &p, bash_params &params )
{
    // Smash vehicle if present
    if( const optional_vpart_position vp = veh_at( p ) ) {
        vp->vehicle().damage( *this, vp->part_index(), params.strength, damage_type::BASH );
        if( !params.silent ) {
            sounds::sound( p, 18, sounds::sound_t::combat, _( "crash!" ), false,
                           "smash_success", "hit_vehicle" );
        }

        params.did_bash = true;
        params.success = true;
        params.bashed_solid = true;
    }
}

void map::bash_field( const tripoint &p, bash_params &params )
{
    std::vector<field_type_id> to_remove;
    for( const std::pair<const field_type_id, field_entry> &fd : field_at( p ) ) {
        if( fd.first->bash_info.str_min > -1 ) {
            params.did_bash = true;
            params.bashed_solid = true; // To prevent bashing furniture/vehicles
            to_remove.push_back( fd.first );
        }
    }
    for( field_type_id fd : to_remove ) {
        remove_field( p, fd );
    }
}

void map::destroy( const tripoint &p, const bool silent )
{
    // Break if it takes more than 25 destructions to remove to prevent infinite loops
    // Example: A bashes to B, B bashes to A leads to A->B->A->...
    int count = 0;
    while( count <= 25 && bash( p, 999, silent, true ).success ) {
        count++;
    }
}

void map::destroy_furn( const tripoint &p, const bool silent )
{
    // Break if it takes more than 25 destructions to remove to prevent infinite loops
    // Example: A bashes to B, B bashes to A leads to A->B->A->...
    int count = 0;
    while( count <= 25 && furn( p ) != f_null && bash( p, 999, silent, true ).success ) {
        count++;
    }
}

void map::destroy_furn( const tripoint_bub_ms &p, const bool silent )
{
    destroy_furn( p.raw(), silent );
}

void map::batter( const tripoint &p, int power, int tries, const bool silent )
{
    int count = 0;
    while( count < tries && bash( p, power, silent ).success ) {
        count++;
    }
}

void map::crush( const tripoint &p )
{
    creature_tracker &creatures = get_creature_tracker();
    Character *crushed_player = creatures.creature_at<Character>( p );

    if( crushed_player != nullptr ) {
        bool player_inside = false;
        if( crushed_player->in_vehicle ) {
            const optional_vpart_position vp = veh_at( p );
            player_inside = vp && vp->is_inside();
        }
        if( !player_inside ) { //If there's a player at p and he's not in a covered vehicle...
            //This is the roof coming down on top of us, no chance to dodge
            crushed_player->add_msg_player_or_npc( m_bad, _( "You are crushed by the falling debris!" ),
                                                   _( "<npcname> is crushed by the falling debris!" ) );
            // TODO: Make this depend on the ceiling material
            const int dam = rng( 0, 40 );
            // Torso and head take the brunt of the blow
            crushed_player->deal_damage( nullptr, bodypart_id( "head" ), damage_instance( damage_type::BASH,
                                         dam * .25 ) );
            crushed_player->deal_damage( nullptr, bodypart_id( "torso" ), damage_instance( damage_type::BASH,
                                         dam * .45 ) );
            // Legs take the next most through transferred force
            crushed_player->deal_damage( nullptr, bodypart_id( "leg_l" ), damage_instance( damage_type::BASH,
                                         dam * .10 ) );
            crushed_player->deal_damage( nullptr, bodypart_id( "leg_r" ), damage_instance( damage_type::BASH,
                                         dam * .10 ) );
            // Arms take the least
            crushed_player->deal_damage( nullptr, bodypart_id( "arm_l" ), damage_instance( damage_type::BASH,
                                         dam * .05 ) );
            crushed_player->deal_damage( nullptr, bodypart_id( "arm_r" ), damage_instance( damage_type::BASH,
                                         dam * .05 ) );

            // Pin whoever got hit
            crushed_player->add_effect( effect_crushed, 1_turns, true );
            crushed_player->check_dead_state();
        }
    }

    if( monster *const monhit = creatures.creature_at<monster>( p ) ) {
        // 25 ~= 60 * .45 (torso)
        monhit->deal_damage( nullptr, bodypart_id( "torso" ), damage_instance( damage_type::BASH, rng( 0,
                             25 ) ) );

        // Pin whoever got hit
        monhit->add_effect( effect_crushed, 1_turns, true );
        monhit->check_dead_state();
    }

    if( const optional_vpart_position vp = veh_at( p ) ) {
        // Arbitrary number is better than collapsing house roof crushing APCs
        vp->vehicle().damage( *this, vp->part_index(), rng( 100, 1000 ), damage_type::BASH, false );
    }
}

void map::shoot( const tripoint &p, projectile &proj, const bool hit_items )
{
    // TODO: make bashing better a destroying, worse at penetrating
    std::array<float, static_cast<int>( damage_type::NUM )> dmg_by_type {};
    for( const damage_unit &dam : proj.impact ) {
        dmg_by_type[static_cast<int>( dam.type )] +=
            dam.amount * dam.damage_multiplier * dam.unconditional_damage_mult +
            dam.res_pen * dam.res_mult * dam.unconditional_res_mult;
    }
    const float initial_damage = std::accumulate( dmg_by_type.begin(), dmg_by_type.end(), 0.0f );
    if( initial_damage < 0 ) {
        return;
    }
    // TODO: use this for more than just vehicle parts
    const damage_type main_damage_type = static_cast<damage_type>(
            std::max_element( dmg_by_type.begin(), dmg_by_type.end() ) - dmg_by_type.begin() );

    // damage value that may be reduced by vehicles, furniture, terrain or fields
    float dam = initial_damage;

    const auto &ammo_effects = proj.proj_effects;
    const bool incendiary = ammo_effects.count( "INCENDIARY" );
    const bool laser = ammo_effects.count( "LASER" );

    if( const optional_vpart_position vp = veh_at( p ) ) {
        dam = vp->vehicle().damage( *this, vp->part_index(), dam, main_damage_type, hit_items );
    }

    const auto shoot_furn_ter = [&]( const map_data_common_t &data ) {
        const map_shoot_info &shoot = *data.shoot;
        bool destroyed = false;

        // if you are aiming at this tile, you can never miss
        if( hit_items || x_in_y( shoot.chance_to_hit, 100 ) ) {
            if( laser ) {
                dam -= rng( shoot.reduce_dmg_min_laser, shoot.reduce_dmg_max_laser );
            } else {
                dam -= rng( shoot.reduce_dmg_min, shoot.reduce_dmg_max );
            }
            // lasers can't destroy some types of furn/ter you can shoot through
            if( !laser || !shoot.no_laser_destroy ) {
                // important to use initial damage, energy from reduction has gone into the furn/ter
                const int min_damage = int( initial_damage ) - shoot.destroy_dmg_min;
                const int max_damage = shoot.destroy_dmg_max - shoot.destroy_dmg_min;
                if( x_in_y( min_damage, max_damage ) ) {
                    // don't need to duplicate all the destruction logic here
                    bash_params bsh{ 0, false, true, false, 0.0, false, false, false, false };
                    bash_ter_furn( p, bsh );
                    destroyed = true;
                }
            }
            if( dam <= 0 && get_player_view().sees( p ) ) {
                add_msg( _( "The shot is stopped by the %s!" ), data.name() );
            }
            // only very flammable furn/ter can be set alight with incendiary rounds
            if( incendiary && data.has_flag( ter_furn_flag::TFLAG_FLAMMABLE_ASH ) ) {
                add_field( p, fd_fire, 1 );
            }
            // bash_ter_furn already triggers the alarm
            // TODO: fix alarm event weirdness (not just here, also in bash, hack, etc)
            if( !destroyed && data.has_flag( ter_furn_flag::TFLAG_ALARMED ) &&
                !get_timed_events().queued( timed_event_type::WANTED ) ) {
                sounds::sound( p, 40, sounds::sound_t::alarm, _( "an alarm go off!" ),
                               false, "environment", "alarm" );
                get_timed_events().add( timed_event_type::WANTED, calendar::turn + 30_minutes, 0,
                                        tripoint_abs_ms( getabs( p ) ) );
            }
            return true;
        }
        return false;
    };

    furn_id furniture = furn( p );
    ter_id terrain = ter( p );
    bool hit_something = false;

    // shoot through furniture or terrain and see if we hit something
    if( furniture->shoot ) {
        hit_something |= shoot_furn_ter( furniture.obj() );
    } else if( terrain->shoot ) {
        hit_something |= shoot_furn_ter( terrain.obj() );
        // fall back to just bashing when shoot data is not defined
    } else if( impassable( p ) && !is_transparent( p ) ) {
        bash( p, dam, false );
        dam = 0;
    }
    dam = std::max( 0.0f, dam );

    for( const ammo_effect &ae : ammo_effects::get_all() ) {
        if( ammo_effects.count( ae.id.str() ) > 0 ) {
            if( x_in_y( ae.trail_chance, 100 ) ) {
                add_field( p, ae.trail_field_type, rng( ae.trail_intensity_min, ae.trail_intensity_max ) );
            }
        }
    }

    // Check fields?
    field &fields_there = field_at( p );
    if( fields_there.field_count() > 0 ) {
        // Need to make a copy since 'remove_field' modifies the value
        field fields_copy = fields_there;
        for( const std::pair<const field_type_id, field_entry> &fd : fields_copy ) {
            if( fd.first->bash_info.str_min > 0 ) {
                if( incendiary ) {
                    add_field( p, fd_fire, fd.second.get_field_intensity() - 1 );
                } else if( dam > 5 + fd.second.get_field_intensity() * 5 &&
                           one_in( 5 - fd.second.get_field_intensity() ) ) {
                    dam -= rng( 1, 2 + fd.second.get_field_intensity() * 2 );
                    remove_field( p, fd.first );
                }
            }
        }
    }

    // Rescale the damage
    if( dam <= 0 ) {
        proj.impact.damage_units.clear();
        return;
    } else if( dam < initial_damage ) {
        proj.impact.mult_damage( dam / static_cast<double>( initial_damage ) );
    }

    // for now, shooting furniture or terrain protects any items
    if( !hit_items || hit_something ) {
        return;
    }

    // Make sure the message is sensible for the ammo effects. Lasers aren't projectiles.
    std::string damage_message;
    if( ammo_effects.count( "LASER" ) ) {
        damage_message = _( "laser beam" );
    } else if( ammo_effects.count( "LIGHTNING" ) ) {
        damage_message = _( "bolt of electricity" );
    } else if( ammo_effects.count( "PLASMA" ) ) {
        damage_message = _( "bolt of plasma" );
    } else {
        damage_message = _( "flying projectile" );
    }

    // Now, smash items on that tile.
    // dam / 3, because bullets aren't all that good at destroying items...
    smash_items( p, dam / 3, damage_message );
}

bool map::hit_with_acid( const tripoint &p )
{
    if( passable( p ) ) {
        return false;    // Didn't hit the tile!
    }
    const ter_id t = ter( p );
    if( t == t_wall_glass || t == t_wall_glass_alarm ||
        t == t_vat ) {
        ter_set( p, t_floor );
    } else if( t == t_door_c || t == t_door_locked || t == t_door_locked_peep ||
               t == t_door_locked_alarm ) {
        if( one_in( 3 ) ) {
            ter_set( p, t_door_b );
        }
    } else if( t == t_door_bar_c || t == t_door_bar_o || t == t_door_bar_locked || t == t_bars ||
               t == t_reb_cage ) {
        ter_set( p, t_floor );
        add_msg_if_player_sees( p, m_warning, _( "The metal bars melt!" ) );
    } else if( t == t_door_b ) {
        if( one_in( 4 ) ) {
            ter_set( p, t_door_frame );
        } else {
            return false;
        }
    } else if( t == t_window || t == t_window_alarm || t == t_window_no_curtains ) {
        ter_set( p, t_window_empty );
    } else if( t == t_wax ) {
        ter_set( p, t_floor_wax );
    } else if( t == t_gas_pump || t == t_gas_pump_smashed ) {
        return false;
    } else if( t == t_card_science || t == t_card_military || t == t_card_industrial ) {
        ter_set( p, t_card_reader_broken );
    }
    return true;
}

// returns true if terrain stops fire
bool map::hit_with_fire( const tripoint &p )
{
    if( passable( p ) ) {
        return false;    // Didn't hit the tile!
    }

    // non passable but flammable terrain, set it on fire
    if( has_flag( ter_furn_flag::TFLAG_FLAMMABLE, p ) ||
        has_flag( ter_furn_flag::TFLAG_FLAMMABLE_ASH, p ) ) {
        add_field( p, fd_fire, 3 );
    }
    return true;
}
bool map::open_door( Creature const &u, const tripoint &p, const bool inside,
                     const bool check_only )
{
    const ter_t &ter = this->ter( p ).obj();
    const furn_t &furn = this->furn( p ).obj();
    if( ter.open ) {
        if( has_flag( ter_furn_flag::TFLAG_OPENCLOSE_INSIDE, p ) && !inside ) {
            return false;
        }

        if( !check_only ) {
            sounds::sound( p, 6, sounds::sound_t::movement, _( "swish" ), true,
                           "open_door", ter.id.str() );
            ter_set( p, ter.open );

            if( u.has_trait( trait_SCHIZOPHRENIC ) && u.is_avatar() &&
                one_in( 50 ) && !ter.has_flag( ter_furn_flag::TFLAG_TRANSPARENT ) ) {
                tripoint mp = p + -2 * u.pos().xy() + tripoint( 2 * p.x, 2 * p.y, p.z );
                g->spawn_hallucination( mp );
            }
        }

        return true;
    } else if( furn.open ) {
        if( has_flag( ter_furn_flag::TFLAG_OPENCLOSE_INSIDE, p ) && !inside ) {
            return false;
        }

        if( !check_only ) {
            sounds::sound( p, 6, sounds::sound_t::movement, _( "swish" ), true,
                           "open_door", furn.id.str() );
            furn_set( p, furn.open );
        }

        return true;
    } else if( const optional_vpart_position vp = veh_at( p ) ) {
        int openable = vp->vehicle().next_part_to_open( vp->part_index(), true );
        if( openable >= 0 ) {
            if( !check_only ) {
                if( ( u.is_npc() || u.is_avatar() ) &&
                    !vp->vehicle().handle_potential_theft( *u.as_character() ) ) {
                    return false;
                }
                vp->vehicle().open_all_at( openable );
            }

            return true;
        }

        return false;
    }

    return false;
}

void map::translate( const ter_id &from, const ter_id &to )
{
    if( from == to ) {
        debugmsg( "map::translate %s => %s",
                  from.obj().name(),
                  from.obj().name() );
        return;
    }
    for( const tripoint &p : points_on_zlevel() ) {
        if( ter( p ) == from ) {
            ter_set( p, to );
        }
    }
}

//This function performs the translate function within a given radius of the player.
void map::translate_radius( const ter_id &from, const ter_id &to, float radi, const tripoint &p,
                            const bool same_submap, const bool toggle_between )
{
    if( from == to ) {
        debugmsg( "map::translate %s => %s", from.obj().name(), to.obj().name() );
        return;
    }

    const tripoint abs_omt_p = ms_to_omt_copy( getabs( p ) );
    for( const tripoint &t : points_on_zlevel() ) {
        const tripoint abs_omt_t = ms_to_omt_copy( getabs( t ) );
        const float radiX = trig_dist( p, t );
        if( ter( t ) == from ) {
            // within distance, and either no submap limitation or same overmap coords.
            if( radiX <= radi && ( !same_submap || abs_omt_t == abs_omt_p ) ) {
                ter_set( t, to );
            }
        } else if( toggle_between && ter( t ) == to ) {
            if( radiX <= radi && ( !same_submap || abs_omt_t == abs_omt_p ) ) {
                ter_set( t, from );
            }
        }
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void map::transform_radius( const ter_furn_transform_id &transform, int radi,
                            const tripoint_abs_ms &p )
{
    if( !inbounds( p - point( radi, radi ) ) || !inbounds( p + point( radi, radi ) ) ) {
        debugmsg( "transform_radius called for area out of bounds" );
    }
    tripoint_bub_ms const loc = bub_from_abs( p );
    for( tripoint_bub_ms const &t : points_in_radius( loc, radi, 0 ) ) {
        if( trig_dist( loc, t ) <= radi ) {
            transform->transform( *this, t );
        }
    }
}

void map::transform_line( const ter_furn_transform_id &transform, const tripoint_abs_ms &first,
                          const tripoint_abs_ms &second )
{
    if( !inbounds( first ) || !inbounds( second ) ) {
        debugmsg( "transform_line called for line out of bounds" );
    }
    for( const tripoint_abs_ms &t : line_to( first, second ) ) {
        transform->transform( *this, bub_from_abs( t ) );
    }
}

bool map::close_door( const tripoint &p, const bool inside, const bool check_only )
{
    if( has_flag( ter_furn_flag::TFLAG_OPENCLOSE_INSIDE, p ) && !inside ) {
        return false;
    }

    const ter_t &ter = this->ter( p ).obj();
    const furn_t &furn = this->furn( p ).obj();
    if( ter.close && !furn.id ) {
        if( !check_only ) {
            sounds::sound( p, 10, sounds::sound_t::movement, _( "swish" ), true,
                           "close_door", ter.id.str() );
            ter_set( p, ter.close );
        }
        return true;
    } else if( furn.close ) {
        if( !check_only ) {
            sounds::sound( p, 10, sounds::sound_t::movement, _( "swish" ), true,
                           "close_door", furn.id.str() );
            furn_set( p, furn.close );
        }
        return true;
    }
    return false;
}

bool map::close_door( const tripoint_bub_ms &p, const bool inside, const bool check_only )
{
    return close_door( p.raw(), inside, check_only );
}

std::string map::get_signage( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return "";
    }

    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to get signage at (%d,%d) but the submap is not loaded", l.x, l.y );
        return std::string();
    }

    return current_submap->get_signage( l );
}
void map::set_signage( const tripoint &p, const std::string &message )
{
    if( !inbounds( p ) ) {
        return;
    }

    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried set signage at (%d,%d) but the submap is not loaded", l.x, l.y );
        return;
    }

    current_submap->set_signage( l, message );
}
void map::delete_signage( const tripoint &p )
{
    if( !inbounds( p ) ) {
        return;
    }

    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to delete signage at (%d,%d) but the submap is not loaded", l.x, l.y );
        return;
    }

    current_submap->delete_signage( l );
}
void map::delete_signage( const tripoint_bub_ms &p )
{
    delete_signage( p.raw() );
}

int map::get_radiation( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return 0;
    }

    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to get radiation at (%d,%d) but the submap is not loaded", l.x, l.y );
        return 0;
    }

    return current_submap->get_radiation( l );
}

void map::set_radiation( const tripoint &p, const int value )
{
    if( !inbounds( p ) ) {
        return;
    }

    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to set radiation at (%d,%d,%d) but the submap is not loaded", l.x, l.y );
        return;
    }

    current_submap->set_radiation( l, value );
}

void map::adjust_radiation( const tripoint &p, const int delta )
{
    if( !inbounds( p ) ) {
        return;
    }

    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to adjust radiation at (%d,%d) but the submap is not loaded", l.x, l.y );
        return;
    }

    int current_radiation = current_submap->get_radiation( l );
    current_submap->set_radiation( l, current_radiation + delta );
}

units::temperature map::get_temperature_mod( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return 0_K;
    }

    const submap *const current_submap = unsafe_get_submap_at( p );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to get temperature at (%d,%d,%d) but the submap is not loaded", p.x, p.y, p.z );
        return 0_K;
    }

    return current_submap->get_temperature();
}

void map::set_temperature_mod( const tripoint &p, units::temperature new_temperature_mod )
{
    if( !inbounds( p ) ) {
        return;
    }
    submap *const current_submap = unsafe_get_submap_at( p );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to set temperature at (%d,%d,%d) but the submap is not loaded", p.x, p.y, p.z );
        return;
    }

    current_submap->set_temperature_mod( new_temperature_mod );
}
// Items: 3D

map_stack map::i_at( const tripoint &p )
{
    if( !inbounds( p ) ) {
        nulitems.clear();
        return map_stack{ &nulitems, p, this };
    }

    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to get items at (%d,%d) but the submap is not loaded", l.x, l.y );
        nulitems.clear();
        return map_stack{ &nulitems, p, this };
    }

    return map_stack{ &current_submap->get_items( l ), p, this };
}

map_stack map::i_at( const tripoint_bub_ms &p )
{
    return i_at( p.raw() );
}

map_stack::iterator map::i_rem( const tripoint &p, const map_stack::const_iterator &it )
{
    point l;
    submap *const current_submap = get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to remove items at (%d,%d) but the submap is not loaded", l.x, l.y );
        nulitems.clear();
        return map_stack{ &nulitems, p, this } .begin();
    }


    current_submap->update_lum_rem( l, *it );

    return current_submap->get_items( l ).erase( it );
}

void map::i_rem( const tripoint &p, item *it )
{
    map_stack map_items = i_at( p );
    map_stack::const_iterator iter = map_items.get_iterator_from_pointer( it );
    if( iter != map_items.end() ) {
        i_rem( p, iter );
    }
}

void map::i_rem( const tripoint_bub_ms &p, item *it )
{
    return i_rem( p.raw(), it );
}

void map::i_clear( const tripoint &p )
{
    point l;
    submap *const current_submap = get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to clear items at (%d,%d) but the submap is not loaded", l.x, l.y );
        return;
    }


    current_submap->set_lum( l, 0 );
    current_submap->get_items( l ).clear();
}

std::vector<item *> map::spawn_items( const tripoint &p, const std::vector<item> &new_items )
{
    std::vector<item *> ret;
    if( !inbounds( p ) || has_flag( ter_furn_flag::TFLAG_DESTROY_ITEM, p ) ) {
        return ret;
    }
    const bool swimmable = has_flag( ter_furn_flag::TFLAG_SWIMMABLE, p );
    for( const item &new_item : new_items ) {

        if( new_item.made_of( phase_id::LIQUID ) && swimmable ) {
            continue;
        }
        item &it = add_item_or_charges( p, new_item );
        if( !it.is_null() ) {
            ret.push_back( &it );
        }
    }

    return ret;
}

std::vector<item *> map::spawn_items( const tripoint_bub_ms &p, const std::vector<item> &new_items )
{
    return spawn_items( p.raw(), new_items );
}

void map::spawn_artifact( const tripoint &p, const relic_procgen_id &id,
                          const int max_attributes,
                          const int power_level, const int max_negative_power, const bool is_resonant )
{
    relic_procgen_data::generation_rules rules;
    rules.max_attributes = max_attributes;
    rules.power_level = power_level;
    rules.max_negative_power = max_negative_power;
    rules.resonant = is_resonant;

    add_item_or_charges( p, id->create_item( rules ) );
}

void map::spawn_item( const tripoint &p, const itype_id &type_id, const unsigned quantity,
                      const int charges, const time_point &birthday, const int damlevel, const std::set<flag_id> &flags,
                      const std::string &variant, const std::string &faction )
{
    if( type_id.is_null() ) {
        return;
    }

    if( item_is_blacklisted( type_id ) ) {
        return;
    }
    // recurse to spawn (quantity - 1) items
    for( size_t i = 1; i < quantity; i++ ) {
        spawn_item( p, type_id, 1, charges, birthday, damlevel, flags, variant, faction );
    }
    // migrate and spawn the item
    itype_id mig_type_id = item_controller->migrate_id( type_id );
    item new_item( mig_type_id, birthday );
    new_item.set_itype_variant( variant );
    new_item.set_owner( faction_id( faction ) );
    if( one_in( 3 ) && new_item.has_flag( flag_VARSIZE ) ) {
        new_item.set_flag( flag_FIT );
    }

    if( charges && new_item.charges > 0 ) {
        //let's fail silently if we specify charges for an item that doesn't support it
        new_item.charges = charges;
    }
    new_item = new_item.in_its_container();
    new_item.set_owner( faction_id( faction ) ); // Set faction to the container as well
    if( ( new_item.made_of( phase_id::LIQUID ) && has_flag( ter_furn_flag::TFLAG_SWIMMABLE, p ) ) ||
        has_flag( ter_furn_flag::TFLAG_DESTROY_ITEM, p ) ) {
        return;
    }

    new_item.set_damage( damlevel );
    new_item.rand_degradation();
    for( const flag_id &flag : flags ) {
        new_item.set_flag( flag );
    }

    add_item_or_charges( p, new_item );
}

units::volume map::max_volume( const tripoint &p )
{
    return i_at( p ).max_volume();
}

// total volume of all the things
units::volume map::stored_volume( const tripoint &p )
{
    return i_at( p ).stored_volume();
}

// free space
units::volume map::free_volume( const tripoint &p )
{
    return i_at( p ).free_volume();
}

units::volume map::free_volume( const tripoint_bub_ms &p )
{
    return free_volume( p.raw() );
}

item &map::add_item_or_charges( const tripoint &pos, item obj, bool overflow )
{
    // Checks if item would not be destroyed if added to this tile
    auto valid_tile = [&]( const tripoint & e ) {
        if( !inbounds( e ) ) {
            dbg( D_INFO ) << e; // should never happen
            return false;
        }

        // Some tiles destroy items (e.g. lava)
        if( has_flag( ter_furn_flag::TFLAG_DESTROY_ITEM, e ) ) {
            return false;
        }

        // Cannot drop liquids into tiles that are comprised of liquid
        if( obj.made_of_from_type( phase_id::LIQUID ) && has_flag( ter_furn_flag::TFLAG_SWIMMABLE, e ) ) {
            return false;
        }

        return true;
    };

    // Checks if sufficient space at tile to add item
    auto valid_limits = [&]( const tripoint & e ) {
        return obj.volume() <= free_volume( e ) && i_at( e ).size() < MAX_ITEM_IN_SQUARE;
    };

    // Performs the actual insertion of the object onto the map
    auto place_item = [&]( const tripoint & tile ) -> item& {
        if( obj.count_by_charges() )
        {
            for( item &e : i_at( tile ) ) {
                if( e.merge_charges( obj ) ) {
                    return e;
                }
            }
        }

        support_dirty( tile );
        return add_item( tile, obj );
    };

    if( item_is_blacklisted( obj.typeId() ) ) {
        return null_item_reference();
    }

    // Some items never exist on map as a discrete item (must be contained by another item)
    if( obj.has_flag( flag_NO_DROP ) ) {
        return null_item_reference();
    }

    // If intended drop tile destroys the item then we don't attempt to overflow
    if( !valid_tile( pos ) ) {
        return null_item_reference();
    }

    if( ( !has_flag( ter_furn_flag::TFLAG_NOITEM, pos ) ||
          ( has_flag( ter_furn_flag::TFLAG_LIQUIDCONT, pos ) && obj.made_of( phase_id::LIQUID ) ) )
        && valid_limits( pos ) ) {
        // Pass map into on_drop, because this map may not be the global map object (in mapgen, for instance).
        if( obj.made_of( phase_id::LIQUID ) || !obj.has_flag( flag_DROP_ACTION_ONLY_IF_LIQUID ) ) {
            if( obj.on_drop( pos, *this ) ) {
                return null_item_reference();
            }

        }
        // If tile can contain items place here...
        return place_item( pos );

    } else if( overflow ) {
        // ...otherwise try to overflow to adjacent tiles (if permitted)
        const int max_dist = 2;
        std::vector<tripoint> tiles = closest_points_first( pos, max_dist );
        tiles.erase( tiles.begin() ); // we already tried this position
        const int max_path_length = 4 * max_dist;
        const pathfinding_settings setting( 0, max_dist, max_path_length, 0, false, true, false, false,
                                            false );
        for( const tripoint &e : tiles ) {
            if( !inbounds( e ) ) {
                continue;
            }
            //must be a path to the target tile
            if( route( pos, e, setting ).empty() ) {
                continue;
            }
            if( obj.made_of( phase_id::LIQUID ) || !obj.has_flag( flag_DROP_ACTION_ONLY_IF_LIQUID ) ) {
                if( obj.on_drop( e, *this ) ) {
                    return null_item_reference();
                }
            }

            if( !valid_tile( e ) || !valid_limits( e ) ||
                has_flag( ter_furn_flag::TFLAG_NOITEM, e ) || has_flag( ter_furn_flag::TFLAG_SEALED, e ) ) {
                continue;
            }
            return place_item( e );
        }
    }

    // failed due to lack of space at target tile (+/- overflow tiles)
    return null_item_reference();
}

item &map::add_item_or_charges( const tripoint_bub_ms &pos, item obj, bool overflow )
{
    return add_item_or_charges( pos.raw(), std::move( obj ), overflow );
}

item &map::add_item( const tripoint &p, item new_item )
{
    if( item_is_blacklisted( new_item.typeId() ) ) {
        return null_item_reference();
    }

    if( !inbounds( p ) ) {
        return null_item_reference();
    }
    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to add items at (%d,%d) but the submap is not loaded", l.x, l.y );
        return null_item_reference();
    }

    // Process foods and temperature tracked items when they are added to the map, here instead of add_item_at()
    // to avoid double processing food during active item processing.
    if( new_item.has_temperature() && !new_item.is_corpse() ) {
        new_item.process( *this, nullptr, p );
    }

    if( new_item.made_of( phase_id::LIQUID ) && has_flag( ter_furn_flag::TFLAG_SWIMMABLE, p ) ) {
        return null_item_reference();
    }

    if( has_flag( ter_furn_flag::TFLAG_DESTROY_ITEM, p ) ) {
        return null_item_reference();
    }

    if( new_item.has_flag( flag_ACT_IN_FIRE ) && get_field( p, fd_fire ) != nullptr ) {
        if( new_item.has_flag( flag_BOMB ) && new_item.is_transformable() ) {
            //Convert a bomb item into its transformable version, e.g. incendiary grenade -> active incendiary grenade
            new_item.convert( dynamic_cast<const iuse_transform *>
                              ( new_item.type->get_use( "transform" )->get_actor_ptr() )->target );
        }
        new_item.active = true;
    }

    if( new_item.is_map() && !new_item.has_var( "reveal_map_center_omt" ) ) {
        new_item.set_var( "reveal_map_center_omt", ms_to_omt_copy( getabs( p ) ) );
    }

    if( new_item.has_flag( json_flag_PRESERVE_SPAWN_OMT ) &&
        !new_item.has_var( "spawn_location_omt" ) ) {
        new_item.set_var( "spawn_location_omt", ms_to_omt_copy( getabs( p ) ) );
    }
    for( item *const it : new_item.all_items_top( item_pocket::pocket_type::CONTAINER ) ) {
        if( it->has_flag( json_flag_PRESERVE_SPAWN_OMT ) &&
            !it->has_var( "spawn_location_omt" ) ) {
            it->set_var( "spawn_location_omt", ms_to_omt_copy( getabs( p ) ) );
        }
    }

    if( new_item.has_flag( flag_ACTIVATE_ON_PLACE ) ) {
        new_item.activate();
    }

    current_submap->ensure_nonuniform();
    invalidate_max_populated_zlev( p.z );

    current_submap->update_lum_add( l, new_item );

    const map_stack::iterator new_pos = current_submap->get_items( l ).insert( new_item );
    if (current_submap->active_items.add(*new_pos, l)) {
        // TODO: fix point types
        submaps_with_active_items_dirty.insert(tripoint_abs_sm(abs_sub.x() + p.x / SEEX,
            abs_sub.y() + p.y / SEEY, p.z));
    }

    return *new_pos;
}

// NOLINTNEXTLINE(readability-make-member-function-const)
item map::water_from( const tripoint &p )
{
    weather_manager &weather = get_weather();
    if( has_flag( ter_furn_flag::TFLAG_SALT_WATER, p ) ) {
        item ret( "salt_water", calendar::turn, item::INFINITE_CHARGES );
        ret.set_item_temperature( std::max( weather.get_temperature( p ),
                                            temperatures::cold ) );
        return ret;
    }

    if( has_flag( ter_furn_flag::TFLAG_CHOCOLATE, p ) ) {
        item ret( "liquid_cacao", calendar::turn, item::INFINITE_CHARGES );
        ret.set_item_temperature( std::max( weather.get_temperature( p ),
                                            temperatures::cold ) );
        return ret;
    }

    if( has_flag( ter_furn_flag::TFLAG_MURKY, p ) ) {
        for( item &ret : get_map().i_at( p ) ) {
            if( ret.made_of( phase_id::LIQUID ) ) {
                ret.set_item_temperature( std::max( weather.get_temperature( p ),
                                                    temperatures::cold ) );
                ret.poison = rng( 1, 6 );
                ret.get_comestible()->parasites = 5;
                ret.get_comestible()->contamination = { { disease_bad_food, 5 } };
                return ret;
            }
        }
    }

    if( has_flag( ter_furn_flag::TFLAG_TOILET_WATER, p ) ) {
        for( item &ret : get_map().i_at( p ) ) {
            if( ret.made_of( phase_id::LIQUID ) ) {
                ret.set_item_temperature( std::max( weather.get_temperature( p ),
                                                    temperatures::cold ) );
                ret.poison = one_in( 3 ) ? 0 : rng( 1, 3 );
                return ret;
            }
        }
    }

    const ter_id terrain_id = ter( p );
    if( terrain_id == t_sewage ) {
        item ret( "water_sewage", calendar::turn, item::INFINITE_CHARGES );
        ret.set_item_temperature( std::max( weather.get_temperature( p ),
                                            temperatures::cold ) );
        ret.poison = rng( 1, 7 );
        return ret;
    }

    item ret( "water", calendar::turn, item::INFINITE_CHARGES );
    ret.set_item_temperature( std::max( weather.get_temperature( p ),
                                        temperatures::cold ) );
    // iexamine::water_source requires a valid liquid from this function.
    if( terrain_id->has_examine( iexamine::water_source ) ) {
        int poison_chance = 0;
        if( terrain_id.obj().has_flag( ter_furn_flag::TFLAG_DEEP_WATER ) ) {
            if( terrain_id.obj().has_flag( ter_furn_flag::TFLAG_CURRENT ) ) {
                poison_chance = 20;
            } else {
                poison_chance = 4;
            }
        } else {
            if( terrain_id.obj().has_flag( ter_furn_flag::TFLAG_CURRENT ) ) {
                poison_chance = 10;
            } else {
                poison_chance = 3;
            }
        }
        if( one_in( poison_chance ) ) {
            ret.poison = rng( 1, 4 );
        }
        return ret;
    }
    if( furn( p )->has_examine( iexamine::water_source ) ) {
        return ret;
    }
    return item();
}

void map::make_active( item_location &loc )
{
    item *target = loc.get_item();

    point l;
    submap *const current_submap = get_submap_at( loc.position(), l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to make active at (%d,%d) but the submap is not loaded", l.x, l.y );
        return;
    }
    cata::colony<item> &item_stack = current_submap->get_items( l );
    cata::colony<item>::iterator iter = item_stack.get_iterator_from_pointer( target );

    if (current_submap->active_items.add(*iter, l)) {
        // TODO: fix point types
        submaps_with_active_items_dirty.insert(tripoint_abs_sm(abs_sub.x() + loc.position().x / SEEX,
            abs_sub.y() + loc.position().y / SEEY, loc.position().z));
    }

}

void map::update_lum( item_location &loc, bool add )
{
    item *target = loc.get_item();

    // if the item is not emissive, do nothing
    if( !target->is_emissive() ) {
        return;
    }

    point l;
    submap *const current_submap = get_submap_at( loc.position(), l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to update lum at (%d,%d) but the submap is not loaded", l.x, l.y );
        return;
    }

    if( add ) {
        current_submap->update_lum_add( l, *target );
    } else {
        current_submap->update_lum_rem( l, *target );
    }
}

static bool process_map_items( map &here, item_stack &items, safe_reference<item> &item_ref,
    item* parent, const tripoint& location, const float insulation,
    const temperature_flag flag, const float spoil_multiplier)
{
    if (item_ref->process(here, nullptr, location, insulation, flag, spoil_multiplier, false)) {
        // Item is to be destroyed so erase it from the map stack
        // unless it was already destroyed by processing.
        if( item_ref ) {
            item_ref->spill_contents(location);
            if (parent != nullptr) {
                parent->remove_item(*item_ref);
            }
            else {
                items.erase(items.get_iterator_from_pointer(item_ref.get()));
            }
        }
        return true;
    }
    // Item not destroyed
    return false;
}

static void process_vehicle_items( vehicle &cur_veh, int part )
{
    bool washing_machine_finished = false;

    const bool washer_here = cur_veh.is_part_on( part ) &&
                             ( cur_veh.part_flag( part, VPFLAG_WASHING_MACHINE ) ||
                               cur_veh.part_flag( part, VPFLAG_DISHWASHER ) );

    if( washer_here ) {
        for( item &n : cur_veh.get_items( part ) ) {
            const time_duration washing_time = 90_minutes;
            const time_duration time_left = washing_time - n.age();
            if( time_left <= 0_turns ) {
                n.unset_flag( flag_FILTHY );
                washing_machine_finished = true;
                cur_veh.part( part ).enabled = false;
            } else if( calendar::once_every( 15_minutes ) ) {
                //~ %1$d: Number of minutes remaining, %2$s: Name of the vehicle
                add_msg( _( "It should take %1$d minutes to finish washing items in the %2$s." ),
                         to_minutes<int>( time_left ) + 1, cur_veh.name );
                break;
            }
        }
        if( washing_machine_finished && !cur_veh.part_flag( part, VPFLAG_APPLIANCE ) ) {
            //~ %1$s: Cleaner, %2$s: Name of the vehicle
            add_msg( _( "The %1$s in the %2$s has finished washing." ), cur_veh.part( part ).name( false ),
                     cur_veh.name );
        } else if( washing_machine_finished ) {
            add_msg( _( "The %1$s has finished washing." ), cur_veh.part( part ).name( false ) );
        }
    }

    const bool autoclave_here = cur_veh.part_flag( part, VPFLAG_AUTOCLAVE ) &&
                                cur_veh.is_part_on( part );
    bool autoclave_finished = false;
    if( autoclave_here ) {
        for( item &n : cur_veh.get_items( part ) ) {
            const time_duration cycle_time = 90_minutes;
            const time_duration time_left = cycle_time - n.age();
            if( time_left <= 0_turns ) {
                if( !n.has_flag( flag_NO_PACKED ) ) {
                    n.unset_flag( flag_NO_STERILE );
                }
                autoclave_finished = true;
                cur_veh.part( part ).enabled = false;
            } else if( calendar::once_every( 15_minutes ) ) {
                const int minutes = to_minutes<int>( time_left ) + 1;
                //~ %1$d: Number of minutes remaining, %2$s: Name of the vehicle
                add_msg( n_gettext( "It should take %1$d minute to finish sterilizing items in the %2$s.",
                                    "It should take %1$d minutes to finish sterilizing items in the %2$s.", minutes ),
                         minutes, cur_veh.name );
                break;
            }
        }
        if( autoclave_finished && !cur_veh.part_flag( part, VPFLAG_APPLIANCE ) ) {
            add_msg( _( "The autoclave in the %s has finished its cycle." ), cur_veh.name );
        } else if( autoclave_finished ) {
            add_msg( _( "The autoclave has finished its cycle." ) );
        }
    }

    const int recharge_part_idx = cur_veh.part_with_feature( part, VPFLAG_RECHARGE, true );
    if( recharge_part_idx >= 0 ) {
        vehicle_part recharge_part = cur_veh.part( recharge_part_idx );
        if( !recharge_part.removed && !recharge_part.is_broken() && recharge_part.enabled ) {
            for( item &n : cur_veh.get_items( part ) ) {
                if( !n.has_flag( flag_RECHARGE ) && !n.has_flag( flag_USE_UPS ) ) {
                    continue;
                }
                // TODO: BATTERIES this should be rewritten when vehicle power and items both use energy quantities
                if( n.ammo_capacity( ammo_battery ) > n.ammo_remaining() ||
                    ( n.type->battery && n.type->battery->max_capacity > n.energy_remaining() ) ) {
                    int power = recharge_part.info().bonus;
                    while( power >= 1000 || x_in_y( power, 1000 ) ) {
                        const int missing = cur_veh.discharge_battery( 1, true );
                        // Around 85% efficient; a few of the discharges don't actually recharge
                        if( missing == 0 && !one_in( 7 ) ) {
                            if( n.is_vehicle_battery() ) {
                                n.mod_energy( 1_kJ );
                            } else {
                                n.ammo_set( itype_battery, n.ammo_remaining() + 1 );
                            }
                        }
                        power -= 1000;
                    }
                    break;
                }
            }
        }
    }
}

void map::process_items()
{
    const int minz = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    const int maxz = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    for( int gz = minz; gz <= maxz; ++gz ) {
        level_cache &cache = access_cache( gz );
        std::set<tripoint> submaps_with_vehicles;
        for( vehicle *this_vehicle : cache.vehicle_list ) {
            tripoint pos = this_vehicle->global_pos3();
            submaps_with_vehicles.emplace( pos.x / SEEX, pos.y / SEEY, pos.z );
        }
        for( const tripoint &pos : submaps_with_vehicles ) {
            submap *const current_submap = get_submap_at_grid( pos );
            if( current_submap == nullptr ) {
                debugmsg( "Tried to process items at (%d,%d,%d) but the submap is not loaded", pos.x, pos.y,
                          pos.z );
                continue;
            }
            // Vehicles first in case they get blown up and drop active items on the map.
            process_items_in_vehicles( *current_submap );
        }
    }
    update_submaps_with_active_items();
    for (auto iter = submaps_with_active_items.begin(); iter != submaps_with_active_items.end(); ) {
        tripoint_abs_sm const abs_pos = *iter;
        if (!inbounds(project_to<coords::ms>(abs_pos))) {
            iter = submaps_with_active_items.erase(iter);
            continue;
        }
        const tripoint_rel_sm local_pos = abs_pos - abs_sub.xy();
        // TODO: fix point types
        submap *const current_submap = get_submap_at_grid( local_pos.raw() );
        if( current_submap == nullptr ) {
            debugmsg( "Tried to process items at %s but the submap is not loaded",
                      local_pos.to_string() );
            continue;
        }
        // TODO: fix point types
        process_items_in_submap(*current_submap, local_pos.raw());
        if (current_submap->active_items.empty()) {
            iter = submaps_with_active_items.erase(iter);
        }
        else {
            ++iter;
        }
    }
}

void map::process_items_in_submap( submap &current_submap, const tripoint &gridp )
{
    // Get a COPY of the active item list for this submap.
    // If more are added as a side effect of processing, they are ignored this turn.
    // If they are destroyed before processing, they don't get processed.
    std::vector<item_reference> active_items = current_submap.active_items.get_for_processing();
    const point grid_offset( gridp.x * SEEX, gridp.y * SEEY );
    for( item_reference &active_item_ref : active_items ) {
        if( !active_item_ref.item_ref ) {
            // The item was destroyed, so skip it.
            continue;
        }

        const tripoint map_location = tripoint( grid_offset + active_item_ref.location, gridp.z );
        const furn_t &furn = this->furn( map_location ).obj();

        if( furn.has_flag( ter_furn_flag::TFLAG_DONT_REMOVE_ROTTEN ) ) {
            // plants contain a seed item which must not be removed under any circumstances.
            // Lets not process it at all.
            continue;
        }
        // root cellars are special
        temperature_flag flag = temperature_flag::NORMAL;
        if( ter( map_location ) == t_rootcellar ) {
            flag = temperature_flag::ROOT_CELLAR;
        }

        float spoil_multiplier = 1.0f;

        if( has_flag( ter_furn_flag::TFLAG_NO_SPOIL, map_location ) ) {
            spoil_multiplier = 0.0f;
        }

        map_stack items = i_at( map_location );

        process_map_items(*this, items, active_item_ref.item_ref, active_item_ref.parent,
            map_location, 1, flag,
            spoil_multiplier * active_item_ref.spoil_multiplier());
    }
}

// 待定 记得将它移动到一个类里，或者将它作为全局的函数
void split_(const std::string& source, const char d, std::set<std::string>& rst)
{
    if (!source.length()) return;
    rst.clear();
    for (int i = 0; i < source.length();)
    {
        std::string temp = "";
        while (true)
        {
            if (i >= source.length()) break;
            if (source[i] == d) break;
            else temp += source[i++];
        }
        rst.insert(temp);
        ++i;
    }
    if (source[source.length() - 1] == d) rst.insert("");
}

void map::process_items_in_vehicles( submap &current_submap )
{   


    creature_tracker& c_t = get_creature_tracker();
    tripoint new_p;
    Creature* c;
    Creature* c_new_p;

    vehicle* vehicle_new;
    std::string now_direction = "";


    // a copy, important if the vehicle list changes because a
    // vehicle got destroyed by a bomb (an active item!), this list
    // won't change, but veh_in_nonant will change.
    std::vector<vehicle *> vehicles;
    for( const auto &veh : current_submap.vehicles ) {
        vehicles.push_back( veh.get() );
    }
    for( vehicle *&cur_veh : vehicles ) {
        if( !current_submap.contains_vehicle( cur_veh ) ) {
            // vehicle not in the vehicle list of the nonant, has been
            // destroyed (or moved to another nonant?)
            // Can't be sure that it still exists, so skip it
            continue;
        }

        if (cur_veh->is_appliance()) {

            if (cur_veh->conveyor_belt_direction != "") {

                vehicle_part& first_part = cur_veh->part(0);
                tripoint& first_part_location = cur_veh->bub_part_pos(first_part).raw();

                if (first_part.enabled == true) {


                    if (cur_veh->conveyor_belt_direction == "向东运输") {

                        new_p = first_part_location;
                        new_p.x++;

                    }
                    else if (cur_veh->conveyor_belt_direction == "向西运输") {

                        new_p = first_part_location;
                        new_p.x--;

                    }
                    else if (cur_veh->conveyor_belt_direction == "向南运输") {

                        new_p = first_part_location;
                        new_p.y++;

                    }
                    else {

                        new_p = first_part_location;
                        new_p.y--;

                    }


                    // 先处理物品
                    for (item& i : cur_veh->get_items(0)) {

                        if (processed_conveyor_belt_item_set.find(&i) == processed_conveyor_belt_item_set.end()) {

                            if (&i != nullptr) {

                                const optional_vpart_position vp_there_new = veh_at(new_p);


                                if (!vp_there_new) {
                                    
                                    processed_conveyor_belt_item_set.insert(&add_item_or_charges(new_p, i));

                                }
                                else {

                                    vehicle_new = &(vp_there_new->vehicle());
                                    
                                    if (vehicle_new->is_appliance() && ( vehicle_new->part(0).info().has_flag("CONVEYOR_BELT")
                                        || vehicle_new->part(0).info().has_flag("CLASSIFIED_DEVICE") ) ) {
                                        
                                        processed_conveyor_belt_item_set.insert(&(vehicle_new->add_item_new(0, i)));

                                    }
                                    else {
                                    
                                        processed_conveyor_belt_item_set.insert(&add_item_or_charges(new_p, i));
                                    
                                    }
                                    
                                    
                                    

                                }


                                cur_veh->remove_item(0, &i);


                            }

                        }

                    }


                    // 再处理生物
                    c = c_t.creature_at(cur_veh->bub_part_pos(cur_veh->part(0)).raw());
                    if (c != nullptr && processed_conveyor_belt_creature_set.find(c) == processed_conveyor_belt_creature_set.end()) {

                        c_new_p = c_t.creature_at(new_p);
                        if (c_new_p == nullptr) {

                            c->setpos(new_p);
                            processed_conveyor_belt_creature_set.insert(c);


                        }

                    }


                }

            }
            
            else if (cur_veh->part(0).info().has_flag("CLASSIFIED_DEVICE")) {


                // 待定 将来有时间的时候，试着将这些集合中的元素存储到存档里
                if (cur_veh->has_set_the_rule == false) {

                    split_(cur_veh->rule_item_to_east, ',', cur_veh->item_to_east);
                    split_(cur_veh->rule_item_to_west, ',', cur_veh->item_to_west);
                    split_(cur_veh->rule_item_to_south, ',', cur_veh->item_to_south);
                    split_(cur_veh->rule_item_to_north, ',', cur_veh->item_to_north);

                    cur_veh->has_set_the_rule = true;
                
                }



                vehicle_part& first_part = cur_veh->part(0);
                tripoint& first_part_location = cur_veh->bub_part_pos(first_part).raw();
                std::set<tripoint> queue;
                std::vector<tripoint> queue_vec;
                
                if (first_part.enabled == true) {


                    if (!cur_veh->item_to_east.empty()) {
                        
                        tripoint temp_tripoint = first_part_location;
                        temp_tripoint.x++;
                        //queue.insert(temp_tripoint);
                        queue_vec.push_back(temp_tripoint);
                    
                    } 

                    if (!cur_veh->item_to_west.empty()) {
                    
                        tripoint temp_tripoint = first_part_location;
                        temp_tripoint.x--;
                        //queue.insert(temp_tripoint);
                        queue_vec.push_back(temp_tripoint);
                    
                    }

                    if (!cur_veh->item_to_south.empty()) {
                    
                        tripoint temp_tripoint = first_part_location;
                        temp_tripoint.y++;
                        //queue.insert(temp_tripoint);
                        queue_vec.push_back(temp_tripoint);
                    
                    }

                    if (!cur_veh->item_to_north.empty()) {
                    
                        tripoint temp_tripoint = first_part_location;
                        temp_tripoint.y--;
                        //queue.insert(temp_tripoint);
                        queue_vec.push_back(temp_tripoint);

                    }


                    for (const tripoint &t : queue_vec) {

                        for (item& i : cur_veh->get_items(0)) {

                            if (&i == nullptr) {

                                continue;
                            
                            }

                            if (t.x> first_part_location.x) {                                       
                                if (cur_veh->item_to_east.find(i.tname()) == cur_veh->item_to_east.end() && cur_veh->item_to_east.find("剩余的所有物品") == cur_veh->item_to_east.end()) {                              
                                    continue;
                                }
                           
                            } else if (t.x < first_part_location.x) {
                                if (cur_veh->item_to_west.find(i.tname()) == cur_veh->item_to_west.end() && cur_veh->item_to_west.find("剩余的所有物品") == cur_veh->item_to_west.end()) {
                                    continue;
                                }

                            }
                            else if (t.y > first_part_location.y) {
                                if (cur_veh->item_to_south.find(i.tname()) == cur_veh->item_to_south.end() && cur_veh->item_to_south.find("剩余的所有物品") == cur_veh->item_to_south.end()) {
                                    continue;
                                }
                            }
                             else{
                                if (cur_veh->item_to_north.find(i.tname()) == cur_veh->item_to_north.end() && cur_veh->item_to_north.find("剩余的所有物品") == cur_veh->item_to_north.end()) {
                                    continue;
                                }

                            }


                            if (processed_conveyor_belt_item_set.find(&i) == processed_conveyor_belt_item_set.end()) {

                                if (&i != nullptr) {

                                    const optional_vpart_position vp_there_new = veh_at(t);

                                    
                                    if (!vp_there_new) {

                                        processed_conveyor_belt_item_set.insert(&add_item_or_charges(t, i));

                                    }
                                    else {

                                        vehicle_new = &(vp_there_new->vehicle());

                                        if (vehicle_new->is_appliance() && (vehicle_new->part(0).info().has_flag("CONVEYOR_BELT")
                                            || vehicle_new->part(0).info().has_flag("CLASSIFIED_DEVICE"))) {

                                            processed_conveyor_belt_item_set.insert(&(vehicle_new->add_item_new(0, i)));

                                        }
                                        else {

                                            processed_conveyor_belt_item_set.insert(&add_item_or_charges(new_p, i));

                                        }

                                    }

                                    cur_veh->remove_item(0, &i);

                                }

                            }

                        }                                
                    }

                }




            
            
            }

        
        }

        process_items_in_vehicle( *cur_veh, current_submap );
    }


    processed_conveyor_belt_item_set.clear();
    processed_conveyor_belt_creature_set.clear();

}

void map::process_items_in_vehicle( vehicle &cur_veh, submap &current_submap )
{
    const bool engine_heater_is_on = cur_veh.has_part( "E_HEATER", true ) && cur_veh.engine_on;
    for( const vpart_reference &vp : cur_veh.get_any_parts( VPFLAG_FLUIDTANK ) ) {
        vp.part().process_contents( *this, vp.pos(), engine_heater_is_on );
    }

    auto cargo_parts = cur_veh.get_parts_including_carried( VPFLAG_CARGO );
    for( const vpart_reference &vp : cargo_parts ) {
        process_vehicle_items( cur_veh, vp.part_index() );
    }

    for( item_reference &active_item_ref : cur_veh.active_items.get_for_processing() ) {
        if( empty( cargo_parts ) ) {
            return;
        } else if( !active_item_ref.item_ref ) {
            // The item was destroyed, so skip it.
            continue;
        }
        const auto it = std::find_if( begin( cargo_parts ),
        end( cargo_parts ), [&]( const vpart_reference & part ) {
            return active_item_ref.location == part.mount();
        } );

        if( it == end( cargo_parts ) ) {
            continue; // Can't find a cargo part matching the active item.
        }
        const item &target = *active_item_ref.item_ref;
        // Find the cargo part and coordinates corresponding to the current active item.
        const vehicle_part &pt = it->part();
        const tripoint item_loc = it->pos();
        vehicle_stack items = cur_veh.get_items( static_cast<int>( it->part_index() ) );
        float it_insulation = 1.0f;
        temperature_flag flag = temperature_flag::NORMAL;
        if( target.has_temperature() || target.is_food_container() ) {
            const vpart_info &pti = pt.info();
            if( engine_heater_is_on ) {
                flag = temperature_flag::HEATER;
            }
            // some vehicle parts provide insulation, default is 1
            it_insulation = item::find_type( pti.base_item )->insulation_factor;

            if( pt.enabled && pti.has_flag( VPFLAG_FRIDGE ) ) {
                it_insulation = 1; // ignore fridge insulation if on
                flag = temperature_flag::FRIDGE;
            } else if( pt.enabled && pti.has_flag( VPFLAG_FREEZER ) ) {
                it_insulation = 1; // ignore freezer insulation if on
                flag = temperature_flag::FREEZER;
            } else if( pt.enabled && pti.has_flag( VPFLAG_HEATED_TANK ) ) {
                it_insulation = 1; // ignore tank insulation if on
                flag = temperature_flag::HEATER;
            }
        }
        if (!process_map_items(*this, items, active_item_ref.item_ref, active_item_ref.parent,
            item_loc, it_insulation, flag,
            active_item_ref.spoil_multiplier())) {
            // If the item was NOT destroyed, we can skip the remainder,
            // which handles fallout from the vehicle being damaged.
            continue;
        }

        // item does not exist anymore, might have been an exploding bomb,
        // check if the vehicle is still valid (does exist)
        if( !current_submap.contains_vehicle( &cur_veh ) ) {
            // Nope, vehicle is not in the vehicle list of the submap,
            // it might have moved to another submap (unlikely)
            // or be destroyed, anyway it does not need to be processed here
            return;
        }

        // Vehicle still valid, reload the list of cargo parts,
        // the list of cargo parts might have changed (imagine a part with
        // a low index has been removed by an explosion, all the other
        // parts would move up to fill the gap).
        cargo_parts = cur_veh.get_any_parts( VPFLAG_CARGO );
    }
}

// Crafting/item finding functions

// Note: this is called quite a lot when drawing tiles
// Console build has the most expensive parts optimized out
bool map::sees_some_items( const tripoint &p, const Creature &who ) const
{
    // Can only see items if there are any items.
    return has_items( p ) && could_see_items( p, who.pos() );
}

bool map::sees_some_items( const tripoint &p, const tripoint &from ) const
{
    return has_items( p ) && could_see_items( p, from );
}

bool map::could_see_items( const tripoint &p, const Creature &who ) const
{
    return could_see_items( p, who.pos() );
}

bool map::could_see_items( const tripoint &p, const tripoint &from ) const
{
    static const std::string container_string( "CONTAINER" );
    const bool container = has_flag_ter_or_furn( container_string, p );
    const bool sealed = has_flag_ter_or_furn( ter_furn_flag::TFLAG_SEALED, p );
    if( sealed && container ) {
        // never see inside of sealed containers
        return false;
    }
    if( container ) {
        // can see inside of containers if adjacent or
        // on top of the container
        return std::abs( p.x - from.x ) <= 1 &&
               std::abs( p.y - from.y ) <= 1 &&
               std::abs( p.z - from.z ) <= 1;
    }
    return true;
}

bool map::has_items( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return false;
    }

    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to check items at (%d,%d) but the submap is not loaded", l.x, l.y );
        return false;
    }

    return !current_submap->get_items( l ).empty();
}

bool map::has_items( const tripoint_bub_ms &p ) const
{
    return has_items( p.raw() );
}

bool map::only_liquid_in_liquidcont( const tripoint &p )
{
    if( has_flag( ter_furn_flag::TFLAG_LIQUIDCONT, p ) ) {
        for( const item &it : i_at( p ) ) {
            if( it.made_of( phase_id::SOLID ) ) {
                return false;
            }
        }
        return true;
    }
    return false;
}

template <typename Stack>
std::list<item> use_amount_stack( Stack stack, const itype_id &type, int &quantity,
                                  const std::function<bool( const item & )> &filter )
{
    std::list<item> ret;
    for( auto a = stack.begin(); a != stack.end() && quantity > 0; ) {
        if( a->use_amount( type, quantity, ret, filter ) ) {
            a = stack.erase( a );
        } else {
            ++a;
        }
    }
    return ret;
}

std::list<item> map::use_amount_square( const tripoint &p, const itype_id &type,
                                        int &quantity, const std::function<bool( const item & )> &filter )
{
    std::list<item> ret;
    // Handle infinite map sources.
    item water = water_from( p );
    if( water.typeId() == type && water.charges == item::INFINITE_CHARGES ) {
        ret.push_back( water );
        quantity = 0;
        return ret;
    }

    if( const std::optional<vpart_reference> vp = veh_at( p ).part_with_feature( "CARGO", true ) ) {
        std::list<item> tmp = use_amount_stack( vp->vehicle().get_items( vp->part_index() ), type,
                                                quantity, filter );
        ret.splice( ret.end(), tmp );
    }
    std::list<item> tmp = use_amount_stack( i_at( p ), type, quantity, filter );
    ret.splice( ret.end(), tmp );
    return ret;
}

std::list<item_location> map::items_with( const tripoint &p,
        const std::function<bool( const item & )> &filter )
{
    std::list<item_location> ret;
    if( const std::optional<vpart_reference> vp = veh_at( p ).part_with_feature( "CARGO", true ) ) {
        for( item &it : vp->vehicle().get_items( vp->part_index() ) ) {
            if( filter( it ) ) {
                ret.emplace_back( vehicle_cursor( vp->vehicle(), vp->part_index() ), &it );
            }
        }
    }
    for( item &it : i_at( p ) ) {
        if( filter( it ) ) {
            ret.emplace_back( map_cursor( p ), &it );
        }
    }
    return ret;
}

std::list<item> map::use_amount( const tripoint &origin, const int range, const itype_id &type,
                                 int &quantity, const std::function<bool( const item & )> &filter, bool select_ind )
{
    std::list<item> ret;
    if( select_ind && !type->count_by_charges() ) {
        std::vector<item_location> locs;
        for( const tripoint &p : points_in_radius( origin, range ) ) {
            std::list<item_location> tmp = items_with( p, [&filter, &type]( const item & it ) -> bool {
                return filter( it ) && it.typeId() == type;
            } );
            locs.insert( locs.end(), tmp.begin(), tmp.end() );
        }
        while( quantity != static_cast<int>( locs.size() ) && quantity > 0 && !locs.empty() ) {
            uilist imenu;
            //~ Select components from the map to consume. %d = number of components left to consume.
            imenu.title = string_format( _( "Select which component to use (%d left)" ), quantity );
            for( const item_location &loc : locs ) {
                imenu.addentry( loc->display_name() + " (" + loc.describe() + ")" );
            }
            imenu.query();
            if( imenu.ret < 0 || static_cast<size_t>( imenu.ret ) >= locs.size() ) {
                break;
            }
            locs[imenu.ret]->use_amount( type, quantity, ret, filter );
            locs[imenu.ret].remove_item();
            locs.erase( locs.begin() + imenu.ret );
        }
    }
    for( int radius = 0; radius <= range && quantity > 0; radius++ ) {
        for( const tripoint &p : points_in_radius( origin, radius ) ) {
            if( rl_dist( origin, p ) >= radius ) {
                std::list<item> tmp = use_amount_square( p, type, quantity, filter );
                ret.splice( ret.end(), tmp );
            }
        }
    }
    return ret;
}

static void use_charges_from_furn( const furn_t &f, const itype_id &type, int &quantity,
                                   map *m, const tripoint &p, std::list<item> &ret,
                                   const std::function<bool( const item & )> &filter, bool in_tools )
{
    if( m->has_flag( ter_furn_flag::TFLAG_LIQUIDCONT, p ) ) {
        map_stack item_list = m->i_at( p );
        auto current_item = item_list.begin();
        for( ; current_item != item_list.end(); ++current_item ) {
            // looking for a liquid that matches
            if( filter( *current_item ) && current_item->made_of( phase_id::LIQUID ) &&
                type == current_item->typeId() ) {
                ret.push_back( *current_item );
                if( current_item->charges - quantity > 0 ) {
                    // Update the returned liquid amount to match the requested amount
                    ret.back().charges = quantity;
                    // Update the liquid item in the world to contain the leftover liquid
                    current_item->charges -= quantity;
                    // All the liquid needed was found, no other sources will be needed
                    quantity = 0;
                } else {
                    // The liquid copy in ret already contains how much was available
                    // The leftover quantity returned will check other sources
                    quantity -= current_item->charges;
                    // Remove liquid item from the world
                    item_list.erase( current_item );
                }
                return;
            }
        }
    }

    const itype *itt = f.crafting_pseudo_item_type();
    if( itt != nullptr && itt->tool && !itt->tool->ammo_id.empty() ) {
        const itype_id ammo = ammotype( *itt->tool->ammo_id.begin() )->default_ammotype();
        const bool using_ammotype = f.has_flag( ter_furn_flag::TFLAG_AMMOTYPE_RELOAD );
        map_stack stack = m->i_at( p );
        auto iter = std::find_if( stack.begin(), stack.end(),
        [ammo, using_ammotype]( const item & i ) {
            if( using_ammotype && i.type->ammo && ammo->ammo ) {
                return i.type->ammo->type == ammo->ammo->type;
            } else {
                return i.typeId() == ammo;
            }
        } );
        if( iter != stack.end() ) {
            item furn_item( itt, calendar::turn_zero );
            furn_item.ammo_set( ammo, iter->charges );

            if( !filter( furn_item ) ) {
                return;
            }
            if( furn_item.use_charges( type, quantity, ret, p, return_true<item>, nullptr, in_tools ) ) {
                stack.erase( iter );
            } else {
                iter->charges = furn_item.ammo_remaining();
            }
        }
    }
}

std::list<item> map::use_charges( const tripoint &origin, const int range,
                                  const itype_id &type, int &quantity,
                                  const std::function<bool( const item & )> &filter,
                                  basecamp *bcp, bool in_tools )
{
    std::list<item> ret;

    // populate a grid of spots that can be reached
    std::vector<tripoint> reachable_pts;
    reachable_flood_steps( reachable_pts, origin, range, 1, 100 );

    // We prefer infinite map sources where available, so search for those
    // first
    for( const tripoint &p : reachable_pts ) {
        // Handle infinite map sources.
        item water = water_from( p );
        if( water.typeId() == type && water.charges == item::INFINITE_CHARGES ) {
            water.charges = quantity;
            ret.push_back( water );
            quantity = 0;
            return ret;
        }
    }

    if( bcp ) {
        ret = bcp->use_charges( type, quantity );
        if( quantity <= 0 ) {
            return ret;
        }
    }

    for( const tripoint &p : reachable_pts ) {
        if( accessible_items( p ) ) {
            std::list<item> tmp = i_at( p ).use_charges( type, quantity, p, filter, in_tools );
            ret.splice( ret.end(), tmp );
            if( quantity <= 0 ) {
                return ret;
            }
        }

        if( has_furn( p ) ) {
            use_charges_from_furn( furn( p ).obj(), type, quantity, this, p, ret, filter, in_tools );
            if( quantity <= 0 ) {
                return ret;
            }
        }

        const optional_vpart_position vp = veh_at( p );
        if( vp ) {
            std::list<item> tmp = vp->vehicle().use_charges( *vp, type, quantity, filter, in_tools );
            ret.splice( ret.end(), tmp );
            if( quantity <= 0 ) {
                return ret;
            }
        }
    }

    return ret;
}

units::energy map::consume_ups( const tripoint &origin, const int range, units::energy qty )
{
    const units::energy wanted_qty = qty;
    int qty_kj = units::to_kilojoule( qty );

    // populate a grid of spots that can be reached
    std::vector<tripoint> reachable_pts;
    reachable_flood_steps( reachable_pts, origin, range, 1, 100 );

    for( const tripoint &p : reachable_pts ) {
        if( accessible_items( p ) ) {

            map_stack items = i_at( p );
            for( item &elem : items ) {
                if( elem.has_flag( flag_IS_UPS ) ) {
                    qty_kj -=  elem.ammo_consume( qty_kj, p, nullptr );
                    if( qty_kj == 0 ) {
                        break;
                    }
                }
            }
        }
    }

    return wanted_qty - units::from_kilojoule( qty_kj );
}

std::list<std::pair<tripoint, item *> > map::get_rc_items( const tripoint &p )
{
    std::list<std::pair<tripoint, item *> > rc_pairs;
    tripoint pos;
    pos.z = abs_sub.z();
    for( pos.x = 0; pos.x < MAPSIZE_X; pos.x++ ) {
        if( p.x != -1 && p.x != pos.x ) {
            continue;
        }
        for( pos.y = 0; pos.y < MAPSIZE_Y; pos.y++ ) {
            if( p.y != -1 && p.y != pos.y ) {
                continue;
            }
            map_stack items = i_at( pos );
            for( item &elem : items ) {
                if( elem.has_flag( flag_RADIO_ACTIVATION ) || elem.has_flag( flag_RADIO_CONTAINER ) ) {
                    rc_pairs.emplace_back( pos, &elem );
                }
            }
        }
    }

    return rc_pairs;
}

bool map::can_see_trap_at( const tripoint &p, const Character &c ) const
{
    return tr_at( p ).can_see( p, c );
}

const trap &map::tr_at( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return tr_null.obj();
    }

    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to get trap at (%d,%d) but the submap is not loaded", l.x, l.y );
        return tr_null.obj();
    }

    const trap_id &builtin_trap = current_submap->get_ter( l )->trap;
    if( builtin_trap != tr_null ) {
        return *builtin_trap;
    }

    return current_submap->get_trap( l ).obj();
}

const trap &map::tr_at( const tripoint_bub_ms &p ) const
{
    return tr_at( p.raw() );
}

partial_con *map::partial_con_at( const tripoint_bub_ms &p )
{
    if( !inbounds( p ) ) {
        return nullptr;
    }
    point_sm_ms l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to get construction at %s but the submap is not loaded", l.to_string() );
        return nullptr;
    }
    auto it = current_submap->partial_constructions.find( tripoint_sm_ms( l, p.z() ) );
    if( it != current_submap->partial_constructions.end() ) {
        return &it->second;
    }
    return nullptr;
}

void map::partial_con_remove( const tripoint_bub_ms &p )
{
    if( !inbounds( p ) ) {
        return;
    }
    point_sm_ms l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to remove construction at %s but the submap is not loaded", l.to_string() );
        return;
    }
    current_submap->partial_constructions.erase( tripoint_sm_ms( l, p.z() ) );
}

void map::partial_con_set( const tripoint_bub_ms &p, const partial_con &con )
{
    if( !inbounds( p ) ) {
        return;
    }
    point_sm_ms l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to set construction at %s but the submap is not loaded", l.to_string() );
        return;
    }
    if( !current_submap->partial_constructions.emplace( tripoint_sm_ms( l, p.z() ), con ).second ) {
        debugmsg( "set partial con on top of terrain which already has a partial con" );
    }
}

void map::trap_set( const tripoint &p, const trap_id &type )
{
    if( !inbounds( p ) ) {
        return;
    }

    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to set trap at %s but the submap is not loaded", l.to_string() );
        return;
    }
    const ter_t &ter = current_submap->get_ter( l ).obj();
    if( ter.trap != tr_null ) {
        debugmsg( "set trap %s (%s) at %s on top of terrain %s (%s) which already has a "
                  "built-in trap", type.id().str(), type->name(), p.to_string(),
                  ter.id.str(), ter.name() );
        return;
    }

    // If there was already a trap here, remove it.
    if( current_submap->get_trap( l ) != tr_null ) {
        remove_trap( p );
    }

    current_submap->set_trap( l, type );
    if( type != tr_null ) {
        traplocs[type.to_i()].push_back( p );
    }
}

void map::trap_set( const tripoint_bub_ms &p, const trap_id &type )
{
    trap_set( p.raw(), type );
}

void map::remove_trap( const tripoint &p )
{
    if( !inbounds( p ) ) {
        return;
    }

    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to remove trap at (%d,%d) but the submap is not loaded", l.x, l.y );
        return;
    }

    trap_id tid = current_submap->get_trap( l );
    if( tid != tr_null ) {
        if( g != nullptr && this == &get_map() ) {
            get_player_character().add_known_trap( p, tr_null.obj() );
        }

        current_submap->set_trap( l, tr_null );
        auto &traps = traplocs[tid.to_i()];
        const auto iter = std::find( traps.begin(), traps.end(), p );
        if( iter != traps.end() ) {
            traps.erase( iter );
        }
    }
}

void map::remove_trap( const tripoint_bub_ms &p )
{
    remove_trap( p.raw() );
}

/*
 * Get wrapper for all fields at xyz
 */
const field &map::field_at( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        nulfield = field();
        return nulfield;
    }

    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to get field at (%d,%d) but the submap is not loaded", l.x, l.y );
        nulfield = field();
        return nulfield;
    }

    return current_submap->get_field( l );
}

/*
 * As above, except not const
 */
field &map::field_at( const tripoint &p )
{
    if( !inbounds( p ) ) {
        nulfield = field();
        return nulfield;
    }

    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to get field at (%d,%d,%d) but the submap is not loaded", p.x, p.y, p.z );
        nulfield = field();
        return nulfield;
    }

    return current_submap->get_field( l );
}

field &map::field_at( const tripoint_bub_ms &p )
{
    return field_at( p.raw() );
}

time_duration map::mod_field_age( const tripoint &p, const field_type_id &type,
                                  const time_duration &offset )
{
    return set_field_age( p, type, offset, true );
}

int map::mod_field_intensity( const tripoint &p, const field_type_id &type, const int offset )
{
    return set_field_intensity( p, type, offset, true );
}

time_duration map::set_field_age( const tripoint &p, const field_type_id &type,
                                  const time_duration &age, const bool isoffset )
{
    if( field_entry *const field_ptr = get_field( p, type ) ) {
        return field_ptr->set_field_age( ( isoffset ? field_ptr->get_field_age() : 0_turns ) + age );
    }
    return -1_turns;
}

/*
 * set intensity of field type at point, creating if not present, removing if intensity is 0
 * returns resulting intensity, or 0 for not present
 */
int map::set_field_intensity( const tripoint &p, const field_type_id &type,
                              const int new_intensity,
                              bool isoffset )
{
    field_entry *field_ptr = get_field( p, type );
    if( field_ptr != nullptr ) {
        int adj = ( isoffset && field_ptr->is_field_alive() ?
                    field_ptr->get_field_intensity() : 0 ) + new_intensity;
        on_field_modified( p, *type );
        field_ptr->set_field_intensity( adj );
        return adj;
    } else if( new_intensity > 0 ) {
        return add_field( p, type, new_intensity ) ? new_intensity : 0;
    }

    return 0;
}

time_duration map::get_field_age( const tripoint &p, const field_type_id &type ) const
{
    const field_entry *field_ptr = field_at( p ).find_field( type );
    return field_ptr == nullptr ? -1_turns : field_ptr->get_field_age();
}

time_duration map::get_field_age( const tripoint_bub_ms &p, const field_type_id &type ) const
{
    return get_field_age( p.raw(), type );
}

int map::get_field_intensity( const tripoint &p, const field_type_id &type ) const
{
    const field_entry *field_ptr = get_field( p, type );
    return field_ptr == nullptr ? 0 : field_ptr->get_field_intensity();
}

bool map::has_field_at( const tripoint &p, bool check_bounds ) const
{
    const tripoint sm = ms_to_sm_copy( p );
    return ( !check_bounds || inbounds( p ) ) && get_cache( p.z ).field_cache[sm.x + sm.y * MAPSIZE];
}

bool map::has_field_at( const tripoint &p, const field_type_id &type ) const
{
    return get_field( p, type ) != nullptr;
}

template<typename Map>
cata::copy_const<Map, field_entry> *map::get_field_helper(
    Map &m, const tripoint &p, const field_type_id &type )
{
    if( !m.inbounds( p ) || !m.has_field_at( p, false ) ) {
        return nullptr;
    }

    point l;
    auto *const current_submap = m.unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to get field at (%d,%d) but the submap is not loaded", l.x, l.y );
        return nullptr;
    }

    return current_submap->get_field( l ).find_field( type );
}

field_entry *map::get_field( const tripoint &p, const field_type_id &type )
{
    return get_field_helper( *this, p, type );
}

field_entry *map::get_field( const tripoint_bub_ms &p, const field_type_id &type )
{
    return get_field( p.raw(), type );
}

const field_entry *map::get_field( const tripoint &p, const field_type_id &type ) const
{
    return get_field_helper( *this, p, type );
}

const field_entry *map::get_field( const tripoint_bub_ms &p, const field_type_id &type ) const
{
    return get_field( p.raw(), type );
}

bool map::dangerous_field_at( const tripoint &p )
{
    for( auto &pr : field_at( p ) ) {
        field_entry &fd = pr.second;
        if( fd.is_dangerous() ) {
            return true;
        }
    }
    return false;
}

bool map::dangerous_field_at( const tripoint_bub_ms &p )
{
    return dangerous_field_at( p.raw() );
}

bool map::mopsafe_field_at( const tripoint &p )
{
    for( const std::pair<const int_id<field_type>, field_entry> &pr : field_at( p ) ) {
        const field_entry &fd = pr.second;
        if( !fd.is_mopsafe() ) {
            return false;
        }
    }
    return true;
}

bool map::add_field( const tripoint &p, const field_type_id &type_id, int intensity,
                     const time_duration &age, bool hit_player )
{
    if( !inbounds( p ) ) {
        return false;
    }

    if( !type_id ) {
        return false;
    }

    // Don't spawn non-gaseous fields on open air
    if( has_flag( ter_furn_flag::TFLAG_NO_FLOOR, p ) && type_id.obj().phase != phase_id::GAS ) {
        return false;
    }

    // Don't spawn liquid fields on water tiles
    if( has_flag( ter_furn_flag::TFLAG_SWIMMABLE, p ) && type_id.obj().phase == phase_id::LIQUID ) {
        return false;
    }

    // Hacky way to force electricity fields to become unlit electricity fields
    const field_type_id &converted_type_id = ( type_id == fd_electricity ||
            type_id == fd_electricity_unlit ) ? get_applicable_electricity_field( p ) : type_id;
    const field_type &fd_type = *converted_type_id;

    intensity = std::min( intensity, fd_type.get_max_intensity() );
    if( intensity <= 0 ) {
        return false;
    }

    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to add field at (%d,%d) but the submap is not loaded", l.x, l.y );
        return false;
    }
    current_submap->ensure_nonuniform();
    invalidate_max_populated_zlev( p.z );

    if( current_submap->get_field( l ).add_field( converted_type_id, intensity, age ) ) {
        //Only adding it to the count if it doesn't exist.
        if( !current_submap->field_count++ ) {
            get_cache( p.z ).field_cache.set(
                static_cast<size_t>( p.x / SEEX ) + ( ( p.y / SEEX ) * MAPSIZE ) );
        }
    }

    if( hit_player ) {
        Character &player_character = get_player_character();
        if( g != nullptr && this == &get_map() && p == player_character.pos() ) {
            //Hit the player with the field if it spawned on top of them.
            creature_in_field( player_character );
        }
    }

    on_field_modified( p, fd_type );

    return true;
}

bool map::add_field( const tripoint_bub_ms &p, const field_type_id &type_id, int intensity,
                     const time_duration &age, bool hit_player )
{
    return add_field( p.raw(), type_id, intensity, age, hit_player );
}

void map::remove_field( const tripoint &p, const field_type_id &field_to_remove )
{
    set_field_intensity( p, field_to_remove, 0 );
}

void map::remove_field( const tripoint_bub_ms &p, const field_type_id &field_to_remove )
{
    return remove_field( p.raw(), field_to_remove );
}

void map::clear_fields( const tripoint &p )
{
    if( !inbounds( p ) ) {
        return;
    }

    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    current_submap->clear_fields( l );
}

void map::on_field_modified( const tripoint &p, const field_type &fd_type )
{
    invalidate_max_populated_zlev( p.z );

    get_cache( p.z ).field_cache.set(
        static_cast<size_t>( p.x / SEEX ) + ( ( p.y / SEEX ) * MAPSIZE ) );

    // Dirty the transparency cache now that field processing doesn't always do it
    if( fd_type.dirty_transparency_cache || !fd_type.is_transparent() ) {
        set_transparency_cache_dirty( p, true );
        set_seen_cache_dirty( p );
    }

    if( fd_type.is_dangerous() ) {
        set_pathfinding_cache_dirty( p.z );
    }

    // Ensure blood type fields don't hang in the air
    if( zlevels && fd_type.accelerated_decay ) {
        support_dirty( p );
    }
}

void map::add_splatter( const field_type_id &type, const tripoint &where, int intensity )
{
    if( intensity <= 0 ) {
        return;
    }
    if( type.obj().is_splattering ) {
        if( const optional_vpart_position vp = veh_at( where ) ) {
            vehicle *const veh = &vp->vehicle();
            // Might be -1 if all the vehicle's parts at where are marked for removal
            const int part = veh->part_displayed_at( vp->mount(), true );
            if( part != -1 ) {
                veh->part( part ).blood += 200 * std::min( intensity, 3 ) / 3;
                return;
            }
        }
    }
    mod_field_intensity( where, type, intensity );
}

void map::add_splatter_trail( const field_type_id &type, const tripoint &from,
                              const tripoint &to )
{
    if( !type.id() ) {
        return;
    }
    const auto trail = line_to( from, to );
    int remainder = trail.size();
    for( const tripoint &elem : trail ) {
        add_splatter( type, elem );
        remainder--;
        if( impassable( elem ) ) { // Blood splatters stop at walls.
            add_splatter( type, elem, remainder );
            return;
        }
    }
}

void map::add_splash( const field_type_id &type, const tripoint &center, int radius,
                      int intensity )
{
    if( !type.id() ) {
        return;
    }
    // TODO: use Bresenham here and take obstacles into account
    for( const tripoint &pnt : points_in_radius( center, radius ) ) {
        if( trig_dist( pnt, center ) <= radius && !one_in( intensity ) ) {
            add_splatter( type, pnt );
        }
    }
}

computer *map::computer_at( const tripoint &p )
{
    if( !inbounds( p ) ) {
        return nullptr;
    }

    point l;
    submap *const sm = unsafe_get_submap_at( p, l );
    if( sm == nullptr ) {
        debugmsg( "Tried to get computer at (%d,%d) but the submap is not loaded", l.x, l.y );
        return nullptr;
    }
    return sm->get_computer( l );
}

bool map::point_within_camp( const tripoint &point_check ) const
{
    // TODO: fix point types
    const tripoint_abs_omt omt_check( ms_to_omt_copy( point_check ) );
    const point_abs_omt p = omt_check.xy();
    for( int x2 = -2; x2 < 2; x2++ ) {
        for( int y2 = -2; y2 < 2; y2++ ) {
            if( std::optional<basecamp *> bcp = overmap_buffer.find_camp( p + point( x2, y2 ) ) ) {
                return ( *bcp )->point_within_camp( omt_check );
            }
        }
    }
    return false;
}

void map::remove_submap_camp( const tripoint &p )
{
    submap *const current_submap = get_submap_at( p );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to remove camp at (%d,%d,%d) but the submap is not loaded", p.x, p.y, p.z );
        return;
    }
    current_submap->camp.reset();
}

basecamp map::hoist_submap_camp( const tripoint &p )
{
    submap *const current_submap = get_submap_at( p );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to hoist camp at (%d,%d,%d) but the submap is not loaded", p.x, p.y, p.z );
        return basecamp();
    }
    basecamp *pcamp = current_submap->camp.get();
    return pcamp ? *pcamp : basecamp();
}

void map::add_camp( const tripoint_abs_omt &omt_pos, const std::string &name )
{
    basecamp temp_camp = basecamp( name, omt_pos );
    overmap_buffer.add_camp( temp_camp );
    get_player_character().camps.insert( omt_pos );
    g->validate_camps();
}

void map::update_submaps_with_active_items()
{
    std::move(submaps_with_active_items_dirty.begin(), submaps_with_active_items_dirty.end(),
        std::inserter(submaps_with_active_items, submaps_with_active_items.begin()));
    submaps_with_active_items_dirty.clear();
}

void map::update_visibility_cache( const int zlev )
{
    Character &player_character = get_player_character();
    visibility_variables_cache.variables_set = true; // Not used yet
    visibility_variables_cache.g_light_level = static_cast<int>( g->light_level( zlev ) );
    visibility_variables_cache.vision_threshold = player_character.get_vision_threshold(
                get_cache_ref(
                    player_character.posz() ).lm[player_character.posx()][player_character.posy()].max() );

    visibility_variables_cache.u_clairvoyance = player_character.clairvoyance();
    visibility_variables_cache.u_sight_impaired = player_character.sight_impaired();
    visibility_variables_cache.u_is_boomered = player_character.has_effect( effect_boomered );
    visibility_variables_cache.clairvoyance_field.reset();
    if( field_fd_clairvoyant.is_valid() ) {
        visibility_variables_cache.clairvoyance_field = field_fd_clairvoyant;
    }

    cata::mdarray<int, point_bub_sm> sm_squares_seen = {};

    auto &visibility_cache = get_cache( zlev ).visibility_cache;

    tripoint p;
    p.z = zlev;
    int &x = p.x;
    int &y = p.y;
    for( x = 0; x < MAPSIZE_X; x++ ) {
        for( y = 0; y < MAPSIZE_Y; y++ ) {
            lit_level ll = apparent_light_at( p, visibility_variables_cache );
            visibility_cache[x][y] = ll;
            sm_squares_seen[ x / SEEX ][ y / SEEY ] += ( ll == lit_level::BRIGHT || ll == lit_level::LIT );
        }
    }

    for( int gridx = 0; gridx < my_MAPSIZE; gridx++ ) {
        for( int gridy = 0; gridy < my_MAPSIZE; gridy++ ) {
            if( sm_squares_seen[gridx][gridy] > 36 ) { // 25% of the submap is visible
                const tripoint sm( gridx, gridy, 0 );
                const tripoint_abs_sm abs_sm = map::abs_sub + sm;
                const tripoint_abs_omt abs_omt = project_to<coords::omt>( abs_sm );
                overmap_buffer.set_seen( abs_omt, true );
            }
        }
    }
}

const visibility_variables &map::get_visibility_variables_cache() const
{
    return visibility_variables_cache;
}

visibility_type map::get_visibility( const lit_level ll,
                                     const visibility_variables &cache ) const
{
    switch( ll ) {
        case lit_level::DARK:
            // can't see this square at all
            if( cache.u_is_boomered ) {
                return visibility_type::BOOMER_DARK;
            } else {
                return visibility_type::DARK;
            }
        case lit_level::BRIGHT_ONLY:
            // can only tell that this square is bright
            if( cache.u_is_boomered ) {
                return visibility_type::BOOMER;
            } else {
                return visibility_type::LIT;
            }

        case lit_level::LOW:
        // low light, square visible in monochrome
        case lit_level::LIT:
        // normal light
        case lit_level::BRIGHT:
            // bright light
            return visibility_type::CLEAR;
        case lit_level::BLANK:
        case lit_level::MEMORIZED:
            return visibility_type::HIDDEN;
    }
    return visibility_type::HIDDEN;
}

static bool has_memory_at( const tripoint &p )
{
    avatar &you = get_avatar();
    if( you.should_show_map_memory() ) {
        int t = you.get_memorized_symbol( get_map().getabs( p ) );
        return t != 0;
    }
    return false;
}

static int get_memory_at( const tripoint &p )
{
    avatar &you = get_avatar();
    if( you.should_show_map_memory() ) {
        return you.get_memorized_symbol( get_map().getabs( p ) );
    }
    return ' ';
}

void map::draw( const catacurses::window &w, const tripoint &center )
{
    // We only need to draw anything if we're not in tiles mode.
    if( is_draw_tiles_mode() ) {
        return;
    }

    g->reset_light_level();

    update_visibility_cache( center.z );
    const visibility_variables &cache = get_visibility_variables_cache();

    const auto &visibility_cache = get_cache_ref( center.z ).visibility_cache;

    int wnd_h = getmaxy( w );
    int wnd_w = getmaxx( w );
    const tripoint offs = center - tripoint( wnd_w / 2, wnd_h / 2, 0 );

    // Map memory should be at least the size of the view range
    // so that new tiles can be memorized, and at least the size of the terminal
    // since displayed area may be bigger than view range.
    const point min_mm_reg = point(
                                 std::min( 0, offs.x ),
                                 std::min( 0, offs.y )
                             );
    const point max_mm_reg = point(
                                 std::max( MAPSIZE_X, offs.x + wnd_w ),
                                 std::max( MAPSIZE_Y, offs.y + wnd_h )
                             );
    avatar &player_character = get_avatar();
    player_character.prepare_map_memory_region(
        getabs( tripoint( min_mm_reg, center.z ) ),
        getabs( tripoint( max_mm_reg, center.z ) )
    );

    const auto draw_background = [&]( const tripoint & p ) {
        int sym = ' ';
        nc_color col = c_black;
        if( has_memory_at( p ) ) {
            sym = get_memory_at( p );
            col = c_brown;
        }
        wputch( w, col, sym );
    };

    const auto draw_vision_effect = [&]( const visibility_type vis ) -> bool {
        int sym = '#';
        nc_color col;
        switch( vis )
        {
            case visibility_type::LIT:
                // can only tell that this square is bright
                col = c_light_gray;
                break;
            case visibility_type::BOOMER:
                col = c_pink;
                break;
            case visibility_type::BOOMER_DARK:
                col = c_magenta;
                break;
            default:
                return false;
        }
        wputch( w, col, sym );
        return true;
    };

    drawsq_params params = drawsq_params().memorize( true );
    for( int wy = 0; wy < wnd_h; wy++ ) {
        for( int wx = 0; wx < wnd_w; wx++ ) {
            wmove( w, point( wx, wy ) );
            const tripoint p = offs + tripoint( wx, wy, 0 );
            if( !inbounds( p ) ) {
                draw_background( p );
                continue;
            }

            const lit_level lighting = visibility_cache[p.x][p.y];
            const visibility_type vis = get_visibility( lighting, cache );

            if( draw_vision_effect( vis ) ) {
                continue;
            }

            if( vis == visibility_type::HIDDEN || vis == visibility_type::DARK ) {
                draw_background( p );
                continue;
            }

            const const_maptile curr_maptile = maptile_at_internal( p );
            params
            .low_light( lighting == lit_level::LOW )
            .bright_light( lighting == lit_level::BRIGHT );
            if( draw_maptile( w, p, curr_maptile, params ) ) {
                continue;
            }
            const maptile tile_below = maptile_at_internal( p + tripoint_below );
            draw_from_above( w, tripoint( p.xy(), p.z - 1 ), tile_below, params );
        }
    }

    // Memorize off-screen tiles
    half_open_rectangle<point> display( offs.xy(), offs.xy() + point( wnd_w, wnd_h ) );
    drawsq_params mm_params = drawsq_params().memorize( true ).output( false );
    for( int y = 0; y < MAPSIZE_Y; y++ ) {
        for( int x = 0; x < MAPSIZE_X; x++ ) {
            const tripoint p( x, y, center.z );
            if( display.contains( p.xy() ) ) {
                // Have been memorized during display loop
                continue;
            }

            const lit_level lighting = visibility_cache[p.x][p.y];
            const visibility_type vis = get_visibility( lighting, cache );

            if( vis != visibility_type::CLEAR ) {
                continue;
            }

            const maptile curr_maptile = maptile_at_internal( p );
            mm_params
            .low_light( lighting == lit_level::LOW )
            .bright_light( lighting == lit_level::BRIGHT );

            draw_maptile( w, p, curr_maptile, mm_params );
        }
    }
}

void map::drawsq( const catacurses::window &w, const tripoint &p,
                  const drawsq_params &params ) const
{
    // If we are in tiles mode, the only thing we want to potentially draw is a highlight
    if( is_draw_tiles_mode() ) {
        if( params.highlight() ) {
            g->draw_highlight( p );
        }
        return;
    }

    if( !inbounds( p ) ) {
        return;
    }

    const tripoint view_center = params.center();
    const int k = p.x + getmaxx( w ) / 2 - view_center.x;
    const int j = p.y + getmaxy( w ) / 2 - view_center.y;
    wmove( w, point( k, j ) );

    const const_maptile tile = maptile_at( p );
    if( draw_maptile( w, p, tile, params ) ) {
        return;
    }

    tripoint below( p.xy(), p.z - 1 );
    const const_maptile tile_below = maptile_at( below );
    draw_from_above( w, below, tile_below, params );
}

// a check to see if the lower floor needs to be rendered in tiles
bool map::dont_draw_lower_floor( const tripoint &p ) const
{
    return !zlevels || p.z <= -OVERMAP_DEPTH ||
           !( has_flag( ter_furn_flag::TFLAG_NO_FLOOR, p ) ||
              has_flag( ter_furn_flag::TFLAG_Z_TRANSPARENT, p ) );
}

bool map::draw_maptile( const catacurses::window &w, const tripoint &p,
                        const const_maptile &curr_maptile, const drawsq_params &params ) const
{
    drawsq_params param = params;
    nc_color tercol;
    const ter_t &curr_ter = curr_maptile.get_ter_t();
    const furn_t &curr_furn = curr_maptile.get_furn_t();
    const trap &curr_trap = curr_maptile.get_trap().obj();
    const field &curr_field = curr_maptile.get_field();
    int sym;
    int memory_sym;
    bool hi = false;
    bool graf = false;
    bool draw_item_sym = false;

    if( curr_ter.has_flag( ter_furn_flag::TFLAG_AUTO_WALL_SYMBOL ) ) {
        memory_sym = sym = determine_wall_corner( p );
        tercol = curr_ter.color();
    } else {
        memory_sym = sym = curr_ter.symbol();
        tercol = curr_ter.color();
    }

    avatar &player_character = get_avatar();
    if( curr_furn.id ) {
        sym = curr_furn.symbol();
        tercol = curr_furn.color();
        if( !( player_character.get_grab_type() == object_type::FURNITURE
               && p == player_character.pos() + player_character.grab_point ) ) {
            memory_sym = sym;
        }
    }
    if( curr_ter.has_flag( ter_furn_flag::TFLAG_SWIMMABLE ) &&
        curr_ter.has_flag( ter_furn_flag::TFLAG_DEEP_WATER ) &&
        !player_character.is_underwater() ) {
        param.show_items( false ); // Can only see underwater items if WE are underwater
    }
    // If there's a trap here, and we have sufficient perception, draw that instead
    if( curr_trap.can_see( p, player_character ) ) {
        tercol = curr_trap.color;
        if( curr_trap.sym == '%' ) {
            switch( rng( 1, 5 ) ) {
                case 1:
                    memory_sym = sym = '*';
                    break;
                case 2:
                    memory_sym = sym = '0';
                    break;
                case 3:
                    memory_sym = sym = '8';
                    break;
                case 4:
                    memory_sym = sym = '&';
                    break;
                case 5:
                    memory_sym = sym = '+';
                    break;
            }
        } else {
            memory_sym = sym = curr_trap.sym;
        }
    }
    // FIXME: fix point type
    if( const_cast<map *>( this )->partial_con_at( tripoint_bub_ms( p ) ) != nullptr ) {
        tercol = tr_unfinished_construction->color;
        memory_sym = sym = tr_unfinished_construction->sym;
    }
    if( curr_field.field_count() > 0 ) {
        const field_type_id &fid = curr_field.displayed_field_type();
        const field_entry *fe = curr_field.find_field( fid );
        const auto field_symbol = fid->get_symbol();
        if( field_symbol == "&" || fe == nullptr ) {
            // Do nothing, a '&' indicates invisible fields.
        } else if( field_symbol == "*" ) {
            // A random symbol.
            switch( rng( 1, 5 ) ) {
                case 1:
                    memory_sym = sym = '*';
                    break;
                case 2:
                    memory_sym = sym = '0';
                    break;
                case 3:
                    memory_sym = sym = '8';
                    break;
                case 4:
                    memory_sym = sym = '&';
                    break;
                case 5:
                    memory_sym = sym = '+';
                    break;
            }
        } else {
            // A field symbol '%' indicates the field should not hide
            // items/terrain. When the symbol is not '%' it will
            // hide items (the color is still inverted if there are items,
            // but the tile symbol is not changed).
            // draw_item_sym indicates that the item symbol should be used
            // even if sym is not '.'.
            // As we don't know at this stage if there are any items
            // (that are visible to the player!), we always set the symbol.
            // If there are items and the field does not hide them,
            // the code handling items will override it.
            draw_item_sym = ( field_symbol == "'%" );
            // If field display_priority is > 1, and the field is set to hide items,
            //draw the field as it obscures what's under it.
            if( ( field_symbol != "%" && fid.obj().priority > 1 ) || ( field_symbol != "%" &&
                    sym == '.' ) )  {
                // default terrain '.' and
                // non-default field symbol -> field symbol overrides terrain
                memory_sym = sym = static_cast<uint8_t>( field_symbol[0] );
            }
            tercol = fe->color();
        }
    }

    // TODO: change the local variable sym to std::string and use it instead of this hack.
    // Currently this are different variables because terrain/... uses int as symbol type and
    // item now use string. Ideally they should all be strings.
    std::string item_sym;

    // If there are items here, draw those instead
    if( param.show_items() && curr_maptile.get_item_count() > 0 &&
        sees_some_items( p, player_character ) ) {
        // if there's furniture/terrain/trap/fields (sym!='.')
        // and we should not override it, then only highlight the square
        if( sym != '.' && sym != '%' && !draw_item_sym ) {
            hi = true;
        } else {
            // otherwise override with the symbol of the last item
            item_sym = curr_maptile.get_uppermost_item().symbol();
            if( !draw_item_sym ) {
                tercol = curr_maptile.get_uppermost_item().color();
            }
            if( curr_maptile.get_item_count() > 1 ) {
                param.highlight( !param.highlight() );
            }
        }
    }

    int veh_part = 0;
    const vehicle *veh = veh_at_internal( p, veh_part );
    if( veh != nullptr ) {
        sym = special_symbol( veh->face.dir_symbol( veh->part_sym( veh_part ) ) );
        tercol = veh->part_color( veh_part );
        item_sym.clear(); // clear the item symbol so `sym` is used instead.

        if( !veh->forward_velocity() && !veh->player_in_control( player_character )
            && !( player_character.get_grab_type() == object_type::VEHICLE
                  && veh->get_points().count( player_character.pos() + player_character.grab_point ) ) ) {
            memory_sym = sym;
        }
    }

    if( param.memorize() && check_and_set_seen_cache( p ) ) {
        player_character.memorize_symbol( getabs( p ), memory_sym );
    }

    // If there's graffiti here, change background color
    if( curr_maptile.has_graffiti() ) {
        graf = true;
    }

    const auto u_vision = player_character.get_vision_modes();
    if( u_vision[BOOMERED] ) {
        tercol = c_magenta;
    } else if( u_vision[NV_GOGGLES] ) {
        tercol = param.bright_light() ? c_white : c_light_green;
    } else if( param.low_light() || u_vision[DARKNESS] ) {
        tercol = c_dark_gray;
    }

    if( param.highlight() ) {
        tercol = invert_color( tercol );
    } else if( hi ) {
        tercol = hilite( tercol );
    } else if( graf ) {
        tercol = red_background( tercol );
    }

    if( item_sym.empty() && sym == ' ' ) {
        if( !zlevels || p.z <= -OVERMAP_DEPTH || !curr_ter.has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) ) {
            // Print filler symbol
            tercol = c_black;
        } else {
            // Draw tile underneath this one instead
            return false;
        }
    }

    if( params.output() ) {
        if( item_sym.empty() ) {
            wputch( w, tercol, sym );
        } else {
            wprintz( w, tercol, item_sym );
        }
    }
    return true;
}

void map::draw_from_above( const catacurses::window &w, const tripoint &p,
                           const const_maptile &curr_tile, const drawsq_params &params ) const
{
    static const int AUTO_WALL_PLACEHOLDER = 2; // this should never appear as a real symbol!

    nc_color tercol = c_dark_gray;
    int sym = ' ';

    const ter_t &curr_ter = curr_tile.get_ter_t();
    const furn_t &curr_furn = curr_tile.get_furn_t();
    int part_below;
    const vehicle *veh;
    if( curr_furn.has_flag( ter_furn_flag::TFLAG_SEEN_FROM_ABOVE ) ) {
        sym = curr_furn.symbol();
        tercol = curr_furn.color();
    } else if( curr_furn.movecost < 0 ) {
        sym = '.';
        tercol = curr_furn.color();
    } else if( ( veh = veh_at_internal( p, part_below ) ) != nullptr ) {
        const int roof = veh->roof_at_part( part_below );
        const int displayed_part = roof >= 0 ? roof : part_below;
        sym = special_symbol( veh->face.dir_symbol( veh->part_sym( displayed_part, true ) ) );
        tercol = ( roof >= 0 ||
                   vpart_position( const_cast<vehicle &>( *veh ),
                                   part_below ).obstacle_at_part() ) ? c_light_gray : c_light_gray_cyan;
    } else if( curr_ter.has_flag( ter_furn_flag::TFLAG_SEEN_FROM_ABOVE ) ) {
        if( curr_ter.has_flag( ter_furn_flag::TFLAG_AUTO_WALL_SYMBOL ) ) {
            sym = AUTO_WALL_PLACEHOLDER;
        } else if( curr_ter.has_flag( ter_furn_flag::TFLAG_RAMP ) ) {
            sym = '>';
        } else {
            sym = curr_ter.symbol();
        }
        tercol = curr_ter.color();
    } else if( curr_ter.movecost == 0 ) {
        sym = '.';
        tercol = curr_ter.color();
    } else if( !curr_ter.has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) ) {
        sym = '.';
        if( curr_ter.color() != c_cyan ) {
            // Need a special case here, it doesn't cyanize well
            tercol = cyan_background( curr_ter.color() );
        } else {
            tercol = c_black_cyan;
        }
    } else {
        sym = curr_ter.symbol();
        tercol = curr_ter.color();
    }

    if( sym == AUTO_WALL_PLACEHOLDER ) {
        sym = determine_wall_corner( p );
    }

    const std::bitset<NUM_VISION_MODES> &u_vision = get_player_character().get_vision_modes();
    if( u_vision[BOOMERED] ) {
        tercol = c_magenta;
    } else if( u_vision[NV_GOGGLES] ) {
        tercol = params.bright_light() ? c_white : c_light_green;
    } else if( params.low_light() || u_vision[DARKNESS] ) {
        tercol = c_dark_gray;
    }

    if( params.highlight() ) {
        tercol = invert_color( tercol );
    }

    if( params.output() ) {
        wputch( w, tercol, sym );
    }
}

bool map::sees( const tripoint &F, const tripoint &T, const int range ) const
{
    int dummy = 0;
    return sees( F, T, range, dummy );
}

bool map::is_in_radius(const tripoint& center, const tripoint& target, int radius) {
    return (std::abs(center.x - target.x) <= radius &&
        std::abs(center.y - target.y) <= radius &&
        std::abs(center.z - target.z) <= radius);
}

/**
 * This one is internal-only, we don't want to expose the slope tweaking ickiness outside the map class.
 **/
bool map::sees( const tripoint &F, const tripoint &T, const int range,
                int &bresenham_slope ) const
{
    if( ( range >= 0 && range < rl_dist( F, T ) ) ||
        !inbounds( T ) ) {
        bresenham_slope = 0;
        return false; // Out of range!
    }
    // Canonicalize the order of the tripoints so the cache is reflexive.
    const tripoint &min = F < T ? F : T;
    const tripoint &max = !( F < T ) ? F : T;
    // A little gross, just pack the values into a point.
    const point key(
        min.x << 16 | min.y << 8 | ( min.z + OVERMAP_DEPTH ),
        max.x << 16 | max.y << 8 | ( max.z + OVERMAP_DEPTH )
    );
    char cached = skew_vision_cache.get( key, -1 );
    if( cached >= 0 ) {
        return cached > 0;
    }
    bool visible = true;

    // Ugly `if` for now
    if( F.z == T.z ) {
        bresenham( F.xy(), T.xy(), bresenham_slope,
        [this, &visible, &T]( const point & new_point ) {
            // Exit before checking the last square, it's still visible even if opaque.
            if( new_point.x == T.x && new_point.y == T.y ) {
                return false;
            }
            if( !this->is_transparent( tripoint( new_point, T.z ) ) ) {
                visible = false;
                return false;
            }
            return true;
        } );
        skew_vision_cache.insert( 100000, key, visible ? 1 : 0 );
        return visible;
    }

    tripoint last_point = F;
    bresenham( F, T, bresenham_slope, 0,
    [this, &visible, &T, &last_point]( const tripoint & new_point ) {
        // Exit before checking the last square if it's not a vertical transition,
        // it's still visible even if opaque.
        if( new_point == T && last_point.z == T.z ) {
            return false;
        }

        // TODO: Allow transparent floors (and cache them!)
        if( new_point.z == last_point.z ) {
            if( !this->is_transparent( new_point ) ) {
                visible = false;
                return false;
            }
        } else {
            const int max_z = std::max( new_point.z, last_point.z );
            if( ( has_floor_or_support( {new_point.xy(), max_z} ) ||
                  !is_transparent( {new_point.xy(), last_point.z} ) ) &&
                ( has_floor_or_support( {last_point.xy(), max_z} ) ||
                  !is_transparent( {last_point.xy(), new_point.z} ) ) ) {
                visible = false;
                return false;
            }
        }

        last_point = new_point;
        return true;
    } );
    skew_vision_cache.insert( 100000, key, visible ? 1 : 0 );
    return visible;
}

int map::obstacle_coverage( const tripoint &loc1, const tripoint &loc2 ) const
{
    // Can't hide if you are standing on furniture, or non-flat slowing-down terrain tile.
    if( furn( loc2 ).obj().id || ( move_cost( loc2 ) > 2 &&
                                   !has_flag_ter( ter_furn_flag::TFLAG_FLAT, loc2 ) ) ) {
        return 0;
    }
    const point a( std::abs( loc1.x - loc2.x ) * 2, std::abs( loc1.y - loc2.y ) * 2 );
    int offset = std::min( a.x, a.y ) - ( std::max( a.x, a.y ) / 2 );
    tripoint obstaclepos;
    bresenham( loc2, loc1, offset, 0, [&obstaclepos]( const tripoint & new_point ) {
        // Only adjacent tile between you and enemy is checked for cover.
        obstaclepos = new_point;
        return false;
    } );
    if( const auto obstacle_f = furn( obstaclepos ) ) {
        return obstacle_f->coverage;
    }
    if( const optional_vpart_position vp = veh_at( obstaclepos ) ) {
        if( vp->obstacle_at_part() ) {
            return 60;
        } else if( !vp->part_with_feature( VPFLAG_AISLE, true ) ) {
            return 45;
        }
    }
    return ter( obstaclepos )->coverage;
}

int map::coverage( const tripoint &p ) const
{
    if( const auto obstacle_f = furn( p ) ) {
        return obstacle_f->coverage;
    }
    if( const optional_vpart_position vp = veh_at( p ) ) {
        if( vp->obstacle_at_part() ) {
            return 60;
        } else if( !vp->part_with_feature( VPFLAG_AISLE, true ) ) {
            return 45;
        }
    }
    return ter( p )->coverage;
}

// This method tries a bunch of initial offsets for the line to try and find a clear one.
// Basically it does, "Find a line from any point in the source that ends up in the target square".
std::vector<tripoint> map::find_clear_path( const tripoint &source,
        const tripoint &destination ) const
{
    // TODO: Push this junk down into the Bresenham method, it's already doing it.
    const point d( destination.xy() - source.xy() );
    const point a( std::abs( d.x ) * 2, std::abs( d.y ) * 2 );
    const int dominant = std::max( a.x, a.y );
    const int minor = std::min( a.x, a.y );
    // This seems to be the method for finding the ideal start value for the error value.
    const int ideal_start_offset = minor - dominant / 2;
    const int start_sign = ( ideal_start_offset > 0 ) - ( ideal_start_offset < 0 );
    // Not totally sure of the derivation.
    const int max_start_offset = std::abs( ideal_start_offset ) * 2 + 1;
    for( int horizontal_offset = -1; horizontal_offset <= max_start_offset; ++horizontal_offset ) {
        int candidate_offset = horizontal_offset * start_sign;
        if( sees( source, destination, rl_dist( source, destination ), candidate_offset ) ) {
            return line_to( source, destination, candidate_offset, 0 );
        }
    }
    // If we couldn't find a clear LoS, just return the ideal one.
    return line_to( source, destination, ideal_start_offset, 0 );
}

void map::reachable_flood_steps( std::vector<tripoint> &reachable_pts, const tripoint &f,
                                 int range, const int cost_min, const int cost_max ) const
{
    struct pq_item {
        int dist;
        int ndx;
    };
    struct pq_item_comp {
        bool operator()( const pq_item &left, const pq_item &right ) {
            return left.dist > right.dist;
        }
    };
    using PQ_type = std::priority_queue< pq_item, std::vector<pq_item>, pq_item_comp>;

    // temp buffer for grid
    const int grid_dim = range * 2 + 1;
    // init to -1 as "not visited yet"
    std::vector< int > t_grid( static_cast<size_t>( grid_dim * grid_dim ), -1 );
    const tripoint origin_offset = {range, range, 0};
    const int initial_visit_distance = range * range; // Large unreachable value

    // Fill positions that are visitable with initial_visit_distance
    for( const tripoint &p : points_in_radius( f, range ) ) {
        const tripoint tp = { p.xy(), f.z };
        const int tp_cost = move_cost( tp );
        // rejection conditions
        if( tp_cost < cost_min || tp_cost > cost_max || ( !has_floor_or_support( tp ) && tp != f ) ) {
            continue;
        }
        // set initial cost for grid point
        tripoint origin_relative = tp - f;
        origin_relative += origin_offset;
        int ndx = origin_relative.x + origin_relative.y * grid_dim;
        t_grid[ ndx ] = initial_visit_distance;
    }

    auto gen_neighbors = []( const pq_item & elem, int grid_dim, std::array<pq_item, 8> &neighbors ) {
        // Up to 8 neighbors
        int new_cost = elem.dist + 1;
        // *INDENT-OFF*
        std::array<int, 8> ox = {
            -1, 0, 1,
            -1,    1,
            -1, 0, 1
        };
        std::array<int, 8> oy = {
            -1, -1, -1,
            0,      0,
            1,  1,  1
        };
        // *INDENT-ON*

        point e( elem.ndx % grid_dim, elem.ndx / grid_dim );
        for( int i = 0; i < 8; ++i ) {
            point n( e + point( ox[i], oy[i] ) );

            int ndx = n.x + n.y * grid_dim;
            neighbors[i] = { new_cost, ndx };
        }
    };

    PQ_type pq( pq_item_comp{} );
    pq_item first_item{ 0, range + range * grid_dim };
    pq.push( first_item );
    std::array<pq_item, 8> neighbor_elems;

    while( !pq.empty() ) {
        const pq_item item = pq.top();
        pq.pop();

        if( t_grid[ item.ndx ] == initial_visit_distance ) {
            t_grid[ item.ndx ] = item.dist;
            if( item.dist + 1 < range ) {
                gen_neighbors( item, grid_dim, neighbor_elems );
                for( pq_item neighbor_elem : neighbor_elems ) {
                    pq.push( neighbor_elem );
                }
            }
        }
    }
    std::vector<char> o_grid( static_cast<size_t>( grid_dim * grid_dim ), 0 );
    for( int y = 0, ndx = 0; y < grid_dim; ++y ) {
        for( int x = 0; x < grid_dim; ++x, ++ndx ) {
            if( t_grid[ ndx ] != -1 && t_grid[ ndx ] < initial_visit_distance ) {
                // set self and neighbors to 1
                for( int dy = -1; dy <= 1; ++dy ) {
                    for( int dx = -1; dx <= 1; ++dx ) {
                        point t2( dx + x, dy + y );

                        if( t2.x >= 0 && t2.x < grid_dim && t2.y >= 0 && t2.y < grid_dim ) {
                            o_grid[ t2.x + t2.y * grid_dim ] = 1;
                        }
                    }
                }
            }
        }
    }

    // Now go over again to pull out all of the reachable points
    for( int y = 0, ndx = 0; y < grid_dim; ++y ) {
        for( int x = 0; x < grid_dim; ++x, ++ndx ) {
            if( o_grid[ ndx ] ) {
                tripoint t = f - origin_offset + tripoint{ x, y, 0 };
                reachable_pts.push_back( t );
            }
        }
    }
}

bool map::clear_path( const tripoint &f, const tripoint &t, const int range,
                      const int cost_min, const int cost_max ) const
{

    if( f.z == t.z ) {
        if( ( range >= 0 && range < rl_dist( f.xy(), t.xy() ) ) ||
            !inbounds( t ) ) {
            return false; // Out of range!
        }
        bool is_clear = true;
        bresenham( f.xy(), t.xy(), 0,
        [this, &is_clear, cost_min, cost_max, &t]( const point & new_point ) {
            // Exit before checking the last square, it's still reachable even if it is an obstacle.
            if( new_point.x == t.x && new_point.y == t.y ) {
                return false;
            }

            const int cost = this->move_cost( new_point );
            if( cost < cost_min || cost > cost_max ) {
                is_clear = false;
                return false;
            }
            return true;
        } );
        return is_clear;
    }

    if( ( range >= 0 && range < rl_dist( f, t ) ) ||
        !inbounds( t ) ) {
        return false; // Out of range!
    }
    bool is_clear = true;
    tripoint last_point = f;
    bresenham( f, t, 0, 0,
    [this, &is_clear, cost_min, cost_max, t, &last_point]( const tripoint & new_point ) {
        // Exit before checking the last square, it's still reachable even if it is an obstacle.
        if( new_point == t ) {
            return false;
        }

        // We have to check a weird case where the move is both vertical and horizontal
        if( new_point.z == last_point.z ) {
            const int cost = move_cost( new_point );
            if( cost < cost_min || cost > cost_max ) {
                is_clear = false;
                return false;
            }
        } else {
            bool this_clear = false;
            const int max_z = std::max( new_point.z, last_point.z );
            if( !has_floor_or_support( {new_point.xy(), max_z} ) ) {
                const int cost = move_cost( {new_point.xy(), last_point.z} );
                if( cost > cost_min && cost < cost_max ) {
                    this_clear = true;
                }
            }

            if( !this_clear && has_floor_or_support( {last_point.xy(), max_z} ) ) {
                const int cost = move_cost( {last_point.xy(), new_point.z} );
                if( cost > cost_min && cost < cost_max ) {
                    this_clear = true;
                }
            }

            if( !this_clear ) {
                is_clear = false;
                return false;
            }
        }

        last_point = new_point;
        return true;
    } );
    return is_clear;
}

bool map::clear_path( const tripoint_bub_ms &f, const tripoint_bub_ms &t, const int range,
                      const int cost_min, const int cost_max ) const
{
    return clear_path( f.raw(), t.raw(), range, cost_min, cost_max );
}

bool map::accessible_items( const tripoint &t ) const
{
    return !has_flag( ter_furn_flag::TFLAG_SEALED, t ) ||
           has_flag( ter_furn_flag::TFLAG_LIQUIDCONT, t );
}

bool map::accessible_items( const tripoint_bub_ms &t ) const
{
    return accessible_items( t.raw() );
}

std::vector<tripoint> map::get_dir_circle( const tripoint &f, const tripoint &t ) const
{
    std::vector<tripoint> circle;
    circle.resize( 8 );

    // The line below can be crazy expensive - we only take the FIRST point of it
    const std::vector<tripoint> line = line_to( f, t, 0, 0 );
    const std::vector<tripoint> spiral = closest_points_first( f, 1 );
    const std::vector<int> pos_index {1, 2, 4, 6, 8, 7, 5, 3};

    //  All possible constellations (closest_points_first goes clockwise)
    //  753  531  312  124  246  468  687  875
    //  8 1  7 2  5 4  3 6  1 8  2 7  4 5  6 3
    //  642  864  786  578  357  135  213  421

    size_t pos_offset = 0;
    for( size_t i = 1; i < spiral.size(); i++ ) {
        if( spiral[i] == line[0] ) {
            pos_offset = i - 1;
            break;
        }
    }

    for( size_t i = 1; i < spiral.size(); i++ ) {
        if( pos_offset >= pos_index.size() ) {
            pos_offset = 0;
        }

        circle[pos_index[pos_offset++] - 1] = spiral[i];
    }

    return circle;
}

void map::save()
{
    for( int gridx = 0; gridx < my_MAPSIZE; gridx++ ) {
        for( int gridy = 0; gridy < my_MAPSIZE; gridy++ ) {
            if( zlevels ) {
                for( int gridz = -OVERMAP_DEPTH; gridz <= OVERMAP_HEIGHT; gridz++ ) {
                    saven( tripoint( gridx, gridy, gridz ) );
                }
            } else {
                saven( tripoint( gridx, gridy, abs_sub.z() ) );
            }
        }
    }
}

void map::load( const tripoint_abs_sm &w, const bool update_vehicle,
                const bool pump_events )
{
    // It used to be unsafe to load a map that overlaps with the primary map;
    // Show an info line in tests to help track new errors
    for( auto &traps : traplocs ) {
        traps.clear();
    }
    field_furn_locs.clear();
    field_ter_locs.clear();
    submaps_with_active_items.clear();
    submaps_with_active_items_dirty.clear();
    set_abs_sub( w );
    clear_vehicle_level_caches();
    for( int gridx = 0; gridx < my_MAPSIZE; gridx++ ) {
        for( int gridy = 0; gridy < my_MAPSIZE; gridy++ ) {
            loadn(point(gridx, gridy), update_vehicle);
            if( pump_events ) {
                inp_mngr.pump_events();
            }
        }
    }
    rebuild_vehicle_level_caches();

    // actualize after loading all submaps to prevent errors
    // with entities at the edges
    for( int gridx = 0; gridx < my_MAPSIZE; gridx++ ) {
        for( int gridy = 0; gridy < my_MAPSIZE; gridy++ ) {
            for( int gridz = -OVERMAP_DEPTH; gridz <= OVERMAP_HEIGHT; gridz++ ) {
                actualize( tripoint( point( gridx, gridy ), gridz ) );
                if( pump_events ) {
                    inp_mngr.pump_events();
                }
            }
        }
    }
}

void map::shift_traps( const tripoint &shift )
{
    // Offset needs to have sign opposite to shift direction
    const tripoint offset( -shift.x * SEEX, -shift.y * SEEY, -shift.z );
    for( auto iter = field_furn_locs.begin(); iter != field_furn_locs.end(); ) {
        tripoint &pos = *iter;
        pos += offset;
        if( inbounds( pos ) ) {
            ++iter;
        } else {
            iter = field_furn_locs.erase( iter );
        }
    }
    for( auto iter = field_ter_locs.begin(); iter != field_ter_locs.end(); ) {
        tripoint &pos = *iter;
        pos += offset;
        if( inbounds( pos ) ) {
            ++iter;
        } else {
            iter = field_ter_locs.erase( iter );
        }
    }
    for( auto &traps : traplocs ) {
        for( auto iter = traps.begin(); iter != traps.end(); ) {
            tripoint &pos = *iter;
            pos += offset;
            if( inbounds( pos ) ) {
                ++iter;
            } else {
                // Theoretical enhancement: if this is not the last entry of the vector,
                // move the last entry into pos and remove the last entry instead of iter.
                // This would avoid moving all the remaining entries.
                iter = traps.erase( iter );
            }
        }
    }
}

template<int SIZE, int MULTIPLIER>
void shift_bitset_cache( std::bitset<SIZE *SIZE> &cache, const point &s )
{
    // sx shifts by MULTIPLIER rows, sy shifts by MULTIPLIER columns.
    int shift_amount = s.x * MULTIPLIER + s.y * SIZE * MULTIPLIER;
    if( shift_amount > 0 ) {
        cache >>= static_cast<size_t>( shift_amount );
    } else if( shift_amount < 0 ) {
        cache <<= static_cast<size_t>( -shift_amount );
    }
    // Shifting in the y direction shifted in 0 values, no no additional clearing is necessary, but
    // a shift in the x direction makes values "wrap" to the next row, and they need to be zeroed.
    if( s.x == 0 ) {
        return;
    }
    const size_t x_offset = s.x > 0 ? SIZE - MULTIPLIER : 0;
    for( size_t y = 0; y < SIZE; ++y ) {
        size_t y_offset = y * SIZE;
        for( size_t x = 0; x < MULTIPLIER; ++x ) {
            cache.reset( y_offset + x_offset + x );
        }
    }
}

template void
shift_bitset_cache<MAPSIZE_X, SEEX>( std::bitset<MAPSIZE_X *MAPSIZE_X> &cache,
                                     const point &s );
template void
shift_bitset_cache<MAPSIZE, 1>( std::bitset<MAPSIZE *MAPSIZE> &cache, const point &s );

void map::shift( const point &sp )
{

    if (!zlevels) {
        debugmsg("map::shift called from map that doesn't support Z levels");
        return;
    }


    // Special case of 0-shift; refresh the map
    if( sp == point_zero ) {
        return; // Skip this?
    }

    if( std::abs( sp.x ) > 1 || std::abs( sp.y ) > 1 ) {
        debugmsg( "map::shift called with a shift of more than one submap" );
        return;
    }

    const tripoint_abs_sm abs = get_abs_sub();
    std::vector<tripoint> loaded_grids;

    // TODO: fix point types (sp should be relative?)
    set_abs_sub( abs + sp );

    g->shift_destination_preview( point( -sp.x * SEEX, -sp.y * SEEY ) );

    shift_traps( tripoint( sp, 0 ) );

    vehicle *remoteveh = g->remoteveh();

    const int zmin = -OVERMAP_DEPTH;
    const int zmax = OVERMAP_HEIGHT;
    for( int gridz = zmin; gridz <= zmax; gridz++ ) {
        level_cache *cache = get_cache_lazy( gridz );
        if( !cache ) {
            continue;
        }
        for( vehicle *veh : cache->vehicle_list ) {
            veh->zones_dirty = true;
        }
    }

    // Shift the map sx submaps to the right and sy submaps down.
    // sx and sy should never be bigger than +/-1.
    // absx and absy are our position in the world, for saving/loading purposes.
    clear_vehicle_level_caches();
    const int x_start = sp.x >= 0 ? 0 : my_MAPSIZE - 1;
    const int x_stop = sp.x >= 0 ? my_MAPSIZE : -1;
    const int x_step = sp.x >= 0 ? 1 : -1;
    const int y_start = sp.y >= 0 ? 0 : my_MAPSIZE - 1;
    const int y_stop = sp.y >= 0 ? my_MAPSIZE : -1;
    const int y_step = sp.y >= 0 ? 1 : -1;
    for( int gridz = zmin; gridz <= zmax; gridz++ ) {
        // Clear vehicle list and rebuild after shift
        // mlangsdorf 2020 - this is kind of insane, building the cache is not free, why are
        // we doing this?
        clear_vehicle_list( gridz );
        level_cache *cache = get_cache_lazy( gridz );
        if( cache ) {
            shift_bitset_cache<MAPSIZE_X, SEEX>( cache->map_memory_seen_cache, sp );
            shift_bitset_cache<MAPSIZE, 1>( cache->field_cache, sp );
        }
    }

    for (int gridx = x_start; gridx != x_stop; gridx += x_step) {
        for (int gridy = y_start; gridy != y_stop; gridy += y_step) {
            if (gridx + sp.x != x_stop && gridy + sp.y != y_stop) {
                for (int gridz = zmin; gridz <= zmax; gridz++) {
                    const tripoint grid(gridx, gridy, gridz);

                    copy_grid(grid, grid + sp);
                    submap* const cur_submap = get_submap_at_grid(grid);
                    if (cur_submap == nullptr) {
                        debugmsg("Tried to update vehicle list at %s but the submap is not loaded", grid.to_string());
                        continue;
                    }
                    update_vehicle_list(cur_submap, gridz);
                }
            }
            else {
                loadn(point(gridx, gridy), true);

                for (int gridz = zmin; gridz <= zmax; gridz++) {
                    loaded_grids.emplace_back(gridx, gridy, gridz);
                }
            }
        }

    }
    rebuild_vehicle_level_caches();

    g->setremoteveh( remoteveh );

    if( !support_cache_dirty.empty() ) {
        std::set<tripoint> old_cache = std::move( support_cache_dirty );
        support_cache_dirty.clear();
        for( const tripoint &pt : old_cache ) {
            support_cache_dirty.insert( pt + point( -sp.x * SEEX, -sp.y * SEEY ) );
        }
    }
    // actualize after loading all submaps to prevent errors
    // with entities at the edges
    for (tripoint loaded_grid : loaded_grids) {
        actualize(loaded_grid);
    }
}

void map::vertical_shift( const int newz )
{
    if( !zlevels ) {
        debugmsg( "Called map::vertical_shift in a non-z-level world" );
        return;
    }

    if( newz < -OVERMAP_DEPTH || newz > OVERMAP_HEIGHT ) {
        debugmsg( "Tried to get z-level %d outside allowed range of %d-%d",
                  newz, -OVERMAP_DEPTH, OVERMAP_HEIGHT );
        return;
    }

    tripoint_abs_sm trp = get_abs_sub();
    set_abs_sub( tripoint_abs_sm( trp.xy(), newz ) );

    // TODO: Remove the function when it's safe
}

// saven saves a single nonant.  worldx and worldy are used for the file
// name and specifies where in the world this nonant is.  gridx and gridy are
// the offset from the top left nonant:
// 0,0 1,0 2,0
// 0,1 1,1 2,1
// 0,2 1,2 2,2
// (worldx,worldy,worldz) denotes the absolute coordinate of the submap
// in grid[0].
void map::saven( const tripoint &grid )
{
    dbg( D_INFO ) << "map::saven(worldx[" << abs_sub.x() << "], worldy[" << abs_sub.y()
                  << "], worldz[" << abs_sub.z()
                  << "], gridx[" << grid.x << "], gridy[" << grid.y << "], gridz[" << grid.z << "])";
    const int gridn = get_nonant( grid );
    submap *submap_to_save = getsubmap( gridn );
    if( submap_to_save == nullptr ) {
        debugmsg( "Tried to save submap node (%d) but it's not loaded", gridn );
        return;
    }
    if( submap_to_save->get_ter( point_zero ) == t_null ) {
        // This is a serious error and should be signaled as soon as possible
        debugmsg( "map::saven grid %s uninitialized!", grid.to_string() );
        return;
    }

    const tripoint_abs_sm abs = abs_sub.xy() + grid;

    if( !zlevels && grid.z != abs_sub.z() ) {
        debugmsg( "Tried to save submap (%d,%d,%d) as (%d,%d,%d), which isn't supported in non-z-level builds",
                  abs.x(), abs.y(), abs_sub.z(), abs.x(), abs.y(), grid.z );
    }

    dbg( D_INFO ) << "map::saven abs: " << abs
                  << "  gridn: " << gridn;
    submap_to_save->last_touched = calendar::turn;
    MAPBUFFER.add_submap( abs, submap_to_save );
}

// Optimized mapgen function that only works properly for very simple overmap types
// Does not create or require a temporary map and does its own saving
bool generate_uniform(const tripoint_abs_sm& p, const oter_id& oter)
{
    std::unique_ptr<submap> sm = std::make_unique<submap>();
    if (oter == oter_open_air) {
        sm->set_all_ter(t_open_air, true);
    }
    else if (oter == oter_empty_rock || oter == oter_deep_rock) {
        sm->set_all_ter(t_rock, true);
    }
    else if (oter == oter_solid_earth) {
        sm->set_all_ter(ter_t_soil, true);
    }
    else {
        return false;
    }
    sm->last_touched = calendar::turn;
    return MAPBUFFER.add_submap(p, sm);
}

bool generate_uniform_omt(const tripoint_abs_sm& p, const oter_id& terrain_type)
{
    dbg( D_INFO ) << "generate_uniform p: " << p
                  << "  terrain_type: " << terrain_type.id().str();
    bool ret = true;
    for( int xd = 0; xd <= 1; xd++ ) {
        for( int yd = 0; yd <= 1; yd++ ) {
            ret &= generate_uniform(p + point(xd, yd), terrain_type);
        }
    }
    return ret;
}

void map::loadn(const tripoint& grid, const bool update_vehicles)
{
    dbg( D_INFO ) << "map::loadn(game[" << g.get() << "], worldx[" << abs_sub.x()
                  << "], worldy[" << abs_sub.y() << "], grid " << grid << ")";

    const tripoint_abs_sm grid_abs_sub = abs_sub.xy() + grid;
    const size_t gridn = get_nonant( grid );

    dbg( D_INFO ) << "map::loadn grid_abs_sub: " << grid_abs_sub << "  gridn: " << gridn;

    const int old_abs_z = abs_sub.z(); // Ugly, but necessary at the moment
    abs_sub.z() = grid.z;

    bool const main_inbounds =
        this != &get_map() && get_map().inbounds( project_to<coords::ms>( grid_abs_sub ) );

    submap *tmpsub = MAPBUFFER.lookup_submap( grid_abs_sub );
    if( tmpsub == nullptr ) {
        // It doesn't exist; we must generate it!
        dbg( D_INFO | D_WARNING ) << "map::loadn: Missing mapbuffer data.  Regenerating.";

        // Each overmap square is two nonants; to prevent overlap, generate only at
        //  squares divisible by 2.
        const tripoint_abs_omt grid_abs_omt = project_to<coords::omt>( grid_abs_sub );
        const tripoint_abs_sm grid_abs_sub_rounded = project_to<coords::sm>( grid_abs_omt );

        const oter_id terrain_type = overmap_buffer.ter( grid_abs_omt );

        // Short-circuit if the map tile is uniform
        // TODO: Replace with json mapgen functions.
        if (!generate_uniform_omt(grid_abs_sub_rounded, terrain_type)) {
            tinymap tmp_map;
            tmp_map.main_cleanup_override( false );
            tmp_map.generate( grid_abs_sub_rounded, calendar::turn );
            _main_requires_cleanup |= main_inbounds && tmp_map.is_main_cleanup_queued();
        }

        // This is the same call to MAPBUFFER as above!
        tmpsub = MAPBUFFER.lookup_submap( grid_abs_sub );
        if( tmpsub == nullptr ) {
            dbg( D_ERROR ) << "failed to generate a submap at " << grid_abs_sub;
            debugmsg( "failed to generate a submap at %s", grid_abs_sub.to_string() );
            return;
        }
    }

    // New submap changes the content of the map and all caches must be recalculated
    set_transparency_cache_dirty( grid.z );
    set_seen_cache_dirty( grid.z );
    set_outside_cache_dirty( grid.z );
    set_floor_cache_dirty( grid.z );
    set_pathfinding_cache_dirty( grid.z );
    setsubmap( gridn, tmpsub );
    if( !tmpsub->active_items.empty() ) {
        submaps_with_active_items_dirty.emplace(grid_abs_sub);
    }
    if( tmpsub->field_count > 0 ) {
        get_cache( grid.z ).field_cache.set( grid.x + grid.y * MAPSIZE );
    }

    // Destroy bugged no-part vehicles
    auto &veh_vec = tmpsub->vehicles;
    for( auto iter = veh_vec.begin(); iter != veh_vec.end(); ) {
        vehicle *veh = iter->get();
        if( veh->part_count() > 0 ) {
            // Always fix submap coordinates for easier Z-level-related operations
            veh->sm_pos = grid;
            iter++;
            if( main_inbounds ) {
                _main_requires_cleanup = true;
            }
        } else {
            if( veh->tracking_on ) {
                overmap_buffer.remove_vehicle( veh );
            }
            dirty_vehicle_list.erase( veh );
            iter = veh_vec.erase( iter );
        }
    }

    // Update vehicle data
    if( update_vehicles ) {
        level_cache &map_cache = get_cache( grid.z );
        for( const auto &veh : tmpsub->vehicles ) {
            // Only add if not tracking already.
            if( map_cache.vehicle_list.find( veh.get() ) == map_cache.vehicle_list.end() ) {
                map_cache.vehicle_list.insert( veh.get() );
                if( !veh->loot_zones.empty() ) {
                    map_cache.zone_vehicles.insert( veh.get() );
                }
            }
        }
    }

   

    abs_sub.z() = old_abs_z;
}

void map::loadn(const point& grid, bool update_vehicles)
{
    if( zlevels ) {
        for( int gridz = -OVERMAP_DEPTH; gridz <= OVERMAP_HEIGHT; gridz++ ) {
            loadn(tripoint(grid, gridz), update_vehicles);
        }

        // Note: we want it in a separate loop! It is a post-load cleanup
        // Since we're adding roofs, we want it to go up (from lowest to highest)
        for( int gridz = -OVERMAP_DEPTH; gridz <= OVERMAP_HEIGHT; gridz++ ) {
            add_roofs( tripoint( grid, gridz ) );
        }
    } else {
        loadn(tripoint(grid, abs_sub.z()), update_vehicles);
    }
}

void map::rotten_item_spawn( const item &item, const tripoint &pnt )
{
    if( get_creature_tracker().creature_at( pnt ) != nullptr ) {
        return;
    }
    const auto &comest = item.get_comestible();
    mongroup_id mgroup = comest->rot_spawn;
    if( mgroup.is_null() ) {
        return;
    }

    if( rng( 0, 100 ) < comest->rot_spawn_chance ) {
        std::vector<MonsterGroupResult> spawn_details = MonsterGroupManager::GetResultFromGroup( mgroup );
        for( const MonsterGroupResult &mgr : spawn_details ) {
            add_spawn(mgr, pnt, item.get_var("friendly",0));
        }
        if( get_player_view().sees( pnt ) ) {
            if( item.is_seed() ) {
                add_msg( m_warning, _( "Something has crawled out of the %s plants!" ), item.get_plant_name() );
            } else {
                add_msg( m_warning, _( "Something has crawled out of the %s!" ), item.tname() );
            }
        }
    }
}

void map::fill_funnels( const tripoint &p, const time_point &since )
{
    const trap &tr = tr_at( p );
    if( !tr.is_funnel() ) {
        return;
    }
    // Note: the inside/outside cache might not be correct at this time
    if( has_flag_ter_or_furn( ter_furn_flag::TFLAG_INDOORS, p ) ) {
        return;
    }
    map_stack items = i_at( p );
    units::volume maxvolume = 0_ml;
    auto biggest_container = items.end();
    for( auto candidate = items.begin(); candidate != items.end(); ++candidate ) {
        if( candidate->is_funnel_container( maxvolume ) ) {
            biggest_container = candidate;
        }
    }
    if( biggest_container != items.end() ) {
        retroactively_fill_from_funnel( *biggest_container, tr, since, calendar::turn, getabs( p ) );
    }
}

void map::grow_plant( const tripoint &p )
{
    const furn_t &furn = this->furn( p ).obj();
    if( !furn.has_flag( ter_furn_flag::TFLAG_PLANT ) ) {
        return;
    }
    // Can't use item_stack::only_item() since there might be fertilizer
    map_stack items = i_at( p );
    map_stack::iterator seed = std::find_if( items.begin(), items.end(), []( const item & it ) {
        return it.is_seed();
    } );

    if( seed == items.end() ) {
        // No seed there anymore, we don't know what kind of plant it was.
        // TODO: Fix point types
        const oter_id ot =
            overmap_buffer.ter( project_to<coords::omt>( tripoint_abs_ms( getabs( p ) ) ) );
        dbg( D_ERROR ) << "a planted item at " << p.x << "," << p.y << "," << p.z
                       << " (within overmap terrain " << ot.id().str() << ") has no seed data";
        i_clear( p );
        furn_set( p, f_null );
        return;
    }
    const time_duration plantEpoch = seed->get_plant_epoch();
    if( seed->age() >= plantEpoch * furn.plant->growth_multiplier &&
        !furn.has_flag( ter_furn_flag::TFLAG_GROWTH_HARVEST ) ) {
        if( seed->age() < plantEpoch * 2 ) {
            if( has_flag_furn( ter_furn_flag::TFLAG_GROWTH_SEEDLING, p ) ) {
                return;
            }

            // Remove fertilizer if any
            map_stack::iterator fertilizer = std::find_if( items.begin(), items.end(), []( const item & it ) {
                return it.has_flag( flag_FERTILIZER );
            } );
            if( fertilizer != items.end() ) {
                items.erase( fertilizer );
            }

            rotten_item_spawn( *seed, p );
            furn_set( p, furn_str_id( furn.plant->transform ) );
        } else if( seed->age() < plantEpoch * 3 * furn.plant->growth_multiplier ) {
            if( has_flag_furn( ter_furn_flag::TFLAG_GROWTH_MATURE, p ) ) {
                return;
            }

            // Remove fertilizer if any
            map_stack::iterator fertilizer = std::find_if( items.begin(), items.end(), []( const item & it ) {
                return it.has_flag( flag_FERTILIZER );
            } );
            if( fertilizer != items.end() ) {
                items.erase( fertilizer );
            }

            rotten_item_spawn( *seed, p );
            //You've skipped the seedling stage so roll monsters twice
            if( !has_flag_furn( ter_furn_flag::TFLAG_GROWTH_SEEDLING, p ) ) {
                rotten_item_spawn( *seed, p );
            }
            furn_set( p, furn_str_id( furn.plant->transform ) );
        } else {
            //You've skipped two stages so roll monsters two times
            if( has_flag_furn( ter_furn_flag::TFLAG_GROWTH_SEEDLING, p ) ) {
                rotten_item_spawn( *seed, p );
                rotten_item_spawn( *seed, p );
                //One stage change
            } else if( has_flag_furn( ter_furn_flag::TFLAG_GROWTH_MATURE, p ) ) {
                rotten_item_spawn( *seed, p );
                //Goes from seed to harvest in one check
            } else {
                rotten_item_spawn( *seed, p );
                rotten_item_spawn( *seed, p );
                rotten_item_spawn( *seed, p );
            }
            furn_set( p, furn_str_id( furn.plant->transform ) );
        }
    }
}

void map::restock_fruits( const tripoint &p, const time_duration &time_since_last_actualize )
{
    const ter_t &ter = this->ter( p ).obj();
    if( !ter.has_flag( ter_furn_flag::TFLAG_HARVESTED ) ) {
        return; // Already harvestable. Do nothing.
    }
    // Make it harvestable again if the last actualization was during a different season or year.
    const time_point last_touched = calendar::turn - time_since_last_actualize;
    if( season_of_year( calendar::turn ) != season_of_year( last_touched ) ||
        time_since_last_actualize >= calendar::season_length() ) {
        ter_set( p, ter.transforms_into );
    }
}

void map::produce_sap( const tripoint &p, const time_duration &time_since_last_actualize )
{
    if( time_since_last_actualize <= 0_turns ) {
        return;
    }

    if( t_tree_maple_tapped != ter( p ) ) {
        return;
    }

    // Amount of maple sap liters produced per season per tap
    static const int maple_sap_per_season = 56;

    // How many turns to produce 1 charge (250 ml) of sap?
    const time_duration producing_length = 0.75 * calendar::season_length();

    const time_duration turns_to_produce = producing_length / ( maple_sap_per_season * 4 );

    // How long of this time_since_last_actualize have we been in the producing period (late winter, early spring)?
    time_duration time_producing = 0_turns;

    if( time_since_last_actualize >= calendar::year_length() ) {
        time_producing = producing_length;
    } else {
        // We are only producing sap on the intersection with the sap producing season.
        const time_duration early_spring_end = 0.5f * calendar::season_length();
        const time_duration late_winter_start = 3.75f * calendar::season_length();

        const time_point last_actualize = calendar::turn - time_since_last_actualize;
        const time_duration last_actualize_tof = time_past_new_year( last_actualize );
        bool last_producing = last_actualize_tof >= late_winter_start ||
                              last_actualize_tof < early_spring_end;
        const time_duration current_tof = time_past_new_year( calendar::turn );
        bool current_producing = current_tof >= late_winter_start ||
                                 current_tof < early_spring_end;

        const time_duration non_producing_length = 3.25 * calendar::season_length();

        if( last_producing && current_producing ) {
            if( time_since_last_actualize < non_producing_length ) {
                time_producing = time_since_last_actualize;
            } else {
                time_producing = time_since_last_actualize - non_producing_length;
            }
        } else if( !last_producing && !current_producing ) {
            if( time_since_last_actualize > non_producing_length ) {
                time_producing = time_since_last_actualize - non_producing_length;
            }
        } else if( last_producing && !current_producing ) {
            // We hit the end of early spring
            if( last_actualize_tof < early_spring_end ) {
                time_producing = early_spring_end - last_actualize_tof;
            } else {
                time_producing = calendar::year_length() - last_actualize_tof + early_spring_end;
            }
        } else if( !last_producing && current_producing ) {
            // We hit the start of late winter
            if( current_tof >= late_winter_start ) {
                time_producing = current_tof - late_winter_start;
            } else {
                time_producing = 0.25f * calendar::season_length() + current_tof;
            }
        }
    }

    int new_charges = roll_remainder( time_producing / turns_to_produce );
    // Not enough time to produce 1 charge of sap
    if( new_charges <= 0 ) {
        return;
    }

    item sap( "maple_sap", calendar::turn );

    sap.set_item_temperature( get_weather().get_temperature( p ) );
    sap.charges = new_charges;

    // Is there a proper container?
    map_stack items = i_at( p );
    for( item &it : items ) {
        if( it.will_spill() || it.is_watertight_container() ) {
            const int capacity = it.get_remaining_capacity_for_liquid( sap, true );
            if( capacity > 0 ) {
                sap.charges = std::min( sap.charges, capacity );

                // The environment might have poisoned the sap with animals passing by, insects, leaves or contaminants in the ground
                sap.poison = one_in( 10 ) ? 1 : 0;

                it.put_in( sap, item_pocket::pocket_type::CONTAINER );
            }
            // Only fill up the first container.
            break;
        }
    }
}

void map::rad_scorch( const tripoint &p, const time_duration &time_since_last_actualize )
{
    const int rads = get_radiation( p );
    if( rads == 0 ) {
        return;
    }

    // TODO: More interesting rad scorch chance - base on season length?
    if( !x_in_y( 1.0 * rads * rads * time_since_last_actualize, 91_days ) ) {
        return;
    }

    // First destroy the farmable plants (those are furniture)
    // TODO: Rad-resistant mutant plants (that produce radioactive fruit)
    const furn_t &fid = furn( p ).obj();
    if( fid.has_flag( ter_furn_flag::TFLAG_PLANT ) ) {
        i_clear( p );
        furn_set( p, f_null );
    }

    const ter_id tid = ter( p );
    // TODO: De-hardcode this
    static const std::map<ter_id, ter_str_id> dies_into {{
            {t_grass, ter_t_dirt},
            {t_tree_young, ter_t_dirt},
            {t_tree_pine, ter_t_tree_deadpine},
            {t_tree_birch, ter_t_tree_birch_harvested},
            {t_tree_willow, ter_t_tree_willow_harvested},
            {t_tree_hickory, ter_t_tree_hickory_dead},
            {t_tree_hickory_harvested, ter_t_tree_hickory_dead},
        }};

    const auto iter = dies_into.find( tid );
    if( iter != dies_into.end() ) {
        ter_set( p, iter->second );
        return;
    }

    const ter_t &tr = tid.obj();
    if( tr.has_flag( ter_furn_flag::TFLAG_SHRUB ) ) {
        ter_set( p, t_dirt );
    } else if( tr.has_flag( ter_furn_flag::TFLAG_TREE ) ) {
        ter_set( p, ter_t_tree_dead );
    }
}

void map::decay_cosmetic_fields( const tripoint &p,
                                 const time_duration &time_since_last_actualize )
{
    for( auto &pr : field_at( p ) ) {
        field_entry &fd = pr.second;
        const time_duration hl = fd.get_field_type().obj().half_life;
        if( !fd.get_field_type()->accelerated_decay || hl <= 0_turns ) {
            continue;
        }

        const time_duration added_age = 2 * time_since_last_actualize / rng( 2, 4 );
        fd.mod_field_age( added_age );
        const int intensity_drop = fd.get_field_age() / hl;
        if( intensity_drop > 0 ) {
            fd.set_field_intensity( fd.get_field_intensity() - intensity_drop );
            fd.mod_field_age( -hl * intensity_drop );
        }
    }
}

void map::actualize( const tripoint &grid )
{
    submap *const tmpsub = get_submap_at_grid( grid );
    if( tmpsub == nullptr ) {
        debugmsg( "Actualize called on null submap (%d,%d,%d)", grid.x, grid.y, grid.z );
        return;
    }

    for( const std::unique_ptr<vehicle> &veh : tmpsub->vehicles ) {
        // spill out items too large, MIGRATION pockets etc from vehicle parts
        for( const vpart_reference &vp : veh->get_all_parts() ) {
            const item &base_const = vp.part().get_base();
            const_cast<item &>( base_const ).overflow( vp.pos() );
        }
        veh->refresh();
    }

    const time_duration time_since_last_actualize = calendar::turn - tmpsub->last_touched;
    const bool do_funnels = grid.z >= 0;

    // check spoiled stuff, and fill up funnels while we're at it
    process_items_in_vehicles(*tmpsub);
    process_items_in_submap( *tmpsub, grid );
    explosion_handler::process_explosions();
    for( int x = 0; x < SEEX; x++ ) {
        for( int y = 0; y < SEEY; y++ ) {
            const tripoint pnt = sm_to_ms_copy( grid ) + point( x, y );
            const point p( x, y );
            const furn_t &furn = *this->furn( pnt );
            const ter_t &terr = *this->ter( pnt );
            if( !furn.emissions.empty() ) {
                field_furn_locs.push_back( pnt );
            }
            if( !terr.emissions.empty() ) {
                field_ter_locs.push_back( pnt );
            }

            const auto trap_here = tmpsub->get_trap( p );
            if( trap_here != tr_null ) {
                traplocs[trap_here.to_i()].push_back( pnt );
            }
            const ter_t &ter = tmpsub->get_ter( p ).obj();
            if( ter.trap != tr_null && ter.trap != tr_ledge ) {
                traplocs[ter.trap.to_i()].push_back( pnt );
            }

            if( do_funnels ) {
                fill_funnels( pnt, tmpsub->last_touched );
            }

            grow_plant( pnt );

            restock_fruits( pnt, time_since_last_actualize );

            produce_sap( pnt, time_since_last_actualize );

            rad_scorch( pnt, time_since_last_actualize );

            decay_cosmetic_fields( pnt, time_since_last_actualize );
        }
    }

    // the last time we touched the submap, is right now.
    tmpsub->last_touched = calendar::turn;
}

void map::add_roofs( const tripoint &grid )
{
    if( !zlevels ) {
        // No roofs required!
        // Why not? Because submaps below and above don't exist yet
        return;
    }

    submap *const sub_here = get_submap_at_grid( grid );
    if( sub_here == nullptr ) {
        debugmsg( "Tried to add roofs/floors on null submap on %d,%d,%d",
                  grid.x, grid.y, grid.z );
        return;
    }

    bool check_roof = grid.z > -OVERMAP_DEPTH;

    submap *const sub_below = check_roof ? get_submap_at_grid( grid + tripoint_below ) : nullptr;

    if( check_roof && sub_below == nullptr ) {
        debugmsg( "Tried to add roofs to sm at %d,%d,%d, but sm below doesn't exist",
                  grid.x, grid.y, grid.z );
        return;
    }

    for( int x = 0; x < SEEX; x++ ) {
        for( int y = 0; y < SEEY; y++ ) {
            const ter_id ter_here = sub_here->get_ter( { x, y } );
            if (ter_here.id() != ter_t_open_air) {
                continue;
            }

            if( !check_roof ) {
                // Make sure we don't have open air at lowest z-level
                sub_here->set_ter( { x, y }, t_rock_floor );
                continue;
            }

            const ter_t &ter_below = sub_below->get_ter( { x, y } ).obj();
            if( ter_below.roof ) {
                // TODO: Make roof variable a ter_id to speed this up
                sub_here->set_ter( { x, y }, ter_below.roof.id() );
            }
        }
    }
}

void map::copy_grid( const tripoint &to, const tripoint &from )
{
    submap *smap = get_submap_at_grid( from );
    if( smap == nullptr ) {
        debugmsg( "Tried to copy grid from (%d,%d,%d) but the submap is not loaded", from.x, from.y,
                  from.z );
        return;
    }
    setsubmap( get_nonant( to ), smap );
    for( auto &it : smap->vehicles ) {
        it->sm_pos = to;
    }
}

void map::spawn_monsters_submap_group( const tripoint &gp, mongroup &group, bool ignore_sight )
{
    Character &player_character = get_player_character();
    const int s_range = std::min( HALF_MAPSIZE_X,
                                  player_character.sight_range( g->light_level( player_character.posz() ) ) );
    int pop = group.population;
    std::vector<tripoint> locations;
    if( !ignore_sight ) {
        // If the submap is one of the outermost submaps, assume that monsters are
        // invisible there.
        if( gp.x == 0 || gp.y == 0 || gp.x + 1 == MAPSIZE || gp.y + 1 == MAPSIZE ) {
            ignore_sight = true;
        }
    }

    if( gp.z != player_character.posz() ) {
        // Note: this is only OK because 3D vision isn't a thing yet
        ignore_sight = true;
    }

    static const auto allow_on_terrain = [&]( const tripoint & p ) {
        // TODO: flying creatures should be allowed to spawn without a floor,
        // but the new creature is created *after* determining the terrain, so
        // we can't check for it here.
        return passable( p ) && has_floor( p );
    };

    // If the submap is uniform, we can skip many checks
    const submap *current_submap = get_submap_at_grid( gp );
    if( current_submap == nullptr ) {
        debugmsg( "Tried spawn monster group at (%d,%d,%d) but the submap is not loaded", gp.x, gp.y,
                  gp.z );
        return;
    }
    bool ignore_terrain_checks = false;
    bool ignore_inside_checks = gp.z < 0;
    if( current_submap->is_uniform() ) {
        const tripoint upper_left{ SEEX * gp.x, SEEY * gp.y, gp.z };
        if( !allow_on_terrain( upper_left ) ||
            ( !ignore_inside_checks && has_flag_ter_or_furn( ter_furn_flag::TFLAG_INDOORS, upper_left ) ) ) {
            const tripoint glp = getabs( gp );
            dbg( D_WARNING ) << "Empty locations for group " << group.type.str() <<
                             " at uniform submap " << gp.x << "," << gp.y << "," << gp.z <<
                             " global " << glp.x << "," << glp.y << "," << glp.z;
            return;
        }

        ignore_terrain_checks = true;
        ignore_inside_checks = true;
    }

    creature_tracker &creatures = get_creature_tracker();
    for( int x = 0; x < SEEX; ++x ) {
        for( int y = 0; y < SEEY; ++y ) {
            point f( x + SEEX * gp.x, y + SEEY * gp.y );
            tripoint fp{ f, gp.z };
            if( creatures.creature_at( fp ) != nullptr ) {
                continue; // there is already some creature
            }

            if( !ignore_terrain_checks && !allow_on_terrain( fp ) ) {
                continue; // solid area, impassable
            }

            if( !ignore_sight && sees( player_character.pos(), fp, s_range ) ) {
                continue; // monster must spawn outside the viewing range of the player
            }

            if( !ignore_inside_checks && has_flag_ter_or_furn( ter_furn_flag::TFLAG_INDOORS, fp ) ) {
                continue; // monster must spawn outside.
            }

            locations.push_back( fp );
        }
    }

    if( locations.empty() ) {
        // TODO: what now? there is no possible place to spawn monsters, most
        // likely because the player can see all the places.
        const tripoint glp = getabs( gp );
        dbg( D_WARNING ) << "Empty locations for group " << group.type.str() <<
                         " at " << gp.x << "," << gp.y << "," << gp.z <<
                         " global " << glp.x << "," << glp.y << "," << glp.z;
        // Just kill the group. It's not like we're removing existing monsters
        // Unless it's a horde - then don't kill it and let it spawn behind a tree or smoke cloud
        if( !group.horde ) {
            group.clear();
        }

        return;
    }

    if( pop ) {
        // Populate the group from its population variable.
        for( int m = 0; m < pop; m++ ) {
            std::vector<MonsterGroupResult> spawn_details =
                MonsterGroupManager::GetResultFromGroup( group.type, &pop );
            for( const MonsterGroupResult &mgr : spawn_details ) {
                if( !mgr.name ) {
                    continue;
                }
                monster tmp( mgr.name );

                // If a monster came from a horde population, configure them to always be willing to rejoin a horde.
                if( group.horde ) {
                    tmp.set_horde_attraction( MHA_ALWAYS );
                }
                for( int i = 0; i < mgr.pack_size; i++ ) {
                    group.monsters.push_back( tmp );
                }
            }
        }
    }

    // Find horde's target submap
    for( monster &tmp : group.monsters ) {
        for( int tries = 0; tries < 10 && !locations.empty(); tries++ ) {
            const tripoint local_pos = random_entry_removed( locations );
            const tripoint_abs_ms abs_pos = get_map().getglobal( local_pos );
            if( !tmp.can_move_to( local_pos ) ) {
                continue; // target can not contain the monster
            }
            if( group.horde ) {
                // Give monster a random point near horde's expected destination
                const tripoint_sm_ms pos_in_sm( rng( 0, SEEX ), rng( 0, SEEY ), local_pos.z );
                const tripoint_abs_ms rand_dest = project_combine( group.target, pos_in_sm );
                const int turns = rl_dist( abs_pos, rand_dest ) + group.interest;
                tmp.wander_to( rand_dest, turns );
                add_msg_debug( debugmode::DF_MAP, "%s targeting %s", tmp.disp_name(),
                               tmp.wander_pos.to_string_writable() );
            }

            monster *const placed = g->place_critter_at( make_shared_fast<monster>( tmp ), local_pos );
            if( placed ) {
                placed->on_load();
            }
            break;
        }
    }
    // indicates the group is empty, and can be removed later
    group.clear();
}

void map::spawn_monsters_submap( const tripoint &gp, bool ignore_sight )
{
    // TODO: fix point types
    const tripoint_abs_sm submap_pos( gp + abs_sub.xy() );
    // Load unloaded monsters
    overmap_buffer.spawn_monster( submap_pos );
    // Only spawn new monsters after existing monsters are loaded.
    std::vector<mongroup *> groups = overmap_buffer.groups_at( submap_pos );
    for( mongroup *&mgp : groups ) {
        spawn_monsters_submap_group( gp, *mgp, ignore_sight );
    }

    submap *const current_submap = get_submap_at_grid( gp );
    if( current_submap == nullptr ) {
        debugmsg( "Tried spawn monsters at (%d,%d,%d) but the submap is not loaded", gp.x, gp.y, gp.z );
        return;
    }
    const tripoint gp_ms = sm_to_ms_copy( gp );

    creature_tracker &creatures = get_creature_tracker();
    for (size_t sp_i = 0; sp_i < current_submap->spawns.size(); ++sp_i) {
        const spawn_point i = current_submap->spawns[sp_i];
        const tripoint center = gp_ms + i.pos;
        const tripoint_range<tripoint> points = points_in_radius( center, 3 );

        for( int j = 0; j < i.count; j++ ) {
            monster tmp( i.type );
            if( i.mission_id > 0 ) {
                tmp.mission_ids = { i.mission_id };
            }
            if( i.name != "NONE" ) {
                tmp.unique_name = i.name;
            }
            if( i.friendly ) {
                tmp.friendly = -1;
                tmp.add_effect( effect_pet, 1_turns, true );
            }
            if( !i.data.ammo.empty() ) {
                for( std::pair<itype_id, jmapgen_int> ap : i.data.ammo ) {
                    tmp.ammo.emplace( ap.first, ap.second.get() );
                }
            } else {
                tmp.ammo = tmp.type->starting_ammo;
            }

            const auto valid_location = [&]( const tripoint & p ) {
                // Checking for creatures via g is only meaningful if this is the main game map.
                // If it's some local map instance, the coordinates will most likely not even match.
                return ( !g || &get_map() != this || !creatures.creature_at( p ) ) && tmp.can_move_to( p );
            };

            const auto place_it = [&]( const tripoint & p ) {
                monster *const placed = g->place_critter_at( make_shared_fast<monster>( tmp ), p );
                if( !i.data.patrol_points_rel_ms.empty() ) {
                    placed->set_patrol_route( i.data.patrol_points_rel_ms );
                }
                if( placed ) {
                    placed->on_load();
                }
            };

            // First check out defined spawn location for a valid placement, and if that doesn't work
            // then fall back to picking a random point that is a valid location.
            if( valid_location( center ) ) {
                place_it( center );
            } else if( const std::optional<tripoint> pos = random_point( points, valid_location ) ) {
                place_it( *pos );
            }
        }
    }
    current_submap->spawns.clear();
}

void map::spawn_monsters( bool ignore_sight )
{
    const int zmin = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    const int zmax = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    tripoint gp;
    int &gx = gp.x;
    int &gy = gp.y;
    int &gz = gp.z;
    for( gz = zmin; gz <= zmax; gz++ ) {
        for( gx = 0; gx < my_MAPSIZE; gx++ ) {
            for( gy = 0; gy < my_MAPSIZE; gy++ ) {
                spawn_monsters_submap( gp, ignore_sight );
            }
        }
    }
}

void map::clear_spawns()
{
    for( submap *&smap : grid ) {
        smap->spawns.clear();
    }
}

void map::clear_traps()
{
    for( submap *&smap : grid ) {
        for( int x = 0; x < SEEX; x++ ) {
            for( int y = 0; y < SEEY; y++ ) {
                const point p( x, y );
                smap->set_trap( p, tr_null );
            }
        }
    }

    // Forget about all trap locations.
    for( auto &i : traplocs ) {
        i.clear();
    }
}

const std::vector<tripoint> &map::get_furn_field_locations() const
{
    return field_furn_locs;
}

const std::vector<tripoint> &map::get_ter_field_locations() const
{
    return field_ter_locs;
}

const std::vector<tripoint> &map::trap_locations( const trap_id &type ) const
{
    return traplocs[type.to_i()];
}

bool map::inbounds( const tripoint_abs_ms &p ) const
{
    return inbounds( getlocal( p ) );
}

bool map::inbounds( const tripoint &p ) const
{
    static constexpr tripoint map_boundary_min( 0, 0, -OVERMAP_DEPTH );
    static constexpr tripoint map_boundary_max( MAPSIZE_Y, MAPSIZE_X, OVERMAP_HEIGHT + 1 );

    static constexpr half_open_cuboid<tripoint> map_boundaries(
        map_boundary_min, map_boundary_max );

    return map_boundaries.contains( p );
}

bool map::inbounds( const tripoint_bub_ms &p ) const
{
    return inbounds( p.raw() );
}

bool map::inbounds( const tripoint_abs_omt &p ) const
{
    const tripoint_abs_omt map_origin = project_to<coords::omt>( abs_sub );
    return inbounds_z( p.z() ) &&
           p.x() >= map_origin.x() &&
           p.y() >= map_origin.y() &&
           p.x() <= map_origin.x() + my_HALF_MAPSIZE &&
           p.y() <= map_origin.y() + my_HALF_MAPSIZE;
}

bool tinymap::inbounds( const tripoint &p ) const
{
    constexpr tripoint map_boundary_min( 0, 0, -OVERMAP_DEPTH );
    constexpr tripoint map_boundary_max( SEEY * 2, SEEX * 2, OVERMAP_HEIGHT + 1 );

    constexpr half_open_cuboid<tripoint> map_boundaries( map_boundary_min, map_boundary_max );

    return map_boundaries.contains( p );
}

// set up a map just long enough scribble on it
// this tinymap should never, ever get saved
fake_map::fake_map( const ter_id &ter_type )
{
    const tripoint_abs_sm tripoint_below_zero( 0, 0, fake_map_z );

    set_abs_sub( tripoint_below_zero );
    for( int gridx = 0; gridx < my_MAPSIZE; gridx++ ) {
        for( int gridy = 0; gridy < my_MAPSIZE; gridy++ ) {
            std::unique_ptr<submap> sm = std::make_unique<submap>();

            sm->set_all_ter( ter_type );
            sm->set_all_furn( f_null );
            sm->set_all_traps( tr_null );

            setsubmap( get_nonant( { gridx, gridy, fake_map_z } ), sm.get() );

            temp_submaps_.emplace_back( std::move( sm ) );
        }
    }
}

fake_map::~fake_map() = default;

void map::set_graffiti( const tripoint &p, const std::string &contents )
{
    if( !inbounds( p ) ) {
        return;
    }
    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to set graffiti at (%d,%d) but the submap is not loaded", l.x, l.y );
        return;
    }
    current_submap->set_graffiti( l, contents );
}

void map::delete_graffiti( const tripoint &p )
{
    if( !inbounds( p ) ) {
        return;
    }
    point l;
    submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to delete graffiti at (%d,%d) but the submap is not loaded", l.x, l.y );
        return;
    }
    current_submap->delete_graffiti( l );
}

const std::string &map::graffiti_at( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        static const std::string empty_string;
        return empty_string;
    }
    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to get graffiti at (%d,%d) but the submap is not loaded", l.x, l.y );
        static const std::string empty_string;
        return empty_string;
    }
    return current_submap->get_graffiti( l );
}

bool map::has_graffiti_at( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        return false;
    }
    point l;
    const submap *const current_submap = unsafe_get_submap_at( p, l );
    if( current_submap == nullptr ) {
        debugmsg( "Tried to get graffiti at (%d,%d) but the submap is not loaded", l.x, l.y );
        return false;
    }
    return current_submap->has_graffiti( l );
}

int map::determine_wall_corner( const tripoint &p ) const
{
    const std::bitset<NUM_TERCONN> &test_connect_group = ter( p ).obj().connect_to_groups;
    uint8_t connections = get_known_connections( p, test_connect_group );
    // The bits in connections are SEWN, whereas the characters in LINE_
    // constants are NESW, so we want values in 8 | 2 | 1 | 4 order.
    switch( connections ) {
        case 8 | 2 | 1 | 4:
            return LINE_XXXX;
        case 0 | 2 | 1 | 4:
            return LINE_OXXX;

        case 8 | 0 | 1 | 4:
            return LINE_XOXX;
        case 0 | 0 | 1 | 4:
            return LINE_OOXX;

        case 8 | 2 | 0 | 4:
            return LINE_XXOX;
        case 0 | 2 | 0 | 4: // NOLINT(misc-redundant-expression)
            return LINE_OXOX;
        case 8 | 0 | 0 | 4: // NOLINT(misc-redundant-expression)
            return LINE_XOOX;
        case 0 | 0 | 0 | 4:
            return LINE_OXOX; // LINE_OOOX would be better

        case 8 | 2 | 1 | 0:
            return LINE_XXXO;
        case 0 | 2 | 1 | 0: // NOLINT(misc-redundant-expression)
            return LINE_OXXO;
        case 8 | 0 | 1 | 0: // NOLINT(bugprone-branch-clone,misc-redundant-expression)
            return LINE_XOXO;
        case 0 | 0 | 1 | 0:
            return LINE_XOXO; // LINE_OOXO would be better
        case 8 | 2 | 0 | 0: // NOLINT(misc-redundant-expression)
            return LINE_XXOO;
        case 0 | 2 | 0 | 0: // NOLINT(misc-redundant-expression)
            return LINE_OXOX; // LINE_OXOO would be better
        case 8 | 0 | 0 | 0: // NOLINT(misc-redundant-expression)
            return LINE_XOXO; // LINE_XOOO would be better

        case 0 | 0 | 0 | 0:
            return ter( p ).obj().symbol(); // technically just a column

        default:
            // cata_assert( false );
            // this shall not happen
            return '?';
    }
}

void map::build_outside_cache( const int zlev )
{
    auto *ch_lazy = get_cache_lazy( zlev );
    if( !ch_lazy || !ch_lazy->outside_cache_dirty ) {
        return;
    }
    level_cache &ch = *ch_lazy;

    // Make a bigger cache to avoid bounds checking
    // We will later copy it to our regular cache
    const size_t padded_w = MAPSIZE_X + 2;
    const size_t padded_h = MAPSIZE_Y + 2;
    cata::mdarray<bool, point_bub_ms, padded_w, padded_h> padded_cache;

    auto &outside_cache = ch.outside_cache;
    if( zlev < 0 ) {
        std::uninitialized_fill_n(
            &outside_cache[0][0], MAPSIZE_X * MAPSIZE_Y, false );
        return;
    }

    padded_cache.fill( true );

    for( int smx = 0; smx < my_MAPSIZE; ++smx ) {
        for( int smy = 0; smy < my_MAPSIZE; ++smy ) {
            const submap *cur_submap = get_submap_at_grid( { smx, smy, zlev } );
            if( cur_submap == nullptr ) {
                debugmsg( "Tried to build outside cache at (%d,%d,%d) but the submap is not loaded", smx, smy,
                          zlev );
                continue;
            }

            for( int sx = 0; sx < SEEX; ++sx ) {
                for( int sy = 0; sy < SEEY; ++sy ) {
                    point sp( sx, sy );
                    if( cur_submap->get_ter( sp ).obj().has_flag( ter_furn_flag::TFLAG_INDOORS ) ||
                        cur_submap->get_furn( sp ).obj().has_flag( ter_furn_flag::TFLAG_INDOORS ) ) {
                        const point p( sx + smx * SEEX, sy + smy * SEEY );
                        // Add 1 to both coordinates, because we're operating on the padded cache
                        for( int dx = 0; dx <= 2; dx++ ) {
                            for( int dy = 0; dy <= 2; dy++ ) {
                                padded_cache[p.x + dx][p.y + dy] = false;
                            }
                        }
                    }
                }
            }
        }
    }

    // Copy the padded cache back to the proper one, but with no padding
    for( int x = 0; x < SEEX * my_MAPSIZE; x++ ) {
        std::copy_n( &padded_cache[x + 1][1], SEEX * my_MAPSIZE, &outside_cache[x][0] );
    }

    ch.outside_cache_dirty = false;
}

void map::build_obstacle_cache(
    const tripoint &start, const tripoint &end,
    cata::mdarray<fragment_cloud, point_bub_ms> &obstacle_cache )
{
    const point min_submap{ std::max( 0, start.x / SEEX ), std::max( 0, start.y / SEEY ) };
    const point max_submap{
        std::min( my_MAPSIZE - 1, end.x / SEEX ), std::min( my_MAPSIZE - 1, end.y / SEEY ) };
    // Find and cache all the map obstacles.
    // For now setting obstacles to be extremely dense and fill their squares.
    // In future, scale effective obstacle density by the thickness of the obstacle.
    // Also consider modelling partial obstacles.
    // TODO: Support z-levels.
    for( int smx = min_submap.x; smx <= max_submap.x; ++smx ) {
        for( int smy = min_submap.y; smy <= max_submap.y; ++smy ) {
            const submap *cur_submap = get_submap_at_grid( { smx, smy, start.z } );
            if( cur_submap == nullptr ) {
                debugmsg( "Tried to build obstacle cache at (%d,%d,%d) but the submap is not loaded", smx, smy,
                          start.z );
                continue;
            }

            // TODO: Init indices to prevent iterating over unused submap sections.
            for( int sx = 0; sx < SEEX; ++sx ) {
                for( int sy = 0; sy < SEEY; ++sy ) {
                    const point sp( sx, sy );
                    int ter_move = cur_submap->get_ter( sp ).obj().movecost;
                    int furn_move = cur_submap->get_furn( sp ).obj().movecost;
                    const point p2( sx + smx * SEEX, sy + smy * SEEY );
                    if( ter_move == 0 || furn_move < 0 || ter_move + furn_move == 0 ) {
                        obstacle_cache[p2.x][p2.y].velocity = 1000.0f;
                        obstacle_cache[p2.x][p2.y].density = 0.0f;
                    } else {
                        // Magic number warning, this is the density of air at sea level at
                        // some nominal temp and humidity.
                        // TODO: figure out if our temp/altitude/humidity variation is
                        // sufficient to bother setting this differently.
                        obstacle_cache[p2.x][p2.y].velocity = 1.2f;
                        obstacle_cache[p2.x][p2.y].density = 1.0f;
                    }
                }
            }
        }
    }
    VehicleList vehs = get_vehicles( start, end );
    const inclusive_cuboid<tripoint> bounds( start, end );
    // Cache all the vehicle stuff in one loop
    for( wrapped_vehicle &v : vehs ) {
        for( const vpart_reference &vp : v.v->get_all_parts() ) {
            tripoint p = v.pos + vp.part().precalc[0];
            if( p.z != start.z ) {
                break;
            }
            if( !bounds.contains( p ) ) {
                continue;
            }

            if( vp.obstacle_at_part() ) {
                obstacle_cache[p.x][p.y].velocity = 1000.0f;
                obstacle_cache[p.x][p.y].density = 0.0f;
            }
        }
    }
    // Iterate over creatures and set them to block their squares relative to their size.
    for( Creature &critter : g->all_creatures() ) {
        const tripoint loc = critter.pos();
        if( loc.z != start.z ) {
            continue;
        }
        // TODO: scale this with expected creature "thickness".
        obstacle_cache[loc.x][loc.y].velocity = 1000.0f;
        // ranged_target_size is "proportion of square that is blocked", and density needs to be
        // "transmissivity of square", so we need the reciprocal.
        obstacle_cache[loc.x][loc.y].density = 1.0 - critter.ranged_target_size();
    }
}

// If this ever shows up on profiling, maybe prepopulate one of these in the map cache for each level.
std::bitset<OVERMAP_LAYERS> map::get_inter_level_visibility( const int origin_zlevel ) const
{
    std::bitset<OVERMAP_LAYERS> seen_levels;
    seen_levels.set( origin_zlevel + OVERMAP_DEPTH );
    for( int z = origin_zlevel + 1; z <= OVERMAP_HEIGHT; ++z ) {
        if( get_cache_ref( z ).no_floor_gaps ) {
            break;
        } else {
            seen_levels.set( z + OVERMAP_DEPTH );
        }
    }
    for( int z = origin_zlevel; z > -OVERMAP_DEPTH; --z ) {
        if( get_cache_ref( z ).no_floor_gaps ) {
            break;
        } else {
            // No floor means we can see the *lower* level.
            seen_levels.set( z - 1 + OVERMAP_DEPTH );
        }
    }
    return seen_levels;
}

bool map::build_floor_cache( const int zlev )
{
    auto *ch_lazy = get_cache_lazy( zlev );
    if( !ch_lazy || !ch_lazy->floor_cache_dirty ) {
        return false;
    }
    level_cache &ch = *ch_lazy;

    auto &floor_cache = ch.floor_cache;
    std::uninitialized_fill_n(
        &floor_cache[0][0], MAPSIZE_X * MAPSIZE_Y, true );
    bool &no_floor_gaps = ch.no_floor_gaps;
    no_floor_gaps = true;

    bool lowest_z_lev = zlev <= -OVERMAP_DEPTH;

    for( int smx = 0; smx < my_MAPSIZE; ++smx ) {
        for( int smy = 0; smy < my_MAPSIZE; ++smy ) {
            const submap *cur_submap = get_submap_at_grid( { smx, smy, zlev } );
            const submap *below_submap = !lowest_z_lev ? get_submap_at_grid( { smx, smy, zlev - 1 } ) : nullptr;

            if( cur_submap == nullptr ) {
                debugmsg( "Tried to build floor cache at (%d,%d,%d) but the submap is not loaded", smx, smy, zlev );
                continue;
            }
            if( !lowest_z_lev && below_submap == nullptr ) {
                debugmsg( "Tried to build floor cache at (%d,%d,%d) but the submap is not loaded", smx, smy,
                          zlev - 1 );
                continue;
            }

            for( int sx = 0; sx < SEEX; ++sx ) {
                for( int sy = 0; sy < SEEY; ++sy ) {
                    point sp( sx, sy );
                    const ter_t &terrain = cur_submap->get_ter( sp ).obj();
                    if( terrain.has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) ||
                        terrain.has_flag( ter_furn_flag::TFLAG_GOES_DOWN ) ||
                        terrain.has_flag( ter_furn_flag::TFLAG_TRANSPARENT_FLOOR ) ) {
                        if( below_submap &&
                            below_submap->get_furn( sp ).obj().has_flag( ter_furn_flag::TFLAG_SUN_ROOF_ABOVE ) ) {
                            continue;
                        }
                        const point p( sx + smx * SEEX, sy + smy * SEEY );
                        floor_cache[p.x][p.y] = false;
                        no_floor_gaps = false;
                    }
                }
            }
        }
    }

    ch.floor_cache_dirty = false;
    return zlevels;
}

void map::build_floor_caches()
{
    const int minz = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    const int maxz = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    for( int z = minz; z <= maxz; z++ ) {
        build_floor_cache( z );
    }
}

static void vehicle_caching_internal( level_cache &zch, const vpart_reference &vp, vehicle *v )
{
    auto &outside_cache = zch.outside_cache;
    auto &transparency_cache = zch.transparency_cache;
    auto &floor_cache = zch.floor_cache;

    const size_t part = vp.part_index();
    const tripoint part_pos =  v->global_part_pos3( vp.part() );

    bool vehicle_is_opaque = vp.has_feature( VPFLAG_OPAQUE ) && !vp.part().is_broken();

    if( vehicle_is_opaque ) {
        int dpart = v->part_with_feature( part, VPFLAG_OPENABLE, true );
        if( dpart < 0 || !v->part( dpart ).open ) {
            transparency_cache[part_pos.x][part_pos.y] = LIGHT_TRANSPARENCY_SOLID;
        } else {
            vehicle_is_opaque = false;
        }
    }

    if( vehicle_is_opaque || vp.is_inside() ) {
        outside_cache[part_pos.x][part_pos.y] = false;
    }

    if( vp.has_feature( VPFLAG_BOARDABLE ) && !vp.part().is_broken() ) {
        floor_cache[part_pos.x][part_pos.y] = true;
    }
}

static void vehicle_caching_internal_above( level_cache &zch_above, const vpart_reference &vp,
        vehicle *v )
{
    if( vp.has_feature( VPFLAG_ROOF ) || vp.has_feature( VPFLAG_OPAQUE ) ) {
        const tripoint part_pos = v->global_part_pos3( vp.part() );
        zch_above.floor_cache[part_pos.x][part_pos.y] = true;
    }
}

void map::do_vehicle_caching( int z )
{
    level_cache *ch = get_cache_lazy( z );
    if( !ch ) {
        return;
    }
    for( vehicle *v : ch->vehicle_list ) {
        for( const vpart_reference &vp : v->get_all_parts_with_fakes() ) {
            const tripoint part_pos = v->global_part_pos3( vp.part() );
            if( !inbounds( part_pos.xy() ) ) {
                continue;
            }
            vehicle_caching_internal( get_cache( part_pos.z ), vp, v );
            if( part_pos.z < OVERMAP_HEIGHT ) {
                vehicle_caching_internal_above( get_cache( part_pos.z + 1 ), vp, v );
            }
        }
    }
}

void map::build_map_cache( const int zlev, bool skip_lightmap )
{
    const int minz = zlevels ? -OVERMAP_DEPTH : zlev;
    const int maxz = zlevels ? OVERMAP_HEIGHT : zlev;
    bool seen_cache_dirty = false;
    bool camera_cache_dirty = false;
    for( int z = minz; z <= maxz; z++ ) {
        const bool affects_seen_cache =  z == zlev;
        build_outside_cache( z );
        build_transparency_cache( z );
        bool floor_cache_was_dirty = build_floor_cache( z );
        seen_cache_dirty |= ( floor_cache_was_dirty && affects_seen_cache );
        if( floor_cache_was_dirty && z > -OVERMAP_DEPTH ) {
            get_cache( z - 1 ).r_up_cache->invalidate();
        }
        seen_cache_dirty |= get_cache( z ).seen_cache_dirty && affects_seen_cache;
    }
    // needs a separate pass as it changes the caches on neighbour z-levels (e.g. floor_cache);
    // otherwise such changes might be overwritten by main cache-building logic
    for( int z = minz; z <= maxz; z++ ) {
        do_vehicle_caching( z );
    }

    for (int z = minz; z <= maxz; z++) {
        seen_cache_dirty |= build_vision_transparency_cache(z);
    }

    if( seen_cache_dirty ) {
        skew_vision_cache.clear();
    }
    avatar &u = get_avatar();
    Character::moncam_cache_t mcache = u.get_active_moncams();
    Character::moncam_cache_t diff;
    std::set_symmetric_difference( u.moncam_cache.begin(), u.moncam_cache.end(), mcache.begin(),
                                   mcache.end(), std::inserter( diff, diff.end() ) );
    camera_cache_dirty |= !diff.empty();
    // Initial value is illegal player position.
    const tripoint_abs_ms p = get_player_character().get_location();
    int const sr = u.unimpaired_range();
    static tripoint_abs_ms player_prev_pos;
    static int player_prev_range( 0 );
    seen_cache_dirty |= player_prev_pos != p || sr != player_prev_range || camera_cache_dirty;
    if( seen_cache_dirty ) {
        if( inbounds( p ) ) {
            build_seen_cache( getlocal( p ), zlev, sr );
        }
        player_prev_pos = p;
        player_prev_range = sr;
        camera_cache_dirty = true;
    }
    if( camera_cache_dirty ) {
        u.moncam_cache = mcache;
        bool cumulative = seen_cache_dirty;
        for( Character::cached_moncam const &mon : u.moncam_cache ) {
            if( inbounds( mon.second ) ) {
                int const range = mon.first->type->vision_day;
                build_seen_cache( getlocal( mon.second ), mon.second.z(), range, cumulative,
                                  true, std::max( 60 - range, 0 ) );
                cumulative = true;
            }
        }
    }
    if( !skip_lightmap ) {
        generate_lightmap( zlev );
    }
}

//////////
///// coordinate helpers

tripoint map::getabs( const tripoint &p ) const
{
    // TODO: fix point types
    return sm_to_ms_copy( abs_sub.xy().raw() ) + p;
}

tripoint map::getabs( const tripoint_bub_ms &p ) const
{
    return ( project_to<coords::ms>( abs_sub.xy() ) + p.raw() ).raw();
}

tripoint_abs_ms map::getglobal( const tripoint &p ) const
{
    return tripoint_abs_ms( getabs( p ) );
}

tripoint_abs_ms map::getglobal( const tripoint_bub_ms &p ) const
{
    return project_to<coords::ms>( abs_sub.xy() ) + p.raw();
}

tripoint map::getlocal( const tripoint &p ) const
{
    // TODO: fix point types
    return p - sm_to_ms_copy( abs_sub.xy().raw() );
}

tripoint map::getlocal( const tripoint_abs_ms &p ) const
{
    // TODO: fix point types
    return getlocal( p.raw() );
}

tripoint_bub_ms map::bub_from_abs( const tripoint &p ) const
{
    // TODO: fix point types
    return bub_from_abs( tripoint_abs_ms( p ) );
}

tripoint_bub_ms map::bub_from_abs( const tripoint_abs_ms &p ) const
{
    return tripoint_bub_ms() + ( p - project_to<coords::ms>( abs_sub.xy() ) );
}

void map::set_abs_sub( const tripoint_abs_sm &p )
{
    abs_sub = p;
}

tripoint_abs_sm map::get_abs_sub() const
{
    return abs_sub;
}

template<typename Array>
static cata::copy_const<Array, submap> *getsubmap_helper(
    const Array &grid, const size_t grididx )
{
    if( grididx >= grid.size() ) {
        debugmsg( "Tried to access invalid grid index %d. Grid size: %d", grididx, grid.size() );
        return nullptr;
    }
    return grid[grididx];
}

submap *map::getsubmap( const size_t grididx )
{
    return getsubmap_helper( grid, grididx );
}

const submap *map::getsubmap( const size_t grididx ) const
{
    return getsubmap_helper( grid, grididx );
}

void map::setsubmap( const size_t grididx, submap *const smap )
{
    if( grididx >= grid.size() ) {
        debugmsg( "Tried to access invalid grid index %d", grididx );
        return;
    } else if( smap == nullptr ) {
        debugmsg( "Tried to set NULL submap pointer at index %d", grididx );
        return;
    }
    grid[grididx] = smap;
}

submap *map::get_submap_at( const tripoint &p )
{
    if( !inbounds( p ) ) {
        debugmsg( "Tried to access invalid map position %s", p.to_string() );
        return nullptr;
    }
    return unsafe_get_submap_at( p );
}

const submap *map::get_submap_at( const tripoint &p ) const
{
    if( !inbounds( p ) ) {
        debugmsg( "Tried to access invalid map position %s", p.to_string() );
        return nullptr;
    }
    return unsafe_get_submap_at( p );
}

submap *map::get_submap_at_grid( const tripoint &gridp )
{
    return getsubmap( get_nonant( gridp ) );
}

const submap *map::get_submap_at_grid( const tripoint &gridp ) const
{
    return getsubmap( get_nonant( gridp ) );
}

size_t map::get_nonant( const tripoint &gridp ) const
{
    if( gridp.x < 0 || gridp.x >= my_MAPSIZE ||
        gridp.y < 0 || gridp.y >= my_MAPSIZE ||
        gridp.z < -OVERMAP_DEPTH || gridp.z > OVERMAP_HEIGHT ) {
        debugmsg( "Tried to access invalid map position at grid (%d,%d,%d)", gridp.x, gridp.y, gridp.z );
        return 0;
    }

    if( zlevels ) {
        const int indexz = gridp.z + OVERMAP_HEIGHT; // Can't be lower than 0
        return indexz + ( gridp.x + gridp.y * my_MAPSIZE ) * OVERMAP_LAYERS;
    } else {
        return gridp.x + gridp.y * my_MAPSIZE;
    }
}

void map::draw_line_ter( const ter_id &type, const point &p1, const point &p2,
                         bool avoid_creatures )
{
    draw_line( [this, type, avoid_creatures]( const point & p ) {
        this->ter_set( p, type, avoid_creatures );
    }, p1, p2 );
}

void map::draw_line_furn( const furn_id &type, const point &p1, const point &p2,
                          bool avoid_creatures )
{
    draw_line( [this, type, avoid_creatures]( const point & p ) {
        this->furn_set( p, type, avoid_creatures );
    }, p1, p2 );
}

void map::draw_fill_background( const ter_id &type )
{
    // Need to explicitly set caches dirty - set_ter would do it before
    set_transparency_cache_dirty( abs_sub.z() );
    set_seen_cache_dirty( abs_sub.z() );
    set_outside_cache_dirty( abs_sub.z() );
    set_pathfinding_cache_dirty( abs_sub.z() );

    // Fill each submap rather than each tile
    for( int gridx = 0; gridx < my_MAPSIZE; gridx++ ) {
        for( int gridy = 0; gridy < my_MAPSIZE; gridy++ ) {
            submap *sm = get_submap_at_grid( {gridx, gridy} );
            if( sm == nullptr ) {
                debugmsg( "Tried to fill background at (%d,%d) but the submap is not loaded", gridx, gridy );
                continue;
            }
            sm->set_all_ter(type, true);
        }
    }
}

void map::draw_fill_background( ter_id( *f )() )
{
    draw_square_ter( f, point_zero, point( SEEX * my_MAPSIZE - 1, SEEY * my_MAPSIZE - 1 ) );
}
void map::draw_fill_background( const weighted_int_list<ter_id> &f )
{
    draw_square_ter( f, point_zero, point( SEEX * my_MAPSIZE - 1, SEEY * my_MAPSIZE - 1 ) );
}

void map::draw_square_ter( const ter_id &type, const point &p1, const point &p2,
                           bool avoid_creatures )
{
    draw_square( [this, type, avoid_creatures]( const point & p ) {
        this->ter_set( p, type, avoid_creatures );
    }, p1, p2 );
}

void map::draw_square_furn( const furn_id &type, const point &p1, const point &p2,
                            bool avoid_creatures )
{
    draw_square( [this, type, avoid_creatures]( const point & p ) {
        this->furn_set( p, type, avoid_creatures );
    }, p1, p2 );
}

void map::draw_square_ter( ter_id( *f )(), const point &p1, const point &p2, bool avoid_creatures )
{
    draw_square( [this, f, avoid_creatures]( const point & p ) {
        this->ter_set( p, f(), avoid_creatures );
    }, p1, p2 );
}

void map::draw_square_ter( const weighted_int_list<ter_id> &f, const point &p1,
                           const point &p2, bool avoid_creatures )
{
    draw_square( [this, f, avoid_creatures]( const point & p ) {
        const ter_id *tid = f.pick();
        this->ter_set( p, tid != nullptr ? *tid : t_null, avoid_creatures );
    }, p1, p2 );
}

void map::draw_rough_circle_ter( const ter_id &type, const point &p, int rad )
{
    draw_rough_circle( [this, type]( const point & q ) {
        this->ter_set( q, type );
    }, p, rad );
}

void map::draw_rough_circle_furn( const furn_id &type, const point &p, int rad )
{
    draw_rough_circle( [this, type]( const point & q ) {
        if( !is_open_air( tripoint( q, abs_sub.z() ) ) ) {
            this->furn_set( q, type );
        }
    }, p, rad );
}

void map::draw_circle_ter( const ter_id &type, const rl_vec2d &p, double rad )
{
    draw_circle( [this, type]( const point & q ) {
        this->ter_set( q, type );
    }, p, rad );
}

void map::draw_circle_ter( const ter_id &type, const point &p, int rad )
{
    draw_circle( [this, type]( const point & q ) {
        this->ter_set( q, type );
    }, p, rad );
}

void map::draw_circle_furn( const furn_id &type, const point &p, int rad )
{
    draw_circle( [this, type]( const point & q ) {
        this->furn_set( q, type );
    }, p, rad );
}

void map::add_corpse( const tripoint &p )
{
    item body;

    const bool isReviveSpecial = one_in( 10 );

    if( !isReviveSpecial ) {
        body = item::make_corpse();
    } else {
        body = item::make_corpse( mon_zombie );
        body.set_flag( flag_REVIVE_SPECIAL );
    }

    put_items_from_loc( Item_spawn_data_default_zombie_clothes, p, calendar::turn_zero );
    if( one_in( 3 ) ) {
        put_items_from_loc( Item_spawn_data_default_zombie_items, p, calendar::turn_zero );
    }

    add_item_or_charges( p, body );
}

field &map::get_field( const tripoint &p )
{
    return field_at( p );
}

void map::creature_on_trap( Creature &c, const bool may_avoid ) const
{
    // boarded in a vehicle means the player is above the trap, like a flying monster and can
    // never trigger the trap.
    const Character *const you = c.as_character();
    if( you != nullptr && you->in_vehicle ) {
        return;
    }
    maybe_trigger_trap( c.pos(), c, may_avoid );
}

void map::maybe_trigger_trap( const tripoint &pos, Creature &c, const bool may_avoid ) const
{
    const trap &tr = tr_at( pos );
    if( tr.is_null() ) {
        return;
    }

    //Don't trigger benign traps like cots and funnels
    if( tr.is_benign() ) {
        return;
    }

    if( tr.has_flag( json_flag_AVATAR_ONLY ) && !c.is_avatar() ) {
        return;
    }

    if( !tr.has_flag( json_flag_UNDODGEABLE ) && may_avoid && c.avoid_trap( pos, tr ) ) {
        Character *const pl = c.as_character();
        if( !tr.is_always_invisible() && pl && !pl->knows_trap( pos ) ) {
            pl->add_msg_if_player( _( "You've spotted a %1$s!" ), tr.name() );
            pl->add_known_trap( pos, tr );
        }
        return;
    }

    if( !tr.is_always_invisible() && tr.has_trigger_msg() ) {
        c.add_msg_player_or_npc( m_bad, tr.get_trigger_message_u(), tr.get_trigger_message_npc(),
                                 tr.name() );
    }
    tr.trigger( c.pos(), c );
}

void map::maybe_trigger_trap( const tripoint_bub_ms &pos, Creature &c, const bool may_avoid ) const
{
    maybe_trigger_trap( pos.raw(), c, may_avoid );
}

template<typename Functor>
void map::function_over( const tripoint &start, const tripoint &end, Functor fun ) const
{
    // start and end are just two points, end can be "before" start
    // Also clip the area to map area
    const tripoint min( std::max( std::min( start.x, end.x ), 0 ), std::max( std::min( start.y, end.y ),
                        0 ), std::max( std::min( start.z, end.z ), -OVERMAP_DEPTH ) );
    const tripoint max( std::min( std::max( start.x, end.x ), SEEX * my_MAPSIZE - 1 ),
                        std::min( std::max( start.y, end.y ), SEEY * my_MAPSIZE - 1 ), std::min( std::max( start.z, end.z ),
                                OVERMAP_HEIGHT ) );

    // Submaps that contain the bounding points
    const point min_sm( min.x / SEEX, min.y / SEEY );
    const point max_sm( max.x / SEEX, max.y / SEEY );
    // Z outermost, because submaps are flat
    tripoint gp;
    int &z = gp.z;
    int &smx = gp.x;
    int &smy = gp.y;
    for( z = min.z; z <= max.z; z++ ) {
        for( smx = min_sm.x; smx <= max_sm.x; smx++ ) {
            for( smy = min_sm.y; smy <= max_sm.y; smy++ ) {
                submap const *cur_submap = get_submap_at_grid( { smx, smy, z } );
                if( cur_submap == nullptr ) {
                    debugmsg( "Tried to function over (%d,%d,%d) but the submap is not loaded", smx, smy, z );
                    continue;
                }
                // Bounds on the submap coordinates
                const point sm_min( smx > min_sm.x ? 0 : min.x % SEEX, smy > min_sm.y ? 0 : min.y % SEEY );
                const point sm_max( smx < max_sm.x ? SEEX - 1 : max.x % SEEX,
                                    smy < max_sm.y ? SEEY - 1 : max.y % SEEY );

                point lp;
                int &sx = lp.x;
                int &sy = lp.y;
                for( sx = sm_min.x; sx <= sm_max.x; ++sx ) {
                    for( sy = sm_min.y; sy <= sm_max.y; ++sy ) {
                        const iteration_state rval = fun( gp, cur_submap, lp );
                        if( rval != ITER_CONTINUE ) {
                            switch( rval ) {
                                case ITER_SKIP_ZLEVEL:
                                    smx = my_MAPSIZE + 1;
                                    smy = my_MAPSIZE + 1;
                                // Fall through
                                case ITER_SKIP_SUBMAP:
                                    sx = SEEX;
                                    sy = SEEY;
                                    break;
                                default:
                                    return;
                            }
                        }
                    }
                }
            }
        }
    }
}

void map::scent_blockers( std::array<std::array<bool, MAPSIZE_X>, MAPSIZE_Y> &blocks_scent,
                          std::array<std::array<bool, MAPSIZE_X>, MAPSIZE_Y> &reduces_scent,
                          const point &min, const point &max )
{
    ter_furn_flag reduce = ter_furn_flag::TFLAG_REDUCE_SCENT;
    ter_furn_flag block = ter_furn_flag::TFLAG_NO_SCENT;
    auto fill_values = [&]( const tripoint & gp, const submap * sm, const point & lp ) {
        // We need to generate the x/y coordinates, because we can't get them "for free"
        const point p = lp + sm_to_ms_copy( gp.xy() );
        if( sm->get_ter( lp ).obj().has_flag( block ) ) {
            blocks_scent[p.x][p.y] = true;
            reduces_scent[p.x][p.y] = false;
        } else if( sm->get_ter( lp ).obj().has_flag( reduce ) ||
                   sm->get_furn( lp ).obj().has_flag( reduce ) ) {
            blocks_scent[p.x][p.y] = false;
            reduces_scent[p.x][p.y] = true;
        } else {
            blocks_scent[p.x][p.y] = false;
            reduces_scent[p.x][p.y] = false;
        }

        return ITER_CONTINUE;
    };

    function_over( tripoint( min, abs_sub.z() ), tripoint( max, abs_sub.z() ), fill_values );

    const inclusive_rectangle<point> local_bounds( min, max );

    // Now vehicles

    VehicleList vehs = get_vehicles();
    for( wrapped_vehicle &wrapped_veh : vehs ) {
        vehicle &veh = *( wrapped_veh.v );
        for( const vpart_reference &vp : veh.get_all_parts_with_fakes() ) {
            if( !vp.has_feature( VPFLAG_OBSTACLE ) &&
                ( !vp.has_feature( VPFLAG_OPENABLE ) || !vp.part().open ) ) {
                continue;
            }
            const tripoint part_pos = vp.pos();
            if( local_bounds.contains( part_pos.xy() ) ) {
                reduces_scent[part_pos.x][part_pos.y] = true;
            }
        }
    }
}

tripoint_range<tripoint> map::points_in_rectangle( const tripoint &from, const tripoint &to ) const
{
    const tripoint min( std::max( 0, std::min( from.x, to.x ) ), std::max( 0, std::min( from.y,
                        to.y ) ), std::max( -OVERMAP_DEPTH, std::min( from.z, to.z ) ) );
    const tripoint max( std::min( SEEX * my_MAPSIZE - 1, std::max( from.x, to.x ) ),
                        std::min( SEEX * my_MAPSIZE - 1, std::max( from.y, to.y ) ), std::min( OVERMAP_HEIGHT,
                                std::max( from.z, to.z ) ) );
    return tripoint_range<tripoint>( min, max );
}

tripoint_range<tripoint> map::points_in_radius( const tripoint &center, size_t radius,
        size_t radiusz ) const
{
    const tripoint min( std::max<int>( 0, center.x - radius ), std::max<int>( 0, center.y - radius ),
                        clamp<int>( center.z - radiusz, -OVERMAP_DEPTH, OVERMAP_HEIGHT ) );
    const tripoint max( std::min<int>( SEEX * my_MAPSIZE - 1, center.x + radius ),
                        std::min<int>( SEEX * my_MAPSIZE - 1, center.y + radius ), clamp<int>( center.z + radiusz,
                                -OVERMAP_DEPTH, OVERMAP_HEIGHT ) );
    return tripoint_range<tripoint>( min, max );
}

tripoint_range<tripoint_bub_ms> map::points_in_radius(
    const tripoint_bub_ms &center, size_t radius, size_t radiusz ) const
{
    const tripoint_bub_ms min(
        std::max<int>( 0, center.x() - radius ), std::max<int>( 0, center.y() - radius ),
        clamp<int>( center.z() - radiusz, -OVERMAP_DEPTH, OVERMAP_HEIGHT ) );
    const tripoint_bub_ms max(
        std::min<int>( SEEX * my_MAPSIZE - 1, center.x() + radius ),
        std::min<int>( SEEX * my_MAPSIZE - 1, center.y() + radius ),
        clamp<int>( center.z() + radiusz, -OVERMAP_DEPTH, OVERMAP_HEIGHT ) );
    return tripoint_range<tripoint_bub_ms>( min, max );
}

tripoint_range<tripoint> map::points_on_zlevel( const int z ) const
{
    if( z < -OVERMAP_DEPTH || z > OVERMAP_HEIGHT ) {
        // TODO: need a default constructor that creates an empty range.
        return tripoint_range<tripoint>( tripoint_zero, tripoint_zero - tripoint_above );
    }
    return tripoint_range<tripoint>(
               tripoint( 0, 0, z ), tripoint( SEEX * my_MAPSIZE - 1, SEEY * my_MAPSIZE - 1, z ) );
}

tripoint_range<tripoint> map::points_on_zlevel() const
{
    return points_on_zlevel( abs_sub.z() );
}

std::list<item_location> map::get_active_items_in_radius(
    const tripoint &center, int radius )
{
    return get_active_items_in_radius( center, radius, special_item_type::none );
}

std::list<item_location> map::get_active_items_in_radius( const tripoint &center, int radius,
        special_item_type type )
{
    std::list<item_location> result;

    const point minp( center.xy() + point( -radius, -radius ) );
    const point maxp( center.xy() + point( radius, radius ) );

    const point ming( std::max( minp.x / SEEX, 0 ),
                      std::max( minp.y / SEEY, 0 ) );
    const point maxg( std::min( maxp.x / SEEX, my_MAPSIZE - 1 ),
                      std::min( maxp.y / SEEY, my_MAPSIZE - 1 ) );

    for( const tripoint_abs_sm &abs_submap_loc : submaps_with_active_items ) {
        // TODO: fix point types
        const tripoint submap_loc = ( abs_submap_loc - abs_sub.xy() ).raw();
        if( submap_loc.x < ming.x || submap_loc.y < ming.y ||
            submap_loc.x > maxg.x || submap_loc.y > maxg.y ) {
            continue;
        }
        const point sm_offset( submap_loc.x * SEEX, submap_loc.y * SEEY );

        submap *sm = get_submap_at_grid( submap_loc );
        if( sm == nullptr ) {
            debugmsg( "Tried get active items in radius of (%d,%d,%d) but the submap is not loaded",
                      submap_loc.x, submap_loc.y, submap_loc.z );
            continue;
        }
        std::vector<item_reference> items = type == special_item_type::none ? sm->active_items.get() :
                                            sm->active_items.get_special( type );
        for( const item_reference &elem : items ) {
            const tripoint pos( sm_offset + elem.location, submap_loc.z );

            if( rl_dist( pos, center ) > radius ) {
                continue;
            }

            if( elem.item_ref ) {
                result.emplace_back( map_cursor( pos ), elem.item_ref.get() );
            }
        }
    }

    return result;
}

std::list<tripoint> map::find_furnitures_with_flag_in_radius( const tripoint &center,
        size_t radius,
        const std::string &flag,
        size_t radiusz ) const
{
    std::list<tripoint> furn_locs;
    for( const tripoint &furn_loc : points_in_radius( center, radius, radiusz ) ) {
        if( has_flag_furn( flag, furn_loc ) ) {
            furn_locs.push_back( furn_loc );
        }
    }
    return furn_locs;
}

std::list<tripoint> map::find_furnitures_with_flag_in_radius( const tripoint &center,
        size_t radius,
        const ter_furn_flag flag,
        size_t radiusz ) const
{
    std::list<tripoint> furn_locs;
    for( const tripoint &furn_loc : points_in_radius( center, radius, radiusz ) ) {
        if( has_flag_furn( flag, furn_loc ) ) {
            furn_locs.push_back( furn_loc );
        }
    }
    return furn_locs;
}

std::list<Creature *> map::get_creatures_in_radius( const tripoint &center, size_t radius,
        size_t radiusz ) const
{
    creature_tracker &creatures = get_creature_tracker();
    std::list<Creature *> creature_list;
    for( const tripoint &loc : points_in_radius( center, radius, radiusz ) ) {
        Creature *tmp_critter = creatures.creature_at( loc );
        if( tmp_critter != nullptr ) {
            creature_list.push_back( tmp_critter );
        }

    }
    return creature_list;
}

level_cache &map::access_cache( int zlev )
{
    if( zlev >= -OVERMAP_DEPTH && zlev <= OVERMAP_HEIGHT ) {
        std::unique_ptr<level_cache> &cache = caches[zlev + OVERMAP_DEPTH];
        if( !cache ) {
            cache = std::make_unique<level_cache>();
        }
        return *cache;
    }

    debugmsg( "access_cache called with invalid z-level: %d", zlev );
    return nullcache;
}

const level_cache &map::access_cache( int zlev ) const
{
    if( zlev >= -OVERMAP_DEPTH && zlev <= OVERMAP_HEIGHT ) {
        std::unique_ptr<level_cache> &cache = caches[zlev + OVERMAP_DEPTH];
        if( !cache ) {
            cache = std::make_unique<level_cache>();
        }
        return *cache;
    }

    debugmsg( "access_cache called with invalid z-level: %d", zlev );
    return nullcache;
}

pathfinding_cache::pathfinding_cache()
{
    dirty = true;
}

pathfinding_cache &map::get_pathfinding_cache( int zlev ) const
{
    return *pathfinding_caches[zlev + OVERMAP_DEPTH];
}

void map::set_pathfinding_cache_dirty( const int zlev )
{
    if( inbounds_z( zlev ) ) {
        get_pathfinding_cache( zlev ).dirty = true;
    }
}

void map::queue_main_cleanup()
{
    if( this != &get_map() ) {
        _main_requires_cleanup = true;
    }
}

bool map::is_main_cleanup_queued() const
{
    return _main_requires_cleanup;
}

void map::main_cleanup_override( bool over )
{
    _main_cleanup_override = over;
}

const pathfinding_cache &map::get_pathfinding_cache_ref( int zlev ) const
{
    if( !inbounds_z( zlev ) ) {
        debugmsg( "Tried to get pathfinding cache for out of bounds z-level %d", zlev );
        return *pathfinding_caches[ OVERMAP_DEPTH ];
    }
    pathfinding_cache &cache = get_pathfinding_cache( zlev );
    if( cache.dirty ) {
        update_pathfinding_cache( zlev );
    }

    return cache;
}

void map::update_pathfinding_cache( int zlev ) const
{
    pathfinding_cache &cache = get_pathfinding_cache( zlev );
    if( !cache.dirty ) {
        return;
    }

    std::uninitialized_fill_n( &cache.special[0][0], MAPSIZE_X * MAPSIZE_Y, PF_NORMAL );

    for( int smx = 0; smx < my_MAPSIZE; ++smx ) {
        for( int smy = 0; smy < my_MAPSIZE; ++smy ) {
            const submap *cur_submap = get_submap_at_grid( { smx, smy, zlev } );
            if( !cur_submap ) {
                return;
            }

            tripoint p( 0, 0, zlev );

            for( int sx = 0; sx < SEEX; ++sx ) {
                p.x = sx + smx * SEEX;
                for( int sy = 0; sy < SEEY; ++sy ) {
                    p.y = sy + smy * SEEY;

                    pf_special cur_value = PF_NORMAL;

                    const_maptile tile( cur_submap, point( sx, sy ) );

                    const ter_t &terrain = tile.get_ter_t();
                    const furn_t &furniture = tile.get_furn_t();
                    const field &field = tile.get_field();
                    int part;
                    const vehicle *veh = veh_at_internal( p, part );

                    const int cost = move_cost_internal( furniture, terrain, field, veh, part );

                    if( cost > 2 ) {
                        cur_value |= PF_SLOW;
                    } else if( cost <= 0 ) {
                        cur_value |= PF_WALL;
                        if( terrain.has_flag( ter_furn_flag::TFLAG_CLIMBABLE ) ) {
                            cur_value |= PF_CLIMBABLE;
                        }
                    }

                    if( veh != nullptr ) {
                        cur_value |= PF_VEHICLE;
                    }

                    for( const auto &fld : tile.get_field() ) {
                        const field_entry &cur = fld.second;
                        if( cur.is_dangerous() ) {
                            cur_value |= PF_FIELD;
                        }
                    }

                    if( !tile.get_trap_t().is_benign() || !terrain.trap.obj().is_benign() ) {
                        cur_value |= PF_TRAP;
                    }

                    if( terrain.has_flag( ter_furn_flag::TFLAG_GOES_DOWN ) ||
                        terrain.has_flag( ter_furn_flag::TFLAG_GOES_UP ) ||
                        terrain.has_flag( ter_furn_flag::TFLAG_RAMP ) || terrain.has_flag( ter_furn_flag::TFLAG_RAMP_UP ) ||
                        terrain.has_flag( ter_furn_flag::TFLAG_RAMP_DOWN ) ) {
                        cur_value |= PF_UPDOWN;
                    }

                    if( terrain.has_flag( ter_furn_flag::TFLAG_SHARP ) ) {
                        cur_value |= PF_SHARP;
                    }

                    cache.special[p.x][p.y] = cur_value;
                }
            }
        }
    }

    cache.dirty = false;
}

void map::clip_to_bounds( tripoint &p ) const
{
    clip_to_bounds( p.x, p.y, p.z );
}

void map::clip_to_bounds( int &x, int &y ) const
{
    if( x < 0 ) {
        x = 0;
    } else if( x >= SEEX * my_MAPSIZE ) {
        x = SEEX * my_MAPSIZE - 1;
    }

    if( y < 0 ) {
        y = 0;
    } else if( y >= SEEY * my_MAPSIZE ) {
        y = SEEY * my_MAPSIZE - 1;
    }
}

void map::clip_to_bounds( int &x, int &y, int &z ) const
{
    clip_to_bounds( x, y );
    if( z < -OVERMAP_DEPTH ) {
        z = -OVERMAP_DEPTH;
    } else if( z > OVERMAP_HEIGHT ) {
        z = OVERMAP_HEIGHT;
    }
}

bool map::is_cornerfloor( const tripoint &p ) const
{
    if( impassable( p ) ) {
        return false;
    }
    std::set<tripoint> impassable_adjacent;
    for( const tripoint &pt : points_in_radius( p, 1 ) ) {
        if( impassable( pt ) ) {
            impassable_adjacent.insert( pt );
        }
    }
    if( !impassable_adjacent.empty() ) {
        //to check if a floor is a corner we first search if any of its diagonal adjacent points is impassable
        std::set< tripoint> diagonals = { p + tripoint_north_east, p + tripoint_north_west, p + tripoint_south_east, p + tripoint_south_west };
        for( const tripoint &impassable_diagonal : diagonals ) {
            if( impassable_adjacent.count( impassable_diagonal ) != 0 ) {
                //for every impassable diagonal found, we check if that diagonal terrain has at least two impassable neighbors that also neighbor point p
                int f = 0;
                for( const tripoint &l : points_in_radius( impassable_diagonal, 1 ) ) {
                    if( impassable_adjacent.count( l ) != 0 ) {
                        f++;
                    }
                    if( f > 2 ) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

int map::calc_max_populated_zlev()
{
    // cache is filled and valid, skip recalculation
    if( max_populated_zlev && max_populated_zlev->first == get_abs_sub() ) {
        return max_populated_zlev->second;
    }

    // We'll assume ground level is populated
    int max_z = 0;

    for( int sz = 1; sz <= OVERMAP_HEIGHT; sz++ ) {
        bool level_done = false;
        for( int sx = 0; sx < my_MAPSIZE; sx++ ) {
            for( int sy = 0; sy < my_MAPSIZE; sy++ ) {
                const submap *sm = get_submap_at_grid( tripoint( sx, sy, sz ) );
                if( sm == nullptr ) {
                    debugmsg( "Tried to calc max populated zlev at (%d,%d,%d) but the submap is not loaded", sx, sy,
                              sz );
                    continue;
                }
                if( !sm->is_uniform() ) {
                    max_z = sz;
                    level_done = true;
                    break;
                }
            }
            if( level_done ) {
                break;
            }
        }
    }

    max_populated_zlev = std::make_pair( get_abs_sub(), max_z );
    return max_z;
}

void map::invalidate_max_populated_zlev( int zlev )
{
    if( max_populated_zlev && max_populated_zlev->second < zlev ) {
        max_populated_zlev->second = zlev;
    }
}

bool map::has_potential_los( const tripoint &from, const tripoint &to,
                             bool bounds_check ) const
{
    if( bounds_check && ( !inbounds( from ) || !inbounds( to ) ) ) {
        return false;
    }
    if( from.z == to.z ) {
        level_cache &cache = get_cache( from.z );
        return cache.r_hor_cache->has_potential_los( from.xy(), to.xy(), cache ) &&
               cache.r_hor_cache->has_potential_los( to.xy(), from.xy(), cache ) ;
    }
    tripoint upper;
    tripoint lower;
    std::tie( upper, lower ) = from.z > to.z ? std::make_pair( from, to ) : std::make_pair( to, from );
    // z-bounds depend on the invariant that both points are inbounds and their z are different
    return get_cache( lower.z ).r_up_cache->has_potential_los(
               lower.xy(), upper.xy(), get_cache( lower.z ), get_cache( lower.z + 1 ) );
}

// Get cache value for debug purposes
int map::reachability_cache_value( const tripoint &p, bool vertical_cache,
                                   reachability_cache_quadrant quadrant ) const
{
    if( !inbounds( p ) ) {
        return -2;
    }

    // rebuild caches, so valid values are shown
    has_potential_los( p, p ); // rebuild horizontal cache;
    has_potential_los( p, p + tripoint_above ); // rebuild "up" cache

    const level_cache &lc = get_cache( p.z );
    if( vertical_cache ) {
        return lc.r_up_cache->get_value( quadrant, p.xy() );
    } else {
        return lc.r_hor_cache->get_value( quadrant, p.xy() );
    }
}

static bool is_haulable( const item &it )
{
    // Liquid cannot be picked up
    return !it.made_of_from_type( phase_id::LIQUID );
}

bool map::has_haulable_items( const tripoint &pos )
{
    const map_stack items = i_at( pos );
    for( const item &it : items ) {
        if( is_haulable( it ) ) {
            return true;
        }
    }
    return false;
}

std::vector<item_location> map::get_haulable_items( const tripoint &pos )
{
    std::vector<item_location> target_items;
    map_stack items = i_at( pos );
    target_items.reserve( items.size() );
    for( item &it : items ) {
        if( is_haulable( it ) ) {
            target_items.emplace_back( map_cursor( pos ), &it );
        }
    }
    return target_items;
}

tripoint drawsq_params::center() const
{
    if( view_center == tripoint_min ) {
        avatar &player_character = get_avatar();
        return player_character.pos() + player_character.view_offset;
    } else {
        return view_center;
    }
}
