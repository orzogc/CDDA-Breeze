#pragma once
#ifndef CATA_SRC_SCENT_MAP_H
#define CATA_SRC_SCENT_MAP_H

#include <array>
#include <iosfwd>
#include <set>
#include <vector>

#include "calendar.h"
#include "enums.h" // IWYU pragma: keep
#include "game_constants.h"
#include <optional>
#include "point.h"
#include "type_id.h"

class JsonObject;

static constexpr int SCENT_MAP_Z_REACH = 1;

class game;
class map;

namespace catacurses
{
class window;
} // namespace catacurses

class scent_type
{
    public:
        static void load_scent_type( const JsonObject &jo, const std::string &src );
        void load( const JsonObject &jo, const std::string & );
        static const std::vector<scent_type> &get_all();
        static void check_scent_consistency();
        bool was_loaded = false;

        scenttype_id id;
        std::vector<std::pair<scenttype_id, mod_id>> src;
        std::set<species_id> receptive_species;
        static void reset();
};

class scent_map
{
    protected:
        template<typename T>
        using scent_array = std::array<std::array<T, MAPSIZE_Y>, MAPSIZE_X>;

        scent_array<int> grscent;
        scenttype_id typescent;
        std::optional<tripoint> player_last_position; // NOLINT(cata-serialize)
        time_point player_last_moved = calendar::before_time_starts; // NOLINT(cata-serialize)

        const game &gm; // NOLINT(cata-serialize)

    public:
        explicit scent_map( const game &g ) : gm( g ) { }

        void deserialize( const std::string &data, bool is_type = false );
        std::string serialize( bool is_type = false ) const;

        void draw( const catacurses::window &win, int div, const tripoint &center ) const;

        void update( const tripoint &center, map &m );
        void reset();
        void decay();
        void shift( const point &sm_shift );

        /**
         * Get the scent value at the given position.
         * An invalid position is allows and will yield a 0 value.
         * The coordinate system is the same as the @ref map (`g->m`) uses.
         */
        /**@{*/
        void set( const tripoint &p, int value, const scenttype_id &type = scenttype_id() );
        int get( const tripoint &p ) const;
        /**@}*/
        void set_unsafe( const tripoint &p, int value, const scenttype_id &type = scenttype_id() );
        int get_unsafe( const tripoint &p ) const;

        scenttype_id get_type() const;
        scenttype_id get_type( const tripoint &p ) const;

        bool inbounds( const tripoint &p ) const;
        bool inbounds( const point &p ) const;
};

scent_map &get_scent();

#endif // CATA_SRC_SCENT_MAP_H
