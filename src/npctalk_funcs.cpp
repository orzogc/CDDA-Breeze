#include "npctalk.h" // IWYU pragma: associated

#include <algorithm>
#include <cstddef>
#include <iosfwd>
#include <list>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "activity_type.h"
#include "auto_pickup.h"
#include "avatar.h"
#include "basecamp.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_utility.h"
#include "character.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue_chatbin.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "faction.h"
#include "game.h"
#include "game_constants.h"
#include "game_inventory.h"
#include "item.h"
#include "item_location.h"
#include "line.h"
#include "magic.h"
#include "map.h"
#include "memory_fast.h"
#include "messages.h"
#include "mission.h"
#include "monster.h"
#include "morale_types.h"
#include "mutation.h"
#include "npc.h"
#include "npctrade.h"
#include <optional>
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player_activity.h"
#include "point.h"
#include "rng.h"
#include "string_input_popup.h"
#include "text_snippets.h"
#include "translations.h"
#include "ui.h"
#include "viewer.h"
#include "mtype.h"

static const activity_id ACT_FIND_MOUNT( "ACT_FIND_MOUNT" );
static const activity_id ACT_MOVE_LOOT( "ACT_MOVE_LOOT" );
static const activity_id ACT_MULTIPLE_BUTCHER( "ACT_MULTIPLE_BUTCHER" );
static const activity_id ACT_MULTIPLE_CHOP_PLANKS( "ACT_MULTIPLE_CHOP_PLANKS" );
static const activity_id ACT_MULTIPLE_CHOP_TREES( "ACT_MULTIPLE_CHOP_TREES" );
static const activity_id ACT_MULTIPLE_CONSTRUCTION( "ACT_MULTIPLE_CONSTRUCTION" );
static const activity_id ACT_MULTIPLE_DIS( "ACT_MULTIPLE_DIS" );
static const activity_id ACT_MULTIPLE_FARM( "ACT_MULTIPLE_FARM" );
static const activity_id ACT_MULTIPLE_FISH( "ACT_MULTIPLE_FISH" );
static const activity_id ACT_MULTIPLE_MINE( "ACT_MULTIPLE_MINE" );
static const activity_id ACT_MULTIPLE_MOP( "ACT_MULTIPLE_MOP" );
static const activity_id ACT_SOCIALIZE( "ACT_SOCIALIZE" );
static const activity_id ACT_TRAIN( "ACT_TRAIN" );
static const activity_id ACT_TRAIN_TEACHER( "ACT_TRAIN_TEACHER" );
static const activity_id ACT_VEHICLE_DECONSTRUCTION( "ACT_VEHICLE_DECONSTRUCTION" );
static const activity_id ACT_VEHICLE_REPAIR( "ACT_VEHICLE_REPAIR" );
static const activity_id ACT_WAIT_NPC( "ACT_WAIT_NPC" );

