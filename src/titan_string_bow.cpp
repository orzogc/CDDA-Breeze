#include "magic.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "calendar.h"
#include "character.h"
#include "flag.h"
#include "item.h"
#include "item_location.h"
#include "messages.h"
#include "skill.h"
#include "type_id.h"

static const efftype_id effect_travel_titan_flourish_lock( "travel_titan_flourish_lock" );
static const flag_id flag_TRAVEL_TITAN_STRING_BOW( "TRAVEL_TITAN_STRING_BOW" );
static const skill_id skill_archery( "archery" );

static bool load_titan_arrow( Character &shooter, item_location &weapon,
                              const std::string &preferred_ammo )
{
    if( weapon->ammo_sufficient( &shooter ) ) {
        return true;
    }

    std::vector<item_location> ammo = shooter.find_ammo( *weapon, true, -1 );
    if( !preferred_ammo.empty() ) {
        const auto preferred = std::find_if( ammo.begin(), ammo.end(), [&]( const item_location &candidate ) {
            return candidate && candidate->typeId().str() == preferred_ammo;
        } );
        if( preferred != ammo.end() &&
            weapon->reload( shooter, *preferred, weapon->ammo_required() ) ) {
            return weapon->ammo_sufficient( &shooter );
        }
    }

    for( item_location &candidate : ammo ) {
        if( weapon->reload( shooter, candidate, weapon->ammo_required() ) ) {
            return weapon->ammo_sufficient( &shooter );
        }
    }
    return false;
}

void spell_effect::titan_string_bow( const spell &sp, Creature &caster, const tripoint &target )
{
    Character *shooter = caster.as_character();
    if( shooter == nullptr ) {
        return;
    }

    item_location weapon = shooter->get_wielded_item();
    if( !weapon || !weapon->is_gun() || !weapon->has_flag( flag_TRAVEL_TITAN_STRING_BOW ) ) {
        shooter->add_msg_if_player( m_bad, _( "你需要手持泰坦弦弓才能奏出挥砍华舞。" ) );
        return;
    }

    const bool primary = sp.effect_data() == "TRAVEL_TITAN_FLOURISH_PRIMARY";
    const bool secondary = sp.effect_data() == "TRAVEL_TITAN_FLOURISH_SECONDARY";
    if( !primary && !secondary ) {
        return;
    }

    if( primary ) {
        weapon->erase_var( "TRAVEL_TITAN_FLOURISH_AMMO" );
    }
    const std::string preferred_ammo = secondary ?
                                       weapon->get_var( "TRAVEL_TITAN_FLOURISH_AMMO" ) : std::string();
    if( !load_titan_arrow( *shooter, weapon, preferred_ammo ) ) {
        shooter->add_msg_if_player( m_bad, _( "泰坦弦弓没有可用的箭矢。" ) );
        if( secondary ) {
            weapon->erase_var( "TRAVEL_TITAN_FLOURISH_AMMO" );
        }
        return;
    }

    if( primary && !weapon->ammo_current().is_null() ) {
        weapon->set_var( "TRAVEL_TITAN_FLOURISH_AMMO", weapon->ammo_current().str() );
    }

    // 华舞仍走标准 fire_gun / projectile_attack 流程。开火前临时清空武器散布，
    // 并让角色后坐与载具运动后坐互相抵消，使这一箭的总散布为零。
    // 箭矢、爆炸效果、命中事件、障碍物碰撞与回收逻辑仍全部由原版弹道处理。
    const double recoil_before_flourish = shooter->recoil;
    shooter->add_effect( effect_travel_titan_flourish_lock, 1_turns );
    shooter->recalculate_enchantment_cache();
    shooter->recoil = -shooter->recoil_vehicle();
    const int shots_fired = shooter->fire_gun( target, 1, *weapon );
    shooter->remove_effect( effect_travel_titan_flourish_lock );
    shooter->recalculate_enchantment_cache();
    if( shots_fired <= 0 ) {
        shooter->recoil = recoil_before_flourish;
    }

    if( secondary ) {
        weapon->erase_var( "TRAVEL_TITAN_FLOURISH_AMMO" );
    }
    if( shots_fired <= 0 || !primary ) {
        return;
    }

    // Breeze 当前弓术普通拉弓耐力为 ( 20 - 弓术 )^2。华舞从两倍该消耗开始，
    // 随法术等级线性下降，在满级时等于一次普通射击。
    const int archery_skill = shooter->get_skill_level( skill_archery );
    const int normal_stamina_cost = static_cast<int>( std::pow( 20 - archery_skill, 2 ) );
    const double level_ratio = sp.get_max_level() > 0 ?
                               std::clamp( static_cast<double>( sp.get_level() ) / sp.get_max_level(), 0.0, 1.0 ) : 1.0;
    const double flourish_multiplier = 2.0 - level_ratio;
    shooter->mod_stamina( -static_cast<int>( std::round( normal_stamina_cost * flourish_multiplier ) ) );
}
