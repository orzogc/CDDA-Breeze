// BREEZE_LUA_CAMP_API_V1
#include "breeze_lua.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include "basecamp.h"
#include "calendar.h"
#include "debug.h"
#include "filesystem.h"
#include "messages.h"
#include "mod_manager.h"
#include "options.h"
#include "worldfactory.h"
#include "lua/lua.hpp"

namespace breeze_lua
{
namespace
{
lua_State *lua_state = nullptr;
std::map<std::string, int> mod_environments;
basecamp *current_camp = nullptr;
int remaining_hook_calls = 0;
constexpr int hook_granularity = 1000;
constexpr int default_instruction_budget = 200000;

void report_error( const std::string &mod_id, const std::string &stage, const std::string &detail )
{
    DebugLog( D_ERROR, DC_ALL ) << "Lua 模组脚本运行失败，" << mod_id << "，" << stage << "，" << detail;
    add_msg( m_bad, "Lua 模组脚本运行失败，%s，%s。", mod_id.c_str(), stage.c_str() );
}

void instruction_hook( lua_State *state, lua_Debug * )
{
    --remaining_hook_calls;
    if( remaining_hook_calls <= 0 ) {
        luaL_error( state, "脚本执行超过本次允许的指令数量" );
    }
}

bool protected_call( const std::string &mod_id, const std::string &stage, int arguments, int results )
{
    remaining_hook_calls = default_instruction_budget / hook_granularity;
    lua_sethook( lua_state, instruction_hook, LUA_MASKCOUNT, hook_granularity );
    const int status = lua_pcall( lua_state, arguments, results, 0 );
    lua_sethook( lua_state, nullptr, 0, 0 );
    if( status == LUA_OK ) {
        return true;
    }
    const char *message = lua_tostring( lua_state, -1 );
    report_error( mod_id, stage, message == nullptr ? "未知错误" : message );
    lua_pop( lua_state, 1 );
    return false;
}

void set_table_string( lua_State *state, const char *key, const std::string &value )
{
    lua_pushlstring( state, value.data(), value.size() );
    lua_setfield( state, -2, key );
}

void set_table_integer( lua_State *state, const char *key, int value )
{
    lua_pushinteger( state, value );
    lua_setfield( state, -2, key );
}

int l_add_msg( lua_State *state )
{
    const char *text = luaL_checkstring( state, 1 );
    add_msg( m_info, "%s", text );
    return 0;
}

int l_get_turn( lua_State *state )
{
    lua_pushinteger( state, to_turns<int>( calendar::turn - calendar::turn_zero ) );
    return 1;
}

int l_get_world_option( lua_State *state )
{
    const char *id = luaL_checkstring( state, 1 );
    if( world_generator == nullptr || world_generator->active_world == nullptr ) {
        lua_pushnil( state );
        return 1;
    }
    const auto &options = world_generator->active_world->WORLD_OPTIONS;
    const auto it = options.find( id );
    if( it == options.end() ) {
        lua_pushnil( state );
        return 1;
    }
    const auto &option = it->second;
    const std::string type = option.getType();
    if( type == "bool" ) {
        lua_pushboolean( state, option.value_as<bool>() );
    } else if( type == "int" || type == "int_map" ) {
        lua_pushinteger( state, option.value_as<int>() );
    } else {
        const std::string value = option.getValue( true );
        lua_pushlstring( state, value.data(), value.size() );
    }
    return 1;
}

int l_get_camp( lua_State *state )
{
    if( current_camp == nullptr ) {
        lua_pushnil( state );
        return 1;
    }
    lua_newtable( state );
    set_table_string( state, "name", current_camp->camp_name() );
    set_table_integer( state, "development", current_camp->mod_development_score() );
    set_table_integer( state, "expansions", current_camp->mod_expansion_count() );
    set_table_integer( state, "survivors", current_camp->mod_survivor_count() );
    set_table_integer( state, "workers", current_camp->mod_worker_count() );
    set_table_integer( state, "distance", current_camp->mod_player_distance() );
    const tripoint_abs_omt &center = current_camp->mod_center();
    set_table_integer( state, "x", center.x() );
    set_table_integer( state, "y", center.y() );
    set_table_integer( state, "z", center.z() );
    return 1;
}

int l_get_camp_value( lua_State *state )
{
    const char *key = luaL_checkstring( state, 1 );
    const char *fallback = luaL_optstring( state, 2, "" );
    if( current_camp == nullptr ) {
        lua_pushstring( state, fallback );
        return 1;
    }
    const std::string value = current_camp->get_mod_value( key, fallback );
    lua_pushlstring( state, value.data(), value.size() );
    return 1;
}

int l_set_camp_value( lua_State *state )
{
    const char *key = luaL_checkstring( state, 1 );
    const char *value = luaL_checkstring( state, 2 );
    if( current_camp != nullptr ) {
        current_camp->set_mod_value( key, value );
    }
    return 0;
}

int l_clear_camp_value( lua_State *state )
{
    const char *key = luaL_checkstring( state, 1 );
    if( current_camp != nullptr ) {
        current_camp->erase_mod_value( key );
    }
    return 0;
}

void push_game_api( const std::string &mod_id )
{
    lua_newtable( lua_state );
    lua_pushstring( lua_state, api_version );
    lua_setfield( lua_state, -2, "api_version" );
    set_table_string( lua_state, "mod_id", mod_id );
    lua_pushcfunction( lua_state, l_add_msg );
    lua_setfield( lua_state, -2, "add_msg" );
    lua_pushcfunction( lua_state, l_get_turn );
    lua_setfield( lua_state, -2, "get_turn" );
    lua_pushcfunction( lua_state, l_get_world_option );
    lua_setfield( lua_state, -2, "get_world_option" );
    lua_pushcfunction( lua_state, l_get_camp );
    lua_setfield( lua_state, -2, "get_camp" );
    lua_pushcfunction( lua_state, l_get_camp_value );
    lua_setfield( lua_state, -2, "get_camp_value" );
    lua_pushcfunction( lua_state, l_set_camp_value );
    lua_setfield( lua_state, -2, "set_camp_value" );
    lua_pushcfunction( lua_state, l_clear_camp_value );
    lua_setfield( lua_state, -2, "clear_camp_value" );
}

void register_game_api()
{
    push_game_api( "" );
    lua_setglobal( lua_state, "game" );
}

void open_safe_libraries()
{
    luaL_requiref( lua_state, "_G", luaopen_base, 1 );
    lua_pop( lua_state, 1 );
    luaL_requiref( lua_state, LUA_COLIBNAME, luaopen_coroutine, 1 );
    lua_pop( lua_state, 1 );
    luaL_requiref( lua_state, LUA_TABLIBNAME, luaopen_table, 1 );
    lua_pop( lua_state, 1 );
    luaL_requiref( lua_state, LUA_STRLIBNAME, luaopen_string, 1 );
    lua_pop( lua_state, 1 );
    luaL_requiref( lua_state, LUA_MATHLIBNAME, luaopen_math, 1 );
    lua_pop( lua_state, 1 );
    luaL_requiref( lua_state, LUA_UTF8LIBNAME, luaopen_utf8, 1 );
    lua_pop( lua_state, 1 );

    for( const char *name : { "dofile", "loadfile", "load", "collectgarbage", "print" } ) {
        lua_pushnil( lua_state );
        lua_setglobal( lua_state, name );
    }
}

bool is_safe_relative_path( const std::string &text )
{
    if( text.empty() || text.front() == '/' || text.front() == '\\' ) {
        return false;
    }
    if( text.size() > 1 && text[1] == ':' ) {
        return false;
    }
    std::string normalized = text;
    std::replace( normalized.begin(), normalized.end(), '\\', '/' );
    std::istringstream parts( normalized );
    std::string part;
    while( std::getline( parts, part, '/' ) ) {
        if( part == ".." ) {
            return false;
        }
    }
    return true;
}

bool read_script( const MOD_INFORMATION &mod, const std::string &relative,
                  std::string &contents, std::string &display_path )
{
    if( !is_safe_relative_path( relative ) ) {
        report_error( mod.ident.str(), "脚本路径不安全", relative );
        return false;
    }
    const cata_path path = mod.root_path / relative;
    display_path = path.generic_u8string();
    if( !file_exist( path ) ) {
        report_error( mod.ident.str(), "无法读取脚本", display_path );
        return false;
    }
    contents = read_entire_file( path.get_unrelative_path() );
    return true;
}

int create_environment( const std::string &mod_id )
{
    lua_newtable( lua_state );
    lua_pushvalue( lua_state, -1 );
    lua_setfield( lua_state, -2, "_G" );
    // 每个模组拥有独立的 game 表，避免脚本互相覆盖接口字段。
    push_game_api( mod_id );
    lua_setfield( lua_state, -2, "game" );

    lua_newtable( lua_state );
    lua_pushglobaltable( lua_state );
    lua_setfield( lua_state, -2, "__index" );
    lua_setmetatable( lua_state, -2 );
    return luaL_ref( lua_state, LUA_REGISTRYINDEX );
}

bool load_script( const MOD_INFORMATION &mod, int environment, const std::string &relative,
                  const std::string &stage )
{
    if( relative.empty() ) {
        return true;
    }
    std::string source;
    std::string display_path;
    if( !read_script( mod, relative, source, display_path ) ) {
        return false;
    }
    if( luaL_loadbuffer( lua_state, source.data(), source.size(), display_path.c_str() ) != LUA_OK ) {
        const char *message = lua_tostring( lua_state, -1 );
        report_error( mod.ident.str(), stage, message == nullptr ? "脚本编译失败" : message );
        lua_pop( lua_state, 1 );
        return false;
    }
    lua_rawgeti( lua_state, LUA_REGISTRYINDEX, environment );
    if( lua_setupvalue( lua_state, -2, 1 ) == nullptr ) {
        lua_pop( lua_state, 1 );
        report_error( mod.ident.str(), stage, "无法设置脚本环境" );
        return false;
    }
    return protected_call( mod.ident.str(), stage, 0, 0 );
}

bool run_environment_function( const std::string &mod_id, int environment,
                               const std::string &function_name, const std::string &stage,
                               bool required )
{
    lua_rawgeti( lua_state, LUA_REGISTRYINDEX, environment );
    lua_getfield( lua_state, -1, function_name.c_str() );
    lua_remove( lua_state, -2 );
    if( !lua_isfunction( lua_state, -1 ) ) {
        lua_pop( lua_state, 1 );
        if( required ) {
            report_error( mod_id, stage, "没有找到函数，" + function_name );
        }
        return !required;
    }
    return protected_call( mod_id, stage + "，" + function_name, 0, 0 );
}

class camp_scope
{
    public:
        explicit camp_scope( basecamp *camp ) : previous( current_camp ) {
            current_camp = camp;
        }
        ~camp_scope() {
            current_camp = previous;
        }
    private:
        basecamp *previous;
};
} // namespace

void shutdown()
{
    current_camp = nullptr;
    mod_environments.clear();
    if( lua_state != nullptr ) {
        lua_close( lua_state );
        lua_state = nullptr;
    }
}

void run_hook( const std::string &function_name )
{
    if( lua_state == nullptr || function_name.empty() ) {
        return;
    }
    for( const auto &environment : mod_environments ) {
        run_environment_function( environment.first, environment.second, function_name,
                                  "运行脚本钩子", false );
    }
}

void load_world()
{
    shutdown();
    if( world_generator == nullptr || world_generator->active_world == nullptr ) {
        return;
    }
    lua_state = luaL_newstate();
    if( lua_state == nullptr ) {
        DebugLog( D_ERROR, DC_ALL ) << "无法建立 Lua 运行环境";
        return;
    }
    open_safe_libraries();
    register_game_api();

    for( const mod_id &id : world_generator->active_world->active_mod_order ) {
        const MOD_INFORMATION &mod = *id;
        if( mod.lua_api.empty() ) {
            continue;
        }
        if( mod.lua_api != api_version ) {
            report_error( mod.ident.str(), "Lua 接口版本不兼容", mod.lua_api );
            continue;
        }
        const int environment = create_environment( mod.ident.str() );
        mod_environments.emplace( mod.ident.str(), environment );
        if( !load_script( mod, environment, mod.lua_preload, "载入预处理脚本" ) ) {
            continue;
        }
        load_script( mod, environment, mod.lua_main, "载入主脚本" );
    }
    run_hook( "on_world_loaded" );
}

bool run_camp_action( const std::string &source_mod, const std::string &function_name,
                      basecamp &camp )
{
    if( lua_state == nullptr ) {
        report_error( source_mod, "运行营地操作", "Lua 运行环境尚未建立" );
        return false;
    }
    const auto environment = mod_environments.find( source_mod );
    if( environment == mod_environments.end() ) {
        report_error( source_mod, "运行营地操作", "没有找到该模组的脚本环境" );
        return false;
    }
    camp_scope scope( &camp );
    return run_environment_function( source_mod, environment->second, function_name,
                                     "运行营地操作", true );
}
} // namespace breeze_lua
