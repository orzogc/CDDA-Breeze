#include "dialogue_chatbin.h"

#include <algorithm>
#include <iterator>

#include "mission.h"

void dialogue_chatbin::add_new_mission( mission *miss )
{
    if( miss == nullptr ) {
        return;
    }
    missions.push_back( miss );
}

void dialogue_chatbin::check_missions()
{
    // TODO: or simply fail them? Some missions might only need to be reported.
    auto &ma = missions_assigned;
    const auto last = std::remove_if( ma.begin(), ma.end(), []( class mission const * m ) {
        return !m->is_assigned();
    } );
    std::copy( last, ma.end(), std::back_inserter( missions ) );
    ma.erase( last, ma.end() );
}

void dialogue_chatbin::store_chosen_training( const skill_id &c_skill, const matype_id &c_style,
        const spell_id &c_spell, const proficiency_id &c_proficiency )
{
    if( c_skill == skill_id() && c_style == matype_id() && c_spell == spell_id() &&
        c_proficiency == proficiency_id() ) {
        return;
    }
    clear_training();
    if( c_skill != skill_id() ) {
        skill = c_skill;
    } else if( c_style != matype_id() ) {
        style = c_style;
    } else if( c_spell != spell_id() ) {
        dialogue_spell = c_spell;
    } else if( c_proficiency != proficiency_id() ) {
        proficiency = c_proficiency;
    }
}

void dialogue_chatbin::clear_training()
{
    style = matype_id();
    skill = skill_id();
    proficiency = proficiency_id();
    dialogue_spell = spell_id();
}