static const efftype_id effect_allow_sleep( "allow_sleep" );
static const efftype_id effect_asked_for_item( "asked_for_item" );
static const efftype_id effect_asked_personal_info( "asked_personal_info" );
static const efftype_id effect_asked_to_follow( "asked_to_follow" );
static const efftype_id effect_asked_to_lead( "asked_to_lead" );
static const efftype_id effect_asked_to_train( "asked_to_train" );
static const efftype_id effect_bite( "bite" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_currently_busy( "currently_busy" );
static const efftype_id effect_infected( "infected" );
static const efftype_id effect_lying_down( "lying_down" );
static const efftype_id effect_npc_suspend( "npc_suspend" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_sleep( "sleep" );

static const faction_id faction_no_faction( "no_faction" );
static const faction_id faction_your_followers( "your_followers" );

static const mission_type_id mission_MISSION_REACH_SAFETY( "MISSION_REACH_SAFETY" );

static const mtype_id mon_chicken( "mon_chicken" );
static const mtype_id mon_cow( "mon_cow" );
static const mtype_id mon_horse( "mon_horse" );

struct itype;

static void spawn_animal( npc &p, const mtype_id &mon );

void talk_function::nothing( npc & )
{
}

void talk_function::assign_mission( npc &p )
{
    mission *miss = p.chatbin.mission_selected;
    if( miss == nullptr ) {
        debugmsg( "assign_mission: mission_selected == nullptr" );
        return;
    } else if( miss->is_assigned() ) {
        DebugLog( D_WARNING, D_MAIN ) << "assign_mission: mission_id: " << miss->mission_id().str() <<
                                      " is already assigned!";
        return;
    }
    miss->assign( get_avatar() );
    p.chatbin.missions_assigned.push_back( miss );
    const auto it = std::find( p.chatbin.missions.begin(), p.chatbin.missions.end(), miss );
    p.chatbin.missions.erase( it );
}

void talk_function::mission_success( npc &p )
{
    mission *miss = p.chatbin.mission_selected;
    if( miss == nullptr ) {
        debugmsg( "mission_success: mission_selected == nullptr" );
        return;
    }

    int miss_val = npc_trading::cash_to_favor( p, miss->get_value() );
    npc_opinion op;
    op.value = 1 + miss_val / 5;
    op.anger = -1;
    p.op_of_u += op;
    faction *p_fac = p.get_faction();
    if( p_fac != nullptr ) {
        int fac_val = std::min( 1 + miss_val / 10, 10 );
        p_fac->likes_u += fac_val;
        p_fac->respects_u += fac_val;
        p_fac->trusts_u += fac_val;
        p_fac->power += fac_val;
    }
    miss->wrap_up();
}

void talk_function::mission_failure( npc &p )
{
    mission *miss = p.chatbin.mission_selected;
    if( miss == nullptr ) {
        debugmsg( "mission_failure: mission_selected == nullptr" );
        return;
    }
    npc_opinion op;
    op.trust = -1;
    op.value = -1;
    op.anger = 1;
    p.op_of_u += op;
    miss->fail();
}

void talk_function::clear_mission( npc &p )
{
    mission *miss = p.chatbin.mission_selected;
    if( miss == nullptr ) {
        debugmsg( "clear_mission: mission_selected == nullptr" );
        return;
    }
    const auto it = std::find( p.chatbin.missions_assigned.begin(), p.chatbin.missions_assigned.end(),
                               miss );
    if( it == p.chatbin.missions_assigned.end() ) {
        debugmsg( "clear_mission: mission_selected not in assigned" );
        return;
    }
    p.chatbin.missions_assigned.erase( it );
    if( p.chatbin.missions_assigned.empty() ) {
        p.chatbin.mission_selected = nullptr;
    } else {
        p.chatbin.mission_selected = p.chatbin.missions_assigned.front();
    }
    if( miss->has_follow_up() ) {
        p.add_new_mission( mission::reserve_new( miss->get_follow_up(), p.getID() ) );
        if( !p.chatbin.mission_selected ) {
            p.chatbin.mission_selected = p.chatbin.missions.front();
        }
    }
}

void talk_function::mission_reward( npc &p )
{
    const mission *miss = p.chatbin.mission_selected;
    if( miss == nullptr ) {
        debugmsg( "Called mission_reward with null mission" );
        return;
    }

    int mission_value = miss->get_value();
    p.op_of_u.owed += mission_value;
    npc_trading::trade( p, 0, _( "Reward" ) );
}

void talk_function::buy_chicken( npc &p )
{
    spawn_animal( p, mon_chicken );
}
void talk_function::buy_horse( npc &p )
{
    spawn_animal( p, mon_horse );
}

void talk_function::buy_cow( npc &p )
{
    spawn_animal( p, mon_cow );
}

void spawn_animal( npc &p, const mtype_id &mon )
{
    if( monster *const mon_ptr = g->place_critter_around( mon, p.pos(), 1 ) ) {
        mon_ptr->friendly = -1;
        mon_ptr->add_effect( effect_pet, 1_turns, true );
    } else {
        // TODO: handle this gracefully (return the money, proper in-character message from npc)
        add_msg_debug( debugmode::DF_NPC, "No space to spawn purchased pet" );
    }
}

void talk_function::start_trade( npc &p )
{
    npc_trading::trade( p, 0, _( "Trade" ) );
}

void talk_function::exert_coercion(npc &p) {


    if (p.get_faction()->id.str() != "free_merchants") {


        
        return;
    
    
    
    }

    // 如果我们已经将其派系威压，不能再进行这个流程
    if (p.get_faction()->conquer_degree>0) {

          add_msg(m_good, _("你之前已经将他们的派系威压成功了。"));

         return;

    }


    avatar& player_avatar = get_avatar();
    int turn = 1;
    int player_psychological_stress_value = 0;
    int target_psychological_stress_value = 0;
    int target_force = 0;
    int player_force = player_avatar.get_str()*2;

    int lose_like_u = 0;
    add_msg(m_good, _(" 1 player_force %s"), player_force);
    for (npc &n_ref : g->all_npcs()) {

        /*if (n_ref.get_fac_id() == p.get_fac_id()) {
            
            target_force = target_force + n_ref.get_str();
        
        }*/
        if (n_ref.is_ally(get_player_character())) {

            player_force = player_force + n_ref.get_str() * 2;
        
        
        }
        // 就算目标武力的时候，应该附带上这些派系，他们都依附于难民中心
        else if(n_ref.get_faction()->id.str() == "free_merchants"  || n_ref.get_faction()->id.str() == "lobby_beggars" || n_ref.get_faction()->id.str() == "no_faction" || n_ref.get_faction()->id.str() == "old_guard") {

            target_force = target_force + n_ref.get_str() * 2 ;
                        
        }

        add_msg(m_good,_("派系: %s"),n_ref.get_faction()->id.str());
        
    
    
    }
    add_msg(m_good, _(" 2 player_force %s"), player_force);

    // 普通施压
    bool normal_attack = false;
    // 展示带来的宠物
    bool show_pet = false;
    // 以武力进行极限施压
    bool force_attack = false;

    bool done_show_pet = false;

    bool done_force_attack = false;

    bool turn_end = false;
    bool should_clear = false;

    bool will_break = false;


    // 玩家和对方的智力差异，如果为正，代表优势，如果为负代表劣势
    int int_diffence = get_player_character().get_int() - p.get_int();

    // 玩家的智力作为基础的心理攻击力
    int base_attack_power = get_player_character().get_int();
    // 对方的智力作为基础的心理攻击力
    int target_base_attack_power = p.get_int();


    // 统计一回合能给对方造成的心理压力值
    int damage = 0;
    // 统计一回合能给玩家造成的心理压力值
    int damage_to_player = 0;


    std::vector<const monster*> player_pet_vec;

    // 统计玩家的宠物
    for (const monster & m: g->all_monsters()) {

        if (m.has_effect(effect_pet)) {
            player_force = player_force + m.get_hp_max() * 0.1 * (m.lv > 0 ? m.lv : 1) * m.type->melee_skill ;
            player_pet_vec.push_back(&m);
            
        
        }

    
    }
    

    float player_fix = player_force / target_force;
    float target_fix;
    if (player_force ==0) {

        target_fix = 1.0;

    }
    else {
    
    
        target_fix = target_force / player_force;

    }
    


    // 显示玩家当前的状况
    catacurses::window w_01;
    // 显示博弈的过程
    catacurses::window w_02;
    // 显示对方当前的状况
    catacurses::window w_03;
    input_context ctxt("");
    ctxt.register_action("QUIT");
    ctxt.register_action("UP");
    ctxt.register_action("LEFT");
    ctxt.register_action("RIGHT");
    ctxt.register_action("DOWN");

    ui_adaptor ui_01;
    ui_01.on_screen_resize([&](ui_adaptor&) {
        const std::pair<point, point> beg_and_max = {
        point(TERMX / 5, TERMY / 8),
        point(TERMX / 2, TERMY / 2)
        };
        const point& beg = beg_and_max.first;
        w_01 = catacurses::newwin(40, 40, beg + point(0, 0));
        w_02 = catacurses::newwin(40, 40, beg + point(40, 0));
        w_03 = catacurses::newwin(40, 40, beg + point(80, 0));
        ui_01.position_from_window(w_01);
        });




    ui_01.mark_resize();


    


    ui_01.on_redraw([&](const ui_adaptor&) {



        damage = damage + base_attack_power / 4;
        damage_to_player = damage_to_player + target_base_attack_power / 4;





        werase(w_01);
        werase(w_02);
        werase(w_03);
        draw_border(w_01);
        draw_border(w_02);
        draw_border(w_03);

        


        if (player_pet_vec.size()>0 && done_show_pet == false) {

            fold_and_print(w_01, point(1, 22), getmaxx(w_01) - 1 - 1, c_white,
                "按 LEFT 展示自己带来的宠物");
            
            if (done_force_attack == false) {

                fold_and_print(w_01, point(1, 25), getmaxx(w_01) - 1 - 1, c_white,
                    "按 RIGHT 以武力进行极限威压 (高风险)");
            
            
            }
            
            
        
        
        }
        else {
        
            if (done_force_attack == false) {
                fold_and_print(w_01, point(1, 22), getmaxx(w_01) - 1 - 1, c_white,
                    "按 RIGHT 以武力进行极限威压 (高风险)");
            }

        }

        



        trim_and_print(w_02, point(1, 1), 25, c_white, "              第 %s 回合 ", turn);


        if (normal_attack == true ) {

            
            if (int_diffence > 0) {


                int chance = rng(1, 3);

                if (chance ==1 ) {

                    fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                        "商人: 话说得高明，值得称赞......老实说，我还真有点动摇...... \n 之后商人巧妙的回应了你的威压。");
                
                }

                if (chance == 2) {

                    fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                        "你能看出商人有一点动摇。 \n 但是之后商人仍然巧妙的回应了你的威压。");
                
                }

                if (chance == 3) {

                    fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                        "商人的表情微微有了一点变化， \n 之后商人仍然巧妙的回应了你的威压。");

                }

                

                
                damage = ( damage + rng_float(0.8,1.0)* 5 )* player_fix ;
                damage_to_player = (damage_to_player + 5 * rng_float(0.5, 0.8) )*  target_fix;
            
            
            
            }
            else {

                int chance = rng(1, 3);

                if (chance == 1) {

                    fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                        "商人只是略作无奈的耸耸肩。 \n 之后商人用密不透风地话术回应了你的威压。");
                
                }

                if (chance == 2) {

                    fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                        "商人轻笑了一声，摇了摇头。 \n 之后商人用密不透风地话术回应了你的威压。");
                
                }

                if (chance == 3) {

                    fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                        "商人先是平平淡淡的应和了几声。 \n 之后商人用密不透风地话术回应了你的威压。");


                }

               
            
                damage = (damage + rng_float(0.1, 0.5) * 5) * player_fix;
                damage_to_player = (damage_to_player + 5 * rng_float(0.8, 1.0)) * target_fix;
            
            
            }    




            // 最终校正

            if (damage >= 10) {
                damage = rng(10, damage);
            }
            else if (damage <= 0) {
                damage = rng(1, 3);
            }

            if (damage_to_player >= 10) {
                damage_to_player = rng(10, damage_to_player);
            }
            else if (damage_to_player <= 0) {
                damage_to_player = rng(1, 3);
            }



            lose_like_u = lose_like_u + 1;

        
                
        }


         else if (show_pet == true  && done_show_pet == false && player_pet_vec.size()>0) {



            int pets_power = 0;

            for (const monster *m : player_pet_vec) {

                pets_power = pets_power + m->get_hp_max() * 0.1 * (m->lv > 0 ? m->lv : 1) * m->type->melee_skill;
            
            }


            if (pets_power > 0.5 * target_force) {
                fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                    "商人: “我很想掩盖住自己的惊讶，可以的话，我真的很想知道你是如何拥有%s强大的宠物的。甚至我想说，用宠物去护卫我们的商路，能让我们避免很多损失......”\n 商人意识到了自己的惊讶后，迅速冷静了下来，像是思索着什么。", player_pet_vec.size()>0 ? "这些" : "这个");

                damage = damage + rng_float(0.5, 1.0) * 10;
                damage_to_player = damage_to_player - 5 * rng_float(0.5, 1.0);

            }
            else {

                int chance = rng(1, 3);

                if (chance == 1) {
                    fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                        "商人充满平静，说到，“就这样？”");
                }
                if (chance == 2) {
                    fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                        "商人轻笑了一声，摇了摇头。");
                }
                if (chance == 3) {
                    fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                        "商人仅仅是平平淡淡的应和了几声。");
                }

                damage = damage - rng_float(0.5, 1.0) ;


            }

            done_show_pet = true;



            // 最终校正

            if (damage >= 10) {
                damage = rng(10, damage);
            }
            else if (damage <= 0) {
                damage = rng(1, 3);
            }

            if (damage_to_player >= 10) {
                damage_to_player = rng(10, damage_to_player);
            }
            else if (damage_to_player <= 0) {
                damage_to_player = rng(1, 3);
            }


        }

         else if ( force_attack == true && done_force_attack == false ) {


            if (target_force - player_force >0) {

                // 大于100 是极大的差距
                if (target_force - player_force > 100) {
                
                    int chance = rng(1, 10);

                    //
                    if (chance != 1 ) {


                        will_break = true;
                        p.get_faction()->likes_u = p.get_faction()->likes_u - 500;
                        fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                            "商人先是一直盯着你，过了许久，然后对你说：“你已经做了不可挽回的事。”");
                        add_msg(m_bad, _("你已经触怒了他们整个派系！"));
                        add_msg(m_bad,_("你已经触怒了他们整个派系！"));
                        
                        
                    
                                        
                    }
                    else {

                        int chance_ = rng(1,3);

                        if (chance_ == 1) {

                            fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                                "商人先是一直盯着你，过了许久，然后对你说：“你已经做了不可挽回的事。”接着又是一阵沉默......");

                        }
                        else if (chance_ == 2) {

                            fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                                "空气好像突然冷凝了一阵，过会，商人和你言辞激烈地交锋起来......");

                        }
                        else {

                            fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                                "商人此刻稍微低下了头，你看不到他的神情......");


                        }
                    
                    
                    
                    }                              
                
                }


                else {



                    int chance = rng(1, 10);

                    if (chance == 1) {


                        will_break = true;
                        p.get_faction()->likes_u = p.get_faction()->likes_u - 500;
                        fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                            "商人先是一直盯着你，过了许久，然后对你说：“你已经做了不可挽回的事。”");
                        add_msg(m_bad, _("你已经触怒了他们整个派系！"));
                        


                    }
                    else {
                    
                    
                        
                        int chance_ = rng(1, 3);

                        if (chance_ == 1) {

                            fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                                "商人先是一直盯着你，过了许久，然后对你说：“你已经做了不可挽回的事。”接着又是一阵沉默......");

                        }
                        else if (chance_ == 2) {

                            fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                                "空气好像突然冷凝了一阵，过会，商人和你言辞激烈地交锋起来......");

                        }
                        else {

                            fold_and_print(w_02, point(1, 3), getmaxx(w_02) - 1 - 1, c_white,
                                "商人此刻稍微低下了头，你看不到他的神情......");


                        }
                                                 
                    }

                    



                    // 对双方的心理伤害都很大
                    damage = damage + rng_float(0.5, 1.0) * 10 + player_force > 100 ? rng(0.01 * player_force, 0.03 * player_force) : rng(0.04 * player_force, 0.07 * player_force);
                    damage_to_player = damage_to_player + 5 * rng_float(0.5, 1.0);



                }
            }


            done_force_attack = true;

            
            lose_like_u = lose_like_u + 1;




            // 最终校正

            if (damage >= 10) {
                damage = rng(10, damage);
            }
            else if (damage <= 0) {
                damage = rng(1, 3);
            }

            if (damage_to_player >= 10) {
                damage_to_player = rng(10, damage_to_player);
            }
            else if (damage_to_player <= 0) {
                damage_to_player = rng(1, 3);
            }

                  
         }


        if (turn_end == true ) {


            if (should_clear == false) {

                target_psychological_stress_value = target_psychological_stress_value + damage;
                player_psychological_stress_value = player_psychological_stress_value + damage_to_player;

            }


            fold_and_print(w_02, point(1, 16), getmaxx(w_02) - 1 - 1, c_white,
                "--------------------------------------");
            fold_and_print(w_02, point(1, 19), getmaxx(w_02) - 1 - 1, c_white,
                "本回合交锋结果: ");
            fold_and_print(w_02, point(1, 22), getmaxx(w_02) - 1 - 1, c_green,
                "你造成的心理压力为 : %s", damage);
            fold_and_print(w_02, point(1, 25), getmaxx(w_02) - 1 - 1, c_red,
                "对方造成的心理压力为 : %s", damage_to_player);
            
            should_clear = true;
            
            fold_and_print(w_02, point(1, 28), getmaxx(w_02) - 1 - 1, c_white,
                "请按 UP/LEFT/RIGHT 结束本回合");
        
            
            
        
        
        
        
        }
        trim_and_print(w_01, point(1, 1), 25, c_white, "我");
        trim_and_print(w_01, point(1, 4), 25, c_white, "心理压力值: %s", player_psychological_stress_value);
        trim_and_print(w_01, point(1, 7), 25, c_white, "当前能展示的武力: %s", player_force * 2);

        fold_and_print(w_01, point(1, 16), getmaxx(w_01) - 1 - 1, c_white,
            "--------------------------------------");
        fold_and_print(w_01, point(1, 19), getmaxx(w_01) - 1 - 1, c_white,
            "按 UP 进行普通威压");
        

          
        trim_and_print(w_03, point(1, 1), 25, c_white, "商人");
        trim_and_print(w_03, point(1, 4), 25, c_white, "心理压力值: %s", target_psychological_stress_value);
        trim_and_print(w_03, point(1, 7), 25, c_white, "对你自身的基础恐惧: %s", p.op_of_u.fear);
        trim_and_print(w_03, point(1, 10), 25, c_white, "当前能展示的武力: %s", target_force*2);
        fold_and_print(w_03, point(1, 16), getmaxx(w_03) - 1 - 1, c_white,
            "--------------------------------------");
        fold_and_print(w_03, point(1, 19), getmaxx(w_03) - 1 - 1, c_white,
            "特性:      冷静      精明");
        
        wnoutrefresh(w_03);
        wnoutrefresh(w_02);
        wnoutrefresh(w_01);

        

        


        damage = 0;
        damage_to_player = 0;

        // 重置标志状态
        normal_attack = false;
        show_pet = false;
        force_attack = false;
        turn_end = false;   
        });








    while (true) {


        ui_01.invalidate_ui();
        ui_manager::redraw_invalidated();
        const std::string action = ctxt.handle_input();
        
        if (will_break==true) {

            break;
        
        }


        if (should_clear == true) {

            should_clear = false;
            turn++;
            continue;
            
        
        } 


        if (turn>10 || player_psychological_stress_value >=100 || target_psychological_stress_value>=100) {

        
            if (target_psychological_stress_value >=100) {

                // 威压成功
                // 征服度上升到100
                p.get_faction()->conquer_degree = 100;

                // 距离下一次拿取物资的天数变为 30
                p.get_faction()->days_required_to_submit_resources = 30;


            }
            else {
            
            
                // 处理积攒的 lose_like_u 的数值，当然如果成功威压，如上面，不用考虑应用上这部分数值
                p.get_faction()->likes_u = p.get_faction()->likes_u - lose_like_u;
            
            
            }
            
            
            
            break;
        
        
        }
        
              
        if (action == "QUIT") {
            break;
        }

        if (action == "UP") {
            
                  
                normal_attack = true;
                turn_end = true;
            
            
                       
        }

        if (action == "LEFT" ) {

            if (player_pet_vec.size() == 0) {

                continue;


            
            }

                show_pet = true;
                turn_end = true;

        }

        if (action == "RIGHT") {

                
                force_attack = true;
                turn_end = true;

                    
        }



        
        

    }





}


