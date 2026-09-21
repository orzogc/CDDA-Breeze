#pragma once
#ifndef CATA_SRC_AMMO_EFFECT_H
#define CATA_SRC_AMMO_EFFECT_H

#include <cstddef>
#include <iosfwd>
#include <set>
#include <string>
#include <vector>

#include "calendar.h"
#include "enum_traits.h"
#include "enums.h"
#include "explosion.h"
#include "field_type.h"
#include "translations.h"
#include "type_id.h"

class JsonObject;

generic_factory<ammo_effect> &get_all_ammo_effects();

enum class ammo_target_condition : int {
    DAMAGED,
    NO_DAMAGE,

    LAST
};

template<>
struct enum_traits<ammo_target_condition> {
    static constexpr ammo_target_condition last = ammo_target_condition::LAST;
};

struct ammo_effect_target {
    int chance = 100;
    std::set<ammo_target_condition> conditions;
    efftype_id effect_type;
    std::string effect_type_name;
    time_duration effect_duration = 0_turns;
    bool effect_permanent = false;
    translation message;
    translation message_npc;
    game_message_type message_type = m_neutral;
};

struct ammo_effect {
    public:
        void load( const JsonObject &jo, const std::string &src );
        void finalize();
        void check() const;

        field_type_id aoe_field_type = fd_null.id_or( INVALID_FIELD_TYPE_ID );
        /** used during JSON loading only */
        std::string aoe_field_type_name = "fd_null";
        int aoe_intensity_min = 0;
        int aoe_intensity_max = 0;
        int aoe_radius = 1;
        int aoe_radius_z = 0;
        int aoe_chance = 100;
        int aoe_size = 0;
        explosion_data aoe_explosion_data;
        bool aoe_check_passable = false;

        bool aoe_check_sees = false;
        int aoe_check_sees_radius = 0;
        bool do_flashbang = false;
        bool do_emp_blast = false;
        bool foamcrete_build = false;

        ammo_effect_target target;

        field_type_id trail_field_type = fd_null.id_or( INVALID_FIELD_TYPE_ID );
        /** used during JSON loading only */
        std::string trail_field_type_name = "fd_null";
        int trail_intensity_min = 0;
        int trail_intensity_max = 0;
        int trail_chance = 100;

        // Used by generic_factory
        string_id<ammo_effect> id;
        std::vector<std::pair<string_id<ammo_effect>, mod_id>> src;
        bool was_loaded = false;

        static size_t count();
};

namespace ammo_effects
{

void load( const JsonObject &jo, const std::string &src );
void finalize_all();
void check_consistency();
void reset();

const std::vector<ammo_effect> &get_all();

} // namespace ammo_effects

extern ammo_effect_id AE_NULL;

#endif // CATA_SRC_AMMO_EFFECT_H
