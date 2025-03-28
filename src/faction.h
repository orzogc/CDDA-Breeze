#pragma once
#ifndef CATA_SRC_FACTION_H
#define CATA_SRC_FACTION_H

#include <bitset>
#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "character_id.h"
#include "color.h"
#include "shop_cons_rate.h"
#include "translations.h"
#include "type_id.h"

namespace catacurses
{
class window;
}  // namespace catacurses

// TODO: Redefine?
static constexpr int MAX_FAC_NAME_SIZE = 40;

std::string fac_ranking_text( int val );
std::string fac_respect_text( int val );
std::string fac_wealth_text( int val, int size );
std::string fac_combat_ability_text( int val );

class item;
class JsonObject;
class JsonOut;
class JsonValue;
class faction;
class npc;

struct dialogue;

using faction_id = string_id<faction>;

namespace npc_factions
{
void finalize();
enum relationship : int {
    kill_on_sight,
    watch_your_back,
    share_my_stuff,
    guard_your_stuff,
    lets_you_in,
    defend_your_space,
    knows_your_voice,
    // final value is the count
    rel_types
};

const std::unordered_map<std::string, relationship> relation_strs = { {
        { "kill on sight", kill_on_sight },
        { "watch your back", watch_your_back },
        { "share my stuff", share_my_stuff },
        { "guard your stuff", guard_your_stuff },
        { "lets you in", lets_you_in },
        { "defends your space", defend_your_space },
        { "knows your voice", knows_your_voice }
    }
};
} // namespace npc_factions

struct faction_price_rule: public icg_entry {
    double markup = 1.0;
    double premium = 1.0;
    std::optional<double> fixed_adj = std::nullopt;
    std::optional<int> price = std::nullopt;

    faction_price_rule() = default;
    faction_price_rule( itype_id const &id, double m, double f )
        : icg_entry{ id, {}, {}, {}, {} }, markup( m ), fixed_adj( f ) {};
    explicit faction_price_rule( icg_entry const &rhs ) : icg_entry( rhs ) {}

    void deserialize( JsonObject const &jo );
};

class faction_price_rules_reader : public generic_typed_reader<faction_price_rules_reader>
{
    public:
        static faction_price_rule get_next( JsonValue &jv );
};

class faction_template
{
    protected:
        faction_template();
        void load_relations( const JsonObject &jsobj );

    private:
        explicit faction_template( const JsonObject &jsobj );

    public:
        static void load( const JsonObject &jsobj );
        static void check_consistency();
        static void reset();

        std::string name;
        int likes_u;
        int respects_u;
        int trusts_u; // Determines which item groups are available for trading
        bool known_by_u;
        faction_id id;
        translation desc;
        int size; // How big is our sphere of influence?
        int power; // General measure of our power
        int food_supply;  //Total nutritional value held
        int wealth;  //Total trade currency
        bool lone_wolf_faction; // is this a faction for just one person?
       
        
        itype_id currency; // id of the faction currency
        std::vector<faction_price_rule> price_rules; // additional pricing rules
        std::map<std::string, std::bitset<npc_factions::rel_types>> relations;
        mfaction_str_id mon_faction; // mon_faction_id of the monster faction; defaults to human
        std::set<std::tuple<int, int, snippet_id>> epilogue_data;
};

class faction : public faction_template
{
    public:
        faction() = default;
        explicit faction( const faction_template &templ );

        void deserialize( const JsonObject &jo );
        void serialize( JsonOut &json ) const;
        void faction_display( const catacurses::window &fac_w, int width ) const;

        std::string describe() const;
        std::vector<std::string> epilogue() const;

        std::string food_supply_text();
        nc_color food_supply_color();

        faction_price_rule const *get_price_rules( item const &it, npc const &guy ) const;

        bool has_relationship( const faction_id &guy_id, npc_factions::relationship flag ) const;
        void add_to_membership( const character_id &guy_id, const std::string &guy_name, bool known );
        void remove_member( const character_id &guy_id );
        int conquer_degree = 0; // 征服度
        int days_required_to_submit_resources = 0; // 被玩家征服的派系，上交资源所需天数
        bool player_has_joined = false; // 玩家是否已经加入了这个派系
        bool if_player_get_food_toady_NO_JOINED = false;
        bool if_player_get_food_today_JOINED = false;  // 玩家加入了这个派系，是否已经领取了今天的食物。
        bool if_player_reported_work_today = false; // 玩家今天是否汇报了工作
        int today_contribution;
        int total_contribution;
        std::vector<int> opinion_of;
        bool validated = false; // NOLINT(cata-serialize)
        std::map<character_id, std::pair<std::string, bool>> members; // NOLINT(cata-serialize)
};

class faction_manager
{
    private:
        std::map<faction_id, faction> factions;

    public:
        void deserialize( const JsonValue &jv );
        void serialize( JsonOut &jsout ) const;

        void clear();
        void create_if_needed();
        void display() const;
        faction *add_new_faction( const std::string &name_new, const faction_id &id_new,
                                  const faction_id &template_id );
        void remove_faction( const faction_id &id );
        const std::map<faction_id, faction> &all() const {
            return factions;
        }

        faction *get( const faction_id &id, bool complain = true );
};

#endif // CATA_SRC_FACTION_H