namespace
{
using speech_member = std::string dialogue_chatbin::*;

const std::map<std::string, speech_member> &npc_speech_fields()
{
    static const std::map<std::string, speech_member> fields = {
        { "camp_food_thanks", &dialogue_chatbin::snip_camp_food_thanks },
        { "camp_larder_empty", &dialogue_chatbin::snip_camp_larder_empty },
        { "camp_water_thanks", &dialogue_chatbin::snip_camp_water_thanks },
        { "cant_flee", &dialogue_chatbin::snip_cant_flee },
        { "close_distance", &dialogue_chatbin::snip_close_distance },
        { "combat_noise_warning", &dialogue_chatbin::snip_combat_noise_warning },
        { "danger_close_distance", &dialogue_chatbin::snip_danger_close_distance },
        { "done_mugging", &dialogue_chatbin::snip_done_mugging },
        { "far_distance", &dialogue_chatbin::snip_far_distance },
        { "fire_bad", &dialogue_chatbin::snip_fire_bad },
        { "fire_in_the_hole_h", &dialogue_chatbin::snip_fire_in_the_hole_h },
        { "fire_in_the_hole", &dialogue_chatbin::snip_fire_in_the_hole },
        { "general_danger_h", &dialogue_chatbin::snip_general_danger_h },
        { "general_danger", &dialogue_chatbin::snip_general_danger },
        { "heal_self", &dialogue_chatbin::snip_heal_self },
        { "hungry", &dialogue_chatbin::snip_hungry },
        { "im_leaving_you", &dialogue_chatbin::snip_im_leaving_you },
        { "its_safe_h", &dialogue_chatbin::snip_its_safe_h },
        { "its_safe", &dialogue_chatbin::snip_its_safe },
        { "keep_up", &dialogue_chatbin::snip_keep_up },
        { "kill_npc_h", &dialogue_chatbin::snip_kill_npc_h },
        { "kill_npc", &dialogue_chatbin::snip_kill_npc },
        { "kill_player_h", &dialogue_chatbin::snip_kill_player_h },
        { "let_me_pass", &dialogue_chatbin::snip_let_me_pass },
        { "lets_talk", &dialogue_chatbin::snip_lets_talk },
        { "medium_distance", &dialogue_chatbin::snip_medium_distance },
        { "monster_warning_h", &dialogue_chatbin::snip_monster_warning_h },
        { "monster_warning", &dialogue_chatbin::snip_monster_warning },
        { "movement_noise_warning", &dialogue_chatbin::snip_movement_noise_warning },
        { "need_batteries", &dialogue_chatbin::snip_need_batteries },
        { "need_booze", &dialogue_chatbin::snip_need_booze },
        { "need_fuel", &dialogue_chatbin::snip_need_fuel },
        { "no_to_thorazine", &dialogue_chatbin::snip_no_to_thorazine },
        { "run_away", &dialogue_chatbin::snip_run_away },
        { "speech_warning", &dialogue_chatbin::snip_speech_warning },
        { "thirsty", &dialogue_chatbin::snip_thirsty },
        { "wait", &dialogue_chatbin::snip_wait },
        { "warn_sleep", &dialogue_chatbin::snip_warn_sleep },
        { "yawn", &dialogue_chatbin::snip_yawn },
        { "yes_to_lsd", &dialogue_chatbin::snip_yes_to_lsd },
        { "mug_dontmove", &dialogue_chatbin::snip_mug_dontmove },
        { "lost_blood", &dialogue_chatbin::snip_lost_blood },
        { "pulp_zombie", &dialogue_chatbin::snip_pulp_zombie },
        { "heal_player", &dialogue_chatbin::snip_heal_player },
        { "wound_infected", &dialogue_chatbin::snip_wound_infected },
        { "wound_bite", &dialogue_chatbin::snip_wound_bite },
        { "bleeding", &dialogue_chatbin::snip_bleeding },
        { "bleeding_badly", &dialogue_chatbin::snip_bleeding_badly },
        { "radiation_sickness", &dialogue_chatbin::snip_radiation_sickness },
        { "acknowledged", &dialogue_chatbin::snip_acknowledged },
        { "bye", &dialogue_chatbin::snip_bye },
        { "consume_cant_accept", &dialogue_chatbin::snip_consume_cant_accept },
        { "consume_cant_consume", &dialogue_chatbin::snip_consume_cant_consume },
        { "consume_rotten", &dialogue_chatbin::snip_consume_rotten },
        { "consume_eat", &dialogue_chatbin::snip_consume_eat },
        { "consume_need_item", &dialogue_chatbin::snip_consume_need_item },
        { "consume_med", &dialogue_chatbin::snip_consume_med },
        { "consume_nocharge", &dialogue_chatbin::snip_consume_nocharge },
        { "consume_use_med", &dialogue_chatbin::snip_consume_use_med },
        { "give_nope", &dialogue_chatbin::snip_give_nope },
        { "give_to_hallucination", &dialogue_chatbin::snip_give_to_hallucination },
        { "give_cancel", &dialogue_chatbin::snip_give_cancel },
        { "give_dangerous", &dialogue_chatbin::snip_give_dangerous },
        { "give_wield", &dialogue_chatbin::snip_give_wield },
        { "give_weapon_weak", &dialogue_chatbin::snip_give_weapon_weak },
        { "give_carry", &dialogue_chatbin::snip_give_carry },
        { "give_carry_cant", &dialogue_chatbin::snip_give_carry_cant },
        { "give_carry_cant_few_space", &dialogue_chatbin::snip_give_carry_cant_few_space },
        { "give_carry_cant_no_space", &dialogue_chatbin::snip_give_carry_cant_no_space },
        { "give_carry_too_heavy", &dialogue_chatbin::snip_give_carry_too_heavy },
        { "wear", &dialogue_chatbin::snip_wear },
        { "mutiny", &dialogue_chatbin::snip_mutiny },
        { "pickup_item", &dialogue_chatbin::snip_pickup_item },
        { "no_dropoff", &dialogue_chatbin::snip_no_dropoff },
        { "socialize", &dialogue_chatbin::snip_socialize },
    };
    return fields;
}
} // namespace

bool dialogue_chatbin::set_speech_override( const std::string &key, const std::string &value )
{
    const auto &fields = npc_speech_fields();
    const auto found = fields.find( key );
    if( found == fields.end() ) {
        return false;
    }
    this->*( found->second ) = value;
    return true;
}

void dialogue_chatbin::apply_speech_overrides(
    const std::map<std::string, std::string> &overrides )
{
    for( const auto &entry : overrides ) {
        set_speech_override( entry.first, entry.second );
    }
}

void dialogue_chatbin::clear_all()
{
    clear_training();
    missions.clear();
    missions_assigned.clear();
    mission_selected = nullptr;
}