void talk_function::join_the_faction(npc& p) {


    faction* f = g->faction_manager_ptr->get(p.fac_id);
    f->player_has_joined = true;


}


void talk_function::set_status_get_food_from_the_faction(npc& p) {

    faction* f = g->faction_manager_ptr->get(p.fac_id);
    f->if_player_get_food_today_JOINED = true;

}

void talk_function::report_work_to_this_faction(npc& p) {

    faction* f = g->faction_manager_ptr->get(p.fac_id);
    f->if_player_reported_work_today = true;

}


void talk_function::sort_loot( npc &p )
{
    p.assign_activity( ACT_MOVE_LOOT );
}

void talk_function::do_construction( npc &p )
{
    p.assign_activity( ACT_MULTIPLE_CONSTRUCTION );
}

void talk_function::do_mining( npc &p )
{
    p.assign_activity( ACT_MULTIPLE_MINE );
}

void talk_function::do_mopping( npc &p )
{
    p.assign_activity( ACT_MULTIPLE_MOP );
}

void talk_function::do_read( npc &p )
{
    p.do_npc_read();
}

void talk_function::dismount( npc &p )
{
    p.npc_dismount();
}

void talk_function::find_mount( npc &p )
{
    // first find one nearby
    for( monster &critter : g->all_monsters() ) {
        if( p.can_mount( critter ) ) {
            // keep the horse still for some time, so that NPC can catch up to it and mount it.
            p.assign_activity( ACT_FIND_MOUNT );
            p.chosen_mount = g->shared_from( critter );
            // we found one, that's all we need.
            return;
        }
    }
    // if we got here and this was prompted by a renewal of the activity, and there are no valid monsters nearby, then cancel whole thing.
    if( p.has_player_activity() ) {
        p.revert_after_activity();
    }
}

void talk_function::do_butcher( npc &p )
{
    p.assign_activity( ACT_MULTIPLE_BUTCHER );
}

void talk_function::do_chop_plank( npc &p )
{
    p.assign_activity( ACT_MULTIPLE_CHOP_PLANKS );
}

void talk_function::do_vehicle_deconstruct( npc &p )
{
    p.assign_activity( ACT_VEHICLE_DECONSTRUCTION );
}

void talk_function::do_vehicle_repair( npc &p )
{
    p.assign_activity( ACT_VEHICLE_REPAIR );
}

void talk_function::do_chop_trees( npc &p )
{
    p.assign_activity( ACT_MULTIPLE_CHOP_TREES );
}

void talk_function::do_farming( npc &p )
{
    p.assign_activity( ACT_MULTIPLE_FARM );
}

void talk_function::do_fishing( npc &p )
{
    p.assign_activity( ACT_MULTIPLE_FISH );
}

void talk_function::revert_activity( npc &p )
{
    p.revert_after_activity();
}

void talk_function::do_craft(npc& p)
{
    p.do_npc_craft();
}
void talk_function::do_disassembly( npc &p )
{
    p.assign_activity( ACT_MULTIPLE_DIS );
}

void talk_function::goto_location( npc &p )
{
    int i = 0;
    uilist selection_menu;
    selection_menu.text = _( "Select a destination" );
    std::vector<basecamp *> camps;
    tripoint_abs_omt destination;
    Character &player_character = get_player_character();
    for( auto elem : player_character.camps ) {
        if( elem == p.global_omt_location() ) {
            continue;
        }
        std::optional<basecamp *> camp = overmap_buffer.find_camp( elem.xy() );
        if( !camp ) {
            continue;
        }
        basecamp *temp_camp = *camp;
        camps.push_back( temp_camp );
    }
    for( const basecamp *iter : camps ) {
        //~ %1$s: camp name, %2$d and %3$d: coordinates
        selection_menu.addentry( i++, true, MENU_AUTOASSIGN, pgettext( "camp", "%1$s at %2$s" ),
                                 iter->camp_name(), iter->camp_omt_pos().to_string() );
    }
    selection_menu.addentry( i++, true, MENU_AUTOASSIGN, _( "My current location" ) );
    if (!player_character.omt_path.empty()) {
        selection_menu.addentry(i++, true, MENU_AUTOASSIGN, "我指定的位置");
    }
    selection_menu.addentry( i, true, MENU_AUTOASSIGN, _( "Cancel" ) );
    selection_menu.selected = 0;
    selection_menu.query();
    int index = selection_menu.ret;
    if (index < 0 || index >= i) {
        return;
    }
    if( index == static_cast<int>( camps.size() ) ) {
        destination = player_character.global_omt_location();
    }
    else if (index == static_cast<int>(camps.size()) + 1) {
        // This looks nuts, but omt_path is emplaced in reverse order. So the front of the vector is our destination
        destination = player_character.omt_path.front();
    } else {
        const basecamp *selected_camp = camps[index];
        destination = selected_camp->camp_omt_pos();
    }
    p.goal = destination;
    p.omt_path = overmap_buffer.get_travel_path( p.global_omt_location(), p.goal,
                 overmap_path_params::for_npc() );
    if( destination == tripoint_abs_omt() || destination == overmap::invalid_tripoint ||
        p.omt_path.empty() ) {
        p.goal = npc::no_goal_point;
        p.omt_path.clear();
        add_msg( m_info, _( "That is not a valid destination for %s." ), p.disp_name() );
        return;
    }
    p.set_mission( NPC_MISSION_TRAVELLING );
    p.chatbin.first_topic = p.chatbin.talk_friend_guard;
    p.guard_pos = std::nullopt;
    p.set_attitude( NPCATT_NULL );
}

void talk_function::assign_guard( npc &p )
{
    if( !p.is_player_ally() ) {
        p.set_mission( NPC_MISSION_GUARD );
        p.set_omt_destination();
        return;
    }

    if( p.has_player_activity() ) {
        p.revert_after_activity();
    }
    p.set_attitude( NPCATT_NULL );
    p.set_mission( NPC_MISSION_GUARD_ALLY );
    p.chatbin.first_topic = p.chatbin.talk_friend_guard;
    p.set_omt_destination();
}

void talk_function::abandon_camp( npc &p )
{
    std::optional<basecamp *> bcp = overmap_buffer.find_camp( p.global_omt_location().xy() );
    if( bcp ) {
        basecamp *temp_camp = *bcp;
        temp_camp->abandon_camp();
    }
}

void talk_function::assign_camp( npc &p )
{
    std::optional<basecamp *> bcp = overmap_buffer.find_camp( p.global_omt_location().xy() );
    if( bcp ) {
        basecamp *temp_camp = *bcp;
        p.set_attitude( NPCATT_NULL );
        p.set_mission( NPC_MISSION_GUARD_ALLY );
        temp_camp->add_assignee( p.getID() );
        temp_camp->job_assignment_ui();
        temp_camp->validate_assignees();
        add_msg( _( "%1$s is assigned to %2$s" ), p.disp_name(), temp_camp->camp_name() );
        if( p.has_player_activity() ) {
            p.revert_after_activity();
        }
        p.chatbin.first_topic = p.chatbin.talk_friend_guard;
        p.set_omt_destination();
    }
}

void talk_function::stop_guard( npc &p )
{
    if( !p.is_player_ally() ) {
        p.set_attitude( NPCATT_NULL );
        p.set_mission( NPC_MISSION_NULL );
        return;
    }
    p.set_attitude( NPCATT_FOLLOW );
    add_msg( _( "%s begins to follow you." ), p.get_name() );
    p.set_mission( NPC_MISSION_NULL );
    if( p.has_companion_mission() ) {
        p.reset_companion_mission();
    }
    p.chatbin.first_topic = p.chatbin.talk_friend;
    p.goal = npc::no_goal_point;
    p.guard_pos = std::nullopt;
    if( p.assigned_camp ) {
        if( std::optional<basecamp *> bcp = overmap_buffer.find_camp( ( *p.assigned_camp ).xy() ) ) {
            ( *bcp )->remove_assignee( p.getID() );
            ( *bcp )->validate_assignees();
        }
        p.assigned_camp = std::nullopt;
    }
}

void talk_function::wake_up( npc &p )
{
    p.rules.clear_override( ally_rule::allow_sleep );
    p.rules.enable_override( ally_rule::allow_sleep );
    p.remove_effect( effect_allow_sleep );
    p.remove_effect( effect_lying_down );
    p.remove_effect( effect_npc_suspend );
    p.remove_effect( effect_sleep );
    // TODO: Get mad at player for waking us up unless we're in danger
}

void talk_function::reveal_stats( npc &p )
{
    p.disp_info();
}

void talk_function::end_conversation( npc &p )
{
    add_msg( _( "%s starts ignoring you." ), p.get_name() );
    p.chatbin.first_topic = "TALK_DONE";
}

void talk_function::insult_combat( npc &p )
{
    add_msg( _( "You start a fight with %s!" ), p.get_name() );
    p.chatbin.first_topic = "TALK_DONE";
    p.set_attitude( NPCATT_KILL );
}

void talk_function::bionic_install( npc &p )
{
    avatar &player_character = get_avatar();
    item_location bionic = game_menus::inv::install_bionic( p, player_character, true );

    if( !bionic ) {
        return;
    }

    item *tmp = bionic.get_item();
    tmp->set_var( VAR_TRADE_IGNORE, 1 );
    const itype &it = *tmp->type;

    signed int price = npc_trading::bionic_install_price( p, player_character, bionic );
    bool const ret = npc_trading::pay_npc( p, price );
    tmp->erase_var( VAR_TRADE_IGNORE );
    if( !ret ) {
        return;
    }

    //Makes the doctor awesome at installing but not perfect
    if( player_character.can_install_bionics( it, p, false, 20 ) ) {
        bionic.remove_item();
        player_character.install_bionics( it, p, false, 20 );
    }
}

void talk_function::bionic_remove( npc &p )
{
    Character &player_character = get_player_character();
    const bionic_collection all_bio = *player_character.my_bionics;
    if( all_bio.empty() ) {
        popup( _( "You don't have any bionics installed…" ) );
        return;
    }

    std::vector<itype_id> bionic_types;
    std::vector<std::string> bionic_names;
    std::vector<const bionic *> bionics;
    for( const bionic &bio : all_bio ) {
        if( std::find( bionic_types.begin(), bionic_types.end(),
                       bio.info().itype() ) == bionic_types.end() ) {
            bionic_types.push_back( bio.info().itype() );
            if( item::type_is_defined( bio.info().itype() ) ) {
                item tmp = item( bio.id.str(), calendar::turn_zero );
                bionic_names.push_back( tmp.tname() + " - " + format_money( 5000 + ( tmp.price( true ) / 4 ) ) );
            } else {
                bionic_names.push_back( bio.id.str() + " - " + format_money( 5000 ) );
            }
            bionics.push_back( &bio );
        }
    }
    // Choose bionic if applicable
    int bionic_index = uilist( _( "Which bionic do you wish to uninstall?" ),
                               bionic_names );
    // Did we cancel?
    if( bionic_index < 0 ) {
        popup( _( "You decide to hold off…" ) );
        return;
    }

    signed int price;
    if( item::type_is_defined( bionic_types[bionic_index] ) ) {
        price = 5000 + ( item( bionic_types[bionic_index], calendar::turn_zero ).price( true ) / 4 );
    } else {
        price = 5000;
    }
    if( !npc_trading::pay_npc( p, price ) ) {
        return;
    }

    //Makes the doctor awesome at uninstalling but not perfect
    if( player_character.can_uninstall_bionic( *bionics[bionic_index], p,
            false, 20 ) ) {
        player_character.uninstall_bionic( *bionics[bionic_index], p, false, 20 );
    }
}

void talk_function::give_equipment( npc &p )
{
    give_equipment_allowance( p, 0 );
}

void talk_function::give_equipment_allowance( npc &p, int allowance )
{
    std::vector<item_pricing> giving = npc_trading::init_selling( p );
    int chosen = -1;
    while( chosen == -1 && !giving.empty() ) {
        int index = rng( 0, giving.size() - 1 );
        if( giving[index].price < p.op_of_u.owed + allowance ) {
            chosen = index;
        } else {
            giving.erase( giving.begin() + index );
        }
    }
    if( giving.empty() ) {
        popup( _( "%s has nothing to give!" ), p.get_name() );
        return;
    }
    if( chosen < 0 || static_cast<size_t>( chosen ) >= giving.size() ) {
        debugmsg( "Chosen index is outside of available item range!" );
        chosen = 0;
    }
    item it = *giving[chosen].loc.get_item();
    giving[chosen].loc.remove_item();
    popup( _( "%1$s gives you a %2$s" ), p.get_name(), it.tname() );
    Character &player_character = get_player_character();
    it.set_owner( player_character );
    player_character.i_add( it );
    allowance -= giving[chosen].price;
    if( allowance < 0 ) {
        p.op_of_u.owed += allowance;
    }
    p.add_effect( effect_asked_for_item, 3_hours );
}

void talk_function::lesser_give_aid( npc &p )
{
    Character &player_character = get_player_character();
    for( const bodypart_id &bp :
         player_character.get_all_body_parts( get_body_part_flags::only_main ) ) {
        player_character.heal( bp, rng( 5, 15 ) );
        if( player_character.has_effect( effect_bleed, bp.id() ) ) {
            player_character.remove_effect( effect_bleed, bp );
        }
    }
    const int moves = to_moves<int>( 15_minutes );
    player_character.assign_activity( ACT_WAIT_NPC, moves );
    player_character.activity.str_values.push_back( p.get_name() );
    p.add_effect( effect_currently_busy, 60_minutes );
}

void talk_function::lesser_give_all_aid( npc &p )
{
    lesser_give_aid( p ); // Provide lesser aid to the player first

    Character &player_character = get_player_character();
    for( npc &guy : g->all_npcs() ) {
        if( guy.is_walking_with() && rl_dist( guy.pos(), player_character.pos() ) < PICKUP_RANGE ) {
            for( const bodypart_id &bp :
                 guy.get_all_body_parts( get_body_part_flags::only_main ) ) {
                guy.heal( bp, rng( 5, 15 ) );
                if( guy.has_effect( effect_bleed, bp.id() ) ) {
                    guy.remove_effect( effect_bleed, bp );
                }
            }
        }
    }

    const int moves = to_moves<int>( 30_minutes );
    player_character.assign_activity( ACT_WAIT_NPC, moves );
    player_character.activity.str_values.push_back( p.get_name() );
    p.add_effect( effect_currently_busy, 120_minutes );
}

void talk_function::give_aid( npc &p )
{
    Character &player_character = get_player_character();
    for( const bodypart_id &bp :
         player_character.get_all_body_parts( get_body_part_flags::only_main ) ) {
        player_character.heal( bp, 5 * rng( 2, 5 ) );
        if( player_character.has_effect( effect_bite, bp.id() ) ) {
            player_character.remove_effect( effect_bite, bp );
        }
        if( player_character.has_effect( effect_bleed, bp.id() ) ) {
            player_character.remove_effect( effect_bleed, bp );
        }
        if( player_character.has_effect( effect_infected, bp.id() ) ) {
            player_character.remove_effect( effect_infected, bp );
        }
    }

    const int moves = to_moves<int>( 30_minutes );
    player_character.assign_activity( ACT_WAIT_NPC, moves );
    player_character.activity.str_values.push_back( p.get_name() );
    p.add_effect( effect_currently_busy, 120_minutes );
}

void talk_function::give_all_aid( npc &p )
{
    give_aid( p ); // Provide aid to the player first

    Character &player_character = get_player_character();
    for( npc &guy : g->all_npcs() ) {
        if( guy.is_walking_with() && rl_dist( guy.pos(), player_character.pos() ) < PICKUP_RANGE ) {
            for( const bodypart_id &bp :
                 guy.get_all_body_parts( get_body_part_flags::only_main ) ) {
                guy.heal( bp, 5 * rng( 2, 5 ) );
                if( guy.has_effect( effect_bite, bp.id() ) ) {
                    guy.remove_effect( effect_bite, bp );
                }
                if( guy.has_effect( effect_bleed, bp.id() ) ) {
                    guy.remove_effect( effect_bleed, bp );
                }
                if( guy.has_effect( effect_infected, bp.id() ) ) {
                    guy.remove_effect( effect_infected, bp );
                }
            }
        }
    }

    const int moves = to_moves<int>( 60_minutes );
    player_character.assign_activity( ACT_WAIT_NPC, moves );
    player_character.activity.str_values.push_back( p.get_name() );
    p.add_effect( effect_currently_busy, 240_minutes );
}

static void generic_barber( const std::string &mut_type )
{
    uilist hair_menu;
    std::string menu_text;
    if( mut_type == "hair_style" ) {
        menu_text = _( "Choose a new hairstyle" );
    } else if( mut_type == "facial_hair" ) {
        menu_text = _( "Choose a new facial hair style" );
    }
    hair_menu.text = menu_text;
    int index = 0;
    hair_menu.addentry( index, true, 'q', _( "Actually…  I've changed my mind." ) );
    std::vector<trait_and_var> hair_muts = mutations_var_in_type( mut_type );
    Character &player_character = get_player_character();
    trait_and_var cur_hair;
    for( const trait_and_var &elem : hair_muts ) {
        if( player_character.has_trait_variant( elem ) ) {
            cur_hair = elem;
        }
        index += 1;
        hair_menu.addentry( index, true, MENU_AUTOASSIGN, elem.name() );
    }
    hair_menu.query();
    int choice = hair_menu.ret;
    if( choice != 0 ) {
        if( player_character.has_trait( cur_hair.trait ) ) {
            player_character.remove_mutation( cur_hair.trait, true );
        }
        const trait_and_var &chosen = hair_muts[choice - 1];
        player_character.set_mutation( chosen.trait, chosen.trait->variant( chosen.variant ) );
        add_msg( m_info, _( "You get a trendy new cut!" ) );
    }
}

void talk_function::barber_beard( npc &/*p*/ )
{
    generic_barber( "facial_hair" );
}

void talk_function::barber_hair( npc &/*p*/ )
{
    generic_barber( "hair_style" );
}

void talk_function::buy_haircut( npc &p )
{
    Character &player_character = get_player_character();
    player_character.add_morale( MORALE_HAIRCUT, 5, 5, 720_minutes, 3_minutes );
    const int moves = to_moves<int>( 20_minutes );
    player_character.assign_activity( ACT_WAIT_NPC, moves );
    player_character.activity.str_values.push_back( p.get_name() );
    add_msg( m_good, _( "%s gives you a decent haircut…" ), p.get_name() );
}

void talk_function::buy_shave( npc &p )
{
    Character &player_character = get_player_character();
    player_character.add_morale( MORALE_SHAVE, 10, 10, 360_minutes, 3_minutes );
    const int moves = to_moves<int>( 5_minutes );
    player_character.assign_activity( ACT_WAIT_NPC, moves );
    player_character.activity.str_values.push_back( p.get_name() );
    add_msg( m_good, _( "%s gives you a decent shave…" ), p.get_name() );
}

void talk_function::morale_chat( npc &p )
{
    get_player_character().add_morale( MORALE_CHAT, rng( 3, 10 ), 10, 200_minutes, 5_minutes / 2 );
    add_msg( m_good, _( "That was a pleasant conversation with %s…" ), p.disp_name() );
}

void talk_function::morale_chat_activity( npc &p )
{
    Character &player_character = get_player_character();
    const int moves = to_moves<int>( 10_minutes );
    player_character.assign_activity( ACT_SOCIALIZE, moves );
    player_character.activity.str_values.push_back( p.get_name() );
    if( one_in( 3 ) ) {
        p.say( SNIPPET.random_from_category( "npc_socialize" ).value_or( translation() ).translated() );
    }
    add_msg( m_good, _( "That was a pleasant conversation with %s." ), p.disp_name() );
    player_character.add_morale( MORALE_CHAT, rng( 3, 10 ), 10, 200_minutes, 5_minutes / 2 );
}

/*
 * Function to make the npc drop non favorite, worn or wielded items at their current position.
 */
void talk_function::drop_items_in_place( npc &p )
{
    std::vector<drop_or_stash_item_info> to_drop;

    // add all non favorite carried items to the drop off list
    for( const item_location &npcs_item : p.all_items_loc() ) {
        if( !npcs_item->is_favorite && npcs_item.where() == item_location::type::container &&
            npcs_item.parent_item().where() == item_location::type::character ) {
            to_drop.emplace_back( npcs_item, npcs_item->count() );
        }
    }
    if( !to_drop.empty() ) {
        // spawn a activity for the npc to drop the specified items
        p.assign_activity( player_activity( drop_activity_actor(
                                                to_drop, tripoint_zero, false
                                            ) ) );
        p.say( "<acknowledged>" );
    } else {
        p.say( _( "I don't have anything to drop off." ) );
    }
}

void talk_function::follow( npc &p )
{
    g->add_npc_follower( p.getID() );
    p.set_attitude( NPCATT_FOLLOW );
    p.set_fac( faction_your_followers );
    get_player_character().cash += p.cash;
    p.cash = 0;
}

void talk_function::follow_only( npc &p )
{
    p.set_attitude( NPCATT_FOLLOW );
}

void talk_function::deny_follow( npc &p )
{
    p.add_effect( effect_asked_to_follow, 6_hours );
}

void talk_function::deny_lead( npc &p )
{
    p.add_effect( effect_asked_to_lead, 6_hours );
}

void talk_function::deny_equipment( npc &p )
{
    p.add_effect( effect_asked_for_item, 1_hours );
}

void talk_function::deny_train( npc &p )
{
    p.add_effect( effect_asked_to_train, 6_hours );
}

void talk_function::deny_personal_info( npc &p )
{
    p.add_effect( effect_asked_personal_info, 3_hours );
}

void talk_function::hostile( npc &p )
{
    if( p.get_attitude() == NPCATT_KILL ) {
        return;
    }

    if( p.sees( get_player_character() ) ) {
        add_msg( _( "%s turns hostile!" ), p.get_name() );
    }

    get_event_bus().send<event_type::npc_becomes_hostile>( p.getID(), p.name );
    p.set_attitude( NPCATT_KILL );
}

void talk_function::flee( npc &p )
{
    add_msg( _( "%s turns to flee!" ), p.get_name() );
    p.set_attitude( NPCATT_FLEE );
}

void talk_function::leave( npc &p )
{
    add_msg( _( "%s leaves." ), p.get_name() );
    g->remove_npc_follower( p.getID() );
    std::string new_fac_id = "solo_";
    new_fac_id += p.name;
    p.job.clear_all_priorities();
    // create a new "lone wolf" faction for this one NPC
    faction *new_solo_fac = g->faction_manager_ptr->add_new_faction( p.name,
                            faction_id( new_fac_id ), faction_no_faction );
    p.set_fac( new_solo_fac ? new_solo_fac->id : faction_no_faction );
    if( new_solo_fac ) {
        new_solo_fac->known_by_u = true;
    }
    p.chatbin.first_topic = p.chatbin.talk_stranger_neutral;
    p.set_attitude( NPCATT_NULL );
    p.mission = NPC_MISSION_NULL;
    p.long_term_goal_action();
}

void talk_function::stop_following( npc &p )
{
    // this is to tell non-allied NPCs to stop following.
    // ( usually after a mission where they were temporarily tagging along )
    // so don't tell already allied NPCs to stop following.
    // they use the guard command for that.
    if( p.is_player_ally() ) {
        return;
    }
    add_msg( _( "%s stops following." ), p.get_name() );
    p.set_attitude( NPCATT_NULL );
}

void talk_function::stranger_neutral( npc &p )
{
    add_msg( _( "%s feels less threatened by you." ), p.get_name() );
    p.set_attitude( NPCATT_NULL );
    p.chatbin.first_topic = p.chatbin.talk_stranger_neutral;
}

bool talk_function::drop_stolen_item( item &cur_item, npc &p )
{
    Character &player_character = get_player_character();
    map &here = get_map();
    bool dropped = false;
    if( cur_item.is_old_owner( p ) ) {
        item to_drop = player_character.i_rem( &cur_item );
        to_drop.remove_old_owner();
        to_drop.set_owner( p );
        here.add_item_or_charges( player_character.pos(), to_drop );
        dropped = true;
    } else if( cur_item.is_container() ) {
        bool changed = false;
        for( item *contained : cur_item.all_items_top() ) {
            changed |= drop_stolen_item( *contained, p );
        }
        if( changed ) {
            dropped = true;
            cur_item.on_contents_changed();
        }
    }
    return dropped;
}

void talk_function::drop_stolen_item( npc &p )
{
    bool dropped = false;
    Character &player_character = get_player_character();
    for( item *&elem : player_character.inv_dump() ) {
        dropped |= drop_stolen_item( *elem, p );
    }
    if( dropped ) {
        player_character.invalidate_weight_carried_cache();
    } else {
        debugmsg( "Failed to drop any stolen items." );
    }
    if( p.known_stolen_item ) {
        p.known_stolen_item = nullptr;
    }
    if( player_character.is_hauling() ) {
        player_character.stop_hauling();
    }
    p.set_attitude( NPCATT_NULL );
}

void talk_function::remove_stolen_status( npc &p )
{
    if( p.known_stolen_item ) {
        p.known_stolen_item = nullptr;
    }
    p.set_attitude( NPCATT_NULL );
}

void talk_function::start_mugging( npc &p )
{
    p.set_attitude( NPCATT_MUG );
    add_msg( _( "Pause to stay still.  Any movement may cause %s to attack." ), p.get_name() );
}

void talk_function::player_leaving( npc &p )
{
    p.set_attitude( NPCATT_WAIT_FOR_LEAVE );
    p.patience = 15 - p.personality.aggression;
}

void talk_function::drop_weapon( npc &p )
{
    if( p.is_hallucination() ) {
        return;
    }
    item weap = p.remove_weapon();
    get_map().add_item_or_charges( p.pos(), weap );
}

void talk_function::player_weapon_away( npc &/*p*/ )
{
    Character &player_character = get_player_character();

    std::optional<bionic *> bionic_weapon = player_character.find_bionic_by_uid(
                player_character.get_weapon_bionic_uid() );
    if( bionic_weapon ) {
        player_character.deactivate_bionic( **bionic_weapon );
        return;
    }

    player_character.i_add( player_character.remove_weapon() );
}

void talk_function::player_weapon_drop( npc &/*p*/ )
{
    Character &player_character = get_player_character();
    item weap = player_character.remove_weapon();
    get_map().add_item_or_charges( player_character.pos(), weap );
}

void talk_function::lead_to_safety( npc &p )
{
    mission *reach_safety_mission = mission::reserve_new( mission_MISSION_REACH_SAFETY,
                                    character_id() );
    reach_safety_mission->assign( get_avatar() );
    p.goal = reach_safety_mission->get_target();
    p.set_attitude( NPCATT_LEAD );
}

bool npc_trading::pay_npc( npc &np, int cost )
{
    if( np.op_of_u.owed >= cost ) {
        np.op_of_u.owed -= cost;
        return true;
    }

    return npc_trading::trade( np, cost, _( "Pay:" ) );
}

void talk_function::start_training_npc( npc &p )
{
    teach_domain d;
    d.skill = p.chatbin.skill;
    d.style = p.chatbin.style;
    d.spell = p.chatbin.dialogue_spell;
    d.prof = p.chatbin.proficiency;
    std::vector<Character *> students;
    students.push_back( &p );
    start_training_gen( get_player_character(), students, d );
}

void talk_function::start_training( npc &p )
{
    teach_domain d;
    d.skill = p.chatbin.skill;
    d.style = p.chatbin.style;
    d.spell = p.chatbin.dialogue_spell;
    d.prof = p.chatbin.proficiency;
    std::vector<Character *> students;
    students.push_back( &get_player_character() );
    start_training_gen( p, students, d );
}

void talk_function::start_training_seminar( npc &p )
{
    teach_domain d;
    d.skill = p.chatbin.skill;
    d.style = p.chatbin.style;
    d.spell = p.chatbin.dialogue_spell;
    d.prof = p.chatbin.proficiency;
    std::vector<npc *> followers = g->get_npcs_if( [&p]( const npc & n ) {
        return n.is_player_ally() && n.is_following() && n.can_hear( p.pos(), p.get_shout_volume() );
    } );
    std::vector<Character *> students;
    for( npc *n : followers ) {
        if( n && p.getID() != n->getID() ) {
            students.push_back( n );
        }
    }
    students.push_back( &get_player_character() );

    std::vector<Character *> picked;
    std::function<bool( const Character * )> include_func = [&]( const Character * c ) {
        if( d.skill != skill_id() ) {
            return c->get_knowledge_level( d.skill ) < p.get_knowledge_level( d.skill );
        } else if( d.style != matype_id() ) {
            return !c->martial_arts_data->has_martialart( d.style );
        } else if( d.prof != proficiency_id() ) {
            return !c->has_proficiency( d.prof );
        } else if( d.spell != spell_id() ) {
            const bool knows = c->magic->knows_spell( d.spell );
            return !knows || c->magic->get_spell( d.spell ).get_level() <
                   p.magic->get_spell( d.spell ).get_level();
        }
        return false;
    };
    std::vector<int> selected = npcs_select_menu( students, _( "Who should participate?" ),
    [&include_func]( const Character * ch ) {
        return !include_func( ch );
    } );

    if( selected.empty() ) {
        return;
    }
    picked.reserve( selected.size() );
    for( int sel : selected ) {
        picked.emplace_back( students[sel] );
    }
    start_training_gen( p, picked, d );
}

void talk_function::start_training_gen( Character &teacher, std::vector<Character *> &students,
                                        teach_domain &d )
{
    int cost = 0;
    time_duration time = 0_turns;
    std::string name;
    const skill_id &skill = d.skill;
    const matype_id &style = d.style;
    const spell_id &sp_id = d.spell;
    const proficiency_id &proficiency = d.prof;
    int expert_multiplier = 1;
    bool player_is_student = false;

    for( Character *student : students ) {
        if( student->is_avatar() ) {
            player_is_student = true;
        }
        int tmp_cost = 0;
        time_duration tmp_time = 0_turns;
        if( skill != skill_id() &&
            student->get_knowledge_level( skill ) < teacher.get_knowledge_level( skill ) ) {
            tmp_cost = calc_skill_training_cost_char( teacher, *student, skill );
            tmp_time = calc_skill_training_time_char( teacher, *student, skill );
            name = skill.str();
        } else if( style != matype_id() &&
                   !student->martial_arts_data->has_martialart( style ) ) {
            tmp_cost = calc_ma_style_training_cost( teacher, *student, style );
            tmp_time = calc_ma_style_training_time( teacher, *student, style );
            name = style.str();
        } else if( sp_id != spell_id() ) {
            // already checked if can learn this spell in npctalk.cpp
            tmp_cost = calc_spell_training_cost( teacher, *student, sp_id );
            tmp_time = calc_spell_training_time( teacher, *student, sp_id );
            name = sp_id.str();
            const spell &temp_spell = teacher.magic->get_spell( sp_id );
            const bool knows = student->magic->knows_spell( sp_id );
            expert_multiplier = knows ? temp_spell.get_level() -
                                student->magic->get_spell( sp_id ).get_level() : 1;
        } else if( proficiency != proficiency_id() ) {
            tmp_cost = calc_proficiency_training_cost( teacher, *student, proficiency );
            tmp_time = calc_proficiency_training_time( teacher, *student, proficiency );
            name = proficiency.str();
        } else {
            debugmsg( "start_training with no valid skill or style set" );
            return;
        }
        // use the slowest common denominator and combine cost
        cost += tmp_cost;
        time = std::max( time, tmp_time );
    }

    if( !teacher.is_avatar() ) {
        npc &p = static_cast<npc &>( teacher );
        mission *miss = p.chatbin.mission_selected;
        const character_id &pid = get_player_character().getID();
        if( player_is_student && miss != nullptr &&
            miss->get_assigned_player_id() == pid && miss->is_complete( pid ) ) {
            clear_mission( p );
        } else if( !npc_trading::pay_npc( p, cost ) ) {
            return;
        }
    }
    player_activity tact = player_activity( ACT_TRAIN_TEACHER, to_moves<int>( time ),
                                            teacher.getID().get_value(), 0, name );
    for( Character *student : students ) {
        player_activity act = player_activity( ACT_TRAIN, to_moves<int>( time ),
                                               teacher.getID().get_value(), 0, name );
        act.values.push_back( expert_multiplier );
        student->assign_activity( act );
        tact.values.push_back( student->getID().get_value() );
    }
    teacher.assign_activity( tact );

    teacher.add_effect( effect_asked_to_train, 6_hours );
}

npc *pick_follower()
{
    std::vector<npc *> followers;
    std::vector<tripoint> locations;

    for( npc &guy : g->all_npcs() ) {
        if( guy.is_player_ally() && get_player_view().sees( guy ) ) {
            followers.push_back( &guy );
            locations.push_back( guy.pos() );
        }
    }

    pointmenu_cb callback( locations );

    uilist menu;
    menu.text = _( "Select a follower" );
    menu.callback = &callback;
    menu.w_y_setup = 2;

    for( const npc *p : followers ) {
        menu.addentry( -1, true, MENU_AUTOASSIGN, p->get_name() );
    }

    menu.query();
    if( menu.ret < 0 || static_cast<size_t>( menu.ret ) >= followers.size() ) {
        return nullptr;
    }

    return followers[ menu.ret ];
}

void talk_function::copy_npc_rules( npc &p )
{
    const npc *other = pick_follower();
    if( other != nullptr && other != &p ) {
        p.rules = other->rules;
    }
}

void talk_function::set_npc_pickup( npc &p )
{
    p.rules.pickup_whitelist->show( p.name );
}

void talk_function::npc_die( npc &p )
{
    p.die( nullptr );
    const shared_ptr_fast<npc> guy = overmap_buffer.find_npc( p.getID() );
    if( guy && !guy->is_dead() ) {
        guy->marked_for_death = true;
    }
}

void talk_function::npc_thankful( npc &p )
{
    if( p.get_attitude() == NPCATT_MUG || p.get_attitude() == NPCATT_WAIT_FOR_LEAVE ||
        p.get_attitude() == NPCATT_FLEE || p.get_attitude() == NPCATT_KILL ||
        p.get_attitude() == NPCATT_FLEE_TEMP ) {
        p.set_attitude( NPCATT_NULL );
    }
    if( p.chatbin.first_topic != p.chatbin.talk_friend ) {
        p.chatbin.first_topic = p.chatbin.talk_stranger_friendly;
    }
    p.personality.aggression -= 1;

}

void talk_function::clear_overrides( npc &p )
{
    p.rules.clear_overrides();
}

void talk_function::set_npc_vertical_alert_range(npc &p) {

    int value = p.get_vertical_alert_range();

    string_input_popup popup;
    popup.title("设置垂直警戒范围")
        .description(string_format("当前：%d", p.get_vertical_alert_range()))
        .width(2)
        .only_digits(true)
        .edit(value);

    p.set_vertical_alert_range(value);
    
}
