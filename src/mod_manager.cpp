#include "mod_manager.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <ostream>
#include <queue>
#include <type_traits>

#include "assign.h"
#include "cata_utility.h"
#include "debug.h"
#include "dependency_tree.h"
#include "filesystem.h"
#include "json.h"
#include "localized_comparator.h"
#include "options.h"
#include "path_info.h"
#include "string_formatter.h"
#include "worldfactory.h"

static const mod_id MOD_INFORMATION_dev_default( "dev:default" );
static const mod_id MOD_INFORMATION_user_default( "user:default" );

static const std::string MOD_SEARCH_FILE( "modinfo.json" );

namespace
{
mod_world_option_position read_world_option_position( const JsonObject &jo,
        const std::string &prefix, bool read_separator )
{
    mod_world_option_position position;
    const std::string before_key = prefix + "before";
    const std::string after_key = prefix + "after";
    const std::string at_key = prefix + "at";
    position.before = jo.get_string( before_key, "" );
    position.after = jo.get_string( after_key, "" );
    position.at = jo.get_string( at_key, "" );
    if( read_separator ) {
        position.separator_before = jo.get_bool( "separator_before", false );
    }

    const int specified = static_cast<int>( !position.before.empty() ) +
                          static_cast<int>( !position.after.empty() ) +
                          static_cast<int>( !position.at.empty() );
    if( specified > 1 ) {
        jo.throw_error( "设置位置只能在 before，after，at 中选择一种" );
    }
    if( !position.at.empty() && position.at != "first" && position.at != "last" ) {
        jo.throw_error_at( at_key, "设置位置 at 只支持 first 或 last" );
    }
    return position;
}
} // namespace

template<>
const MOD_INFORMATION &string_id<MOD_INFORMATION>::obj() const
{
    const auto &map = world_generator->get_mod_manager().mod_map;
    const auto iter = map.find( *this );
    if( iter == map.end() ) {
        debugmsg( "Invalid mod %s requested", str() );
        static const MOD_INFORMATION dummy{};
        return dummy;
    }
    return iter->second;
}

template<>
bool string_id<MOD_INFORMATION>::is_valid() const
{
    return world_generator->get_mod_manager().mod_map.count( *this ) > 0;
}

std::string MOD_INFORMATION::name() const
{
    if( name_.empty() ) {
        // "No name" gets confusing if many mods have no name
        //~ name of a mod that has no name entry, (%s is the mods identifier)
        return string_format( _( "No name (%s)" ), ident.c_str() );
    } else {
        return name_.translated();
    }
}

// These accessors are to delay the initialization of the strings in the respective containers until after gettext is initialized.
const std::vector<std::pair<std::string, translation>> &get_mod_list_categories()
{
    static const std::vector<std::pair<std::string, translation>> mod_list_categories = {
        {"total_conversion", to_translation( "TOTAL CONVERSIONS" )},
        {"content", to_translation( "CORE CONTENT PACKS" )},
        {"items", to_translation( "ITEM ADDITION MODS" )},
        {"creatures", to_translation( "CREATURE MODS" )},
        {"misc_additions", to_translation( "MISC ADDITIONS" )},
        {"buildings", to_translation( "BUILDINGS MODS" )},
        {"vehicles", to_translation( "VEHICLE MODS" )},
        {"rebalance", to_translation( "REBALANCING MODS" )},
        {"magical", to_translation( "MAGICAL MODS" )},
        {"item_exclude", to_translation( "ITEM EXCLUSION MODS" )},
        {"monster_exclude", to_translation( "MONSTER EXCLUSION MODS" )},
        {"graphical", to_translation( "GRAPHICAL MODS" )},
        {"", to_translation( "NO CATEGORY" )}
    };

    return mod_list_categories;
}

const std::vector<std::pair<std::string, translation>> &get_mod_list_tabs()
{
    static const std::vector<std::pair<std::string, translation>> mod_list_tabs = {
        {"tab_default", to_translation( "Default" )},
        {"tab_blacklist", to_translation( "Blacklist" )},
        {"tab_balance", to_translation( "Balance" )}
    };

    return mod_list_tabs;
}

const std::map<std::string, std::string> &get_mod_list_cat_tab()
{
    static const std::map<std::string, std::string> mod_list_cat_tab = {
        {"item_exclude", "tab_blacklist"},
        {"monster_exclude", "tab_blacklist"},
        {"rebalance", "tab_balance"}
    };

    return mod_list_cat_tab;
}

void mod_manager::load_replacement_mods( const cata_path &path )
{
    read_from_file_optional_json( path, [&]( const JsonArray & jsin ) {
        for( JsonArray arr : jsin ) {
            mod_replacements.emplace( mod_id( arr.get_string( 0 ) ),
                                      mod_id( arr.size() > 1 ? arr.get_string( 1 ) : "" ) );
        }
    } );
}

mod_manager::mod_manager()
{
    load_replacement_mods( PATH_INFO::mods_replacements() );
    refresh_mod_list();
    set_usable_mods();
}

mod_manager::~mod_manager() = default;

std::vector<mod_id> mod_manager::all_mods() const
{
    std::vector<mod_id> result;
    std::transform( mod_map.begin(), mod_map.end(),
    std::back_inserter( result ), []( const decltype( mod_manager::mod_map )::value_type & pair ) {
        return pair.first;
    } );
    return result;
}

dependency_tree &mod_manager::get_tree()
{
    return *tree;
}

void mod_manager::clear()
{
    tree->clear();
    mod_map.clear();
    default_mods.clear();
}

void mod_manager::refresh_mod_list()
{
    clear();

    std::map<mod_id, std::vector<mod_id>> mod_dependency_map;
    load_mods_from( PATH_INFO::moddir() );
    load_mods_from( PATH_INFO::user_moddir_path() );

    if( file_exist( PATH_INFO::mods_dev_default() ) ) {
        load_mod_info( PATH_INFO::mods_dev_default() );
    }
    if( file_exist( PATH_INFO::mods_user_default() ) ) {
        load_mod_info( PATH_INFO::mods_user_default() );
    }

    if( !set_default_mods( MOD_INFORMATION_user_default ) ) {
        set_default_mods( MOD_INFORMATION_dev_default );
    }
    // remove these mods from the list, so they do not appear to the user
    remove_mod( MOD_INFORMATION_user_default );
    remove_mod( MOD_INFORMATION_dev_default );
    for( auto &elem : mod_map ) {
        const auto &deps = elem.second.dependencies;
        mod_dependency_map[elem.second.ident] = std::vector<mod_id>( deps.begin(), deps.end() );
    }
    tree->init( mod_dependency_map );
}

void mod_manager::remove_mod( const mod_id &ident )
{
    const auto a = mod_map.find( ident );
    if( a != mod_map.end() ) {
        mod_map.erase( a );
    }
}

void mod_manager::remove_invalid_mods( std::vector<mod_id> &mods ) const
{
    mods.erase( std::remove_if( mods.begin(), mods.end(), [this]( const mod_id & mod ) {
        return mod_map.count( mod ) == 0;
    } ), mods.end() );
}

bool mod_manager::set_default_mods( const mod_id &ident )
{
    // can't use string_id::is_valid as the global mod_manger instance does not exist yet
    const auto iter = mod_map.find( ident );
    if( iter == mod_map.end() ) {
        return false;
    }
    const MOD_INFORMATION &mod = iter->second;
    auto deps = std::vector<mod_id>( mod.dependencies.begin(), mod.dependencies.end() );
    remove_invalid_mods( deps );
    default_mods = deps;
    return true;
}

void mod_manager::load_mods_from( const cata_path &path )
{
    for( cata_path &mod_file : get_files_from_path( MOD_SEARCH_FILE, path, true ) ) {
        load_mod_info( mod_file );
    }
}

void mod_manager::load_modfile( const JsonObject &jo, const cata_path &path )
{
    if( !jo.has_string( "type" ) || jo.get_string( "type" ) != "MOD_INFO" ) {
        // Ignore anything that is not a mod-info
        jo.allow_omitted_members();
        return;
    }

    // TEMPORARY until 0.G: Remove "ident" support
    const mod_id m_ident( jo.has_string( "ident" ) ? jo.get_string( "ident" ) : jo.get_string( "id" ) );
    // can't use string_id::is_valid as the global mod_manger instance does not exist yet
    if( mod_map.count( m_ident ) > 0 ) {
        // TODO: change this to make unique ident for the mod
        // (instead of discarding it?)
        debugmsg( "there is already a mod with ident %s", m_ident.c_str() );
        return;
    }

    translation m_name;
    jo.read( "name", m_name );

    std::string m_cat = jo.get_string( "category", "" );
    std::pair<int, translation> p_cat = {-1, translation()};
    bool bCatFound = false;

    do {
        for( size_t i = 0; i < get_mod_list_categories().size(); ++i ) {
            if( get_mod_list_categories()[i].first == m_cat ) {
                p_cat = { static_cast<int>( i ), get_mod_list_categories()[i].second };
                bCatFound = true;
                break;
            }
        }

        if( !bCatFound && !m_cat.empty() ) {
            m_cat.clear();
        } else {
            break;
        }
    } while( !bCatFound );

    MOD_INFORMATION modfile;
    modfile.ident = m_ident;
    modfile.name_ = m_name;
    modfile.category = p_cat;
    modfile.root_path = path; // BREEZE_LUA_CAMP_API_V1
    // MOD_CAMP_API_V1_BEGIN，世界选项只从 MOD_INFO 预扫描，不等待完整模组 JSON 加载。
    modfile.world_options_position = read_world_option_position( jo, "world_options_", false );
    modfile.world_options_separator = jo.get_bool( "world_options_separator", true );
    if( jo.has_array( "world_options" ) ) {
        for( JsonObject option_jo : jo.get_array( "world_options" ) ) {
            mod_world_option option;
            option.id = option_jo.get_string( "id" );
            option.type = option_jo.get_string( "type" );
            option_jo.read( "name", option.name );
            option_jo.read( "description", option.description );
            option.position = read_world_option_position( option_jo, "", true );
            if( option.type == "bool" ) {
                option.bool_default = option_jo.get_bool( "default", false );
            } else if( option.type == "int" ) {
                option.int_min = option_jo.get_int( "min", 0 );
                option.int_max = option_jo.get_int( "max", 100 );
                option.int_default = option_jo.get_int( "default", option.int_min );
            } else if( option.type == "int_map" ) {
                option.int_default = option_jo.get_int( "default" );
                for( JsonObject value_jo : option_jo.get_array( "values" ) ) {
                    translation label;
                    value_jo.read( "name", label );
                    option.int_values.emplace_back( value_jo.get_int( "value" ), label );
                }
            } else if( option.type == "string_select" ) {
                option.string_default = option_jo.get_string( "default" );
                for( JsonObject value_jo : option_jo.get_array( "values" ) ) {
                    translation label;
                    value_jo.read( "name", label );
                    option.string_values.emplace_back( value_jo.get_string( "value" ), label );
                }
            } else {
                option_jo.throw_error_at( "type", "模组世界设置只支持 bool，int，int_map，string_select" );
            }
            modfile.world_options.emplace_back( std::move( option ) );
        }
    }
    // MOD_CAMP_API_V1_END

    std::string mod_json_path;
    if( assign( jo, "path", mod_json_path ) ) {
        modfile.path = path / mod_json_path;
    } else {
        modfile.path = path;
    }

    assign( jo, "authors", modfile.authors );
    assign( jo, "maintainers", modfile.maintainers );
    assign( jo, "description", modfile.description );
    assign( jo, "version", modfile.version );
    assign( jo, "dependencies", modfile.dependencies );
    assign( jo, "core", modfile.core );
    assign( jo, "obsolete", modfile.obsolete );
    // BREEZE_LUA_CAMP_API_V1_BEGIN，最小 Lua 模组入口。
    assign( jo, "lua_api", modfile.lua_api );
    assign( jo, "lua_preload", modfile.lua_preload );
    assign( jo, "lua_main", modfile.lua_main );
    if( !modfile.lua_api.empty() && modfile.lua_preload.empty() && modfile.lua_main.empty() ) {
        jo.throw_error( "声明 lua_api 的模组至少需要 lua_preload 或 lua_main" );
    }
    // BREEZE_LUA_CAMP_API_V1_END

    if( std::find( modfile.dependencies.begin(), modfile.dependencies.end(),
                   modfile.ident ) != modfile.dependencies.end() ) {
        jo.throw_error_at( "dependencies", "mod specifies self as a dependency" );
    }

    mod_map[modfile.ident] = std::move( modfile );
}

bool mod_manager::set_default_mods( const t_mod_list &mods )
{
    default_mods = mods;
    return write_to_file( PATH_INFO::mods_user_default(), [&]( std::ostream & fout ) {
        JsonOut json( fout, true ); // pretty-print
        json.start_object();
        json.member( "type", "MOD_INFO" );
        json.member( "id", "user:default" );
        json.member( "dependencies" );
        json.write( mods );
        json.end_object();
    }, _( "list of default mods" ) );
}

bool mod_manager::copy_mod_contents( const t_mod_list &mods_to_copy,
                                     const cata_path &output_base_path )
{
    if( mods_to_copy.empty() ) {
        // nothing to copy, so technically we succeeded already!
        return true;
    }
    std::vector<std::string> search_extensions;
    search_extensions.emplace_back( ".json" );

    DebugLog( D_INFO, DC_ALL ) << "Copying mod contents into directory: " << output_base_path;

    if( !assure_dir_exist( output_base_path ) ) {
        DebugLog( D_ERROR, DC_ALL ) << "Unable to create or open mod directory at [" << output_base_path <<
                                    "] for saving";
        return false;
    }

    for( size_t i = 0; i < mods_to_copy.size(); ++i ) {
        const MOD_INFORMATION &mod = *mods_to_copy[i];

        // now to get all of the json files inside of the mod and get them ready to copy
        auto input_files = get_files_from_path( ".json", mod.path, true, true );
        auto input_dirs  = get_directories_with( search_extensions, mod.path, true );

        cata_path mod_base_path = mod.path;

        if( input_files.empty() && mod.path.get_relative_path().filename().u8string() == MOD_SEARCH_FILE ) {
            // Self contained mod, all data is inside the modinfo.json file
            input_files.push_back( mod.path );
            mod_base_path = mod.path.parent_path();
        }

        if( input_files.empty() ) {
            continue;
        }

        // create needed directories
        // NOLINTNEXTLINE(cata-translate-string-literal)
        const cata_path cur_mod_dir = output_base_path / string_format( "mod_%05d", i + 1 );

        std::queue<cata_path> dir_to_make;
        dir_to_make.push( cur_mod_dir );
        for( cata_path &input_dir : input_dirs ) {
            dir_to_make.push( cur_mod_dir / input_dir.get_relative_path().lexically_relative(
                                  mod_base_path.get_relative_path() ) );
        }

        while( !dir_to_make.empty() ) {
            if( !assure_dir_exist( dir_to_make.front() ) ) {
                DebugLog( D_ERROR, DC_ALL ) << "Unable to create or open mod directory at [" <<
                                            dir_to_make.front() << "] for saving";
            }

            dir_to_make.pop();
        }

        // trim file paths from full length down to just /data forward
        for( cata_path &input_file : input_files ) {
            cata_path output_path = cur_mod_dir / ( input_file.get_relative_path().lexically_relative(
                    mod_base_path.get_relative_path() ) );
            copy_file( input_file, output_path );
        }
    }
    return true;
}

// MOD_CAMP_API_V1_BEGIN，按世界的活动模组重建世界设置页。
void mod_manager::apply_world_options( WORLD *world )
{
    options_manager &opts = get_options();
    for( const std::string &id : registered_world_option_group_heads ) {
        opts.remove_separator_before_option( id );
    }
    registered_world_option_group_heads.clear();
    for( const std::string &id : registered_world_options ) {
        opts.remove_option( id );
    }
    registered_world_options.clear();
    if( world == nullptr ) {
        return;
    }

    std::map<std::string, std::string> saved_values;
    for( const auto &entry : world->WORLD_OPTIONS ) {
        saved_values.emplace( entry.first, entry.second.getValue( true ) );
    }

    struct registered_group {
        const MOD_INFORMATION *mod = nullptr;
        std::vector<const mod_world_option *> options;
        std::vector<std::string> ids;
    };
    std::vector<registered_group> groups;
    std::set<std::string> seen;

    // 第一遍只注册。等所有模组选项都存在后再排序，跨模组锚点才可靠。
    for( const mod_id &id : world->active_mod_order ) {
        const auto mod_it = mod_map.find( id );
        if( mod_it == mod_map.end() ) {
            continue;
        }
        registered_group group;
        group.mod = &mod_it->second;
        for( const mod_world_option &option : mod_it->second.world_options ) {
            if( option.id.empty() || !seen.insert( option.id ).second || opts.has_option( option.id ) ) {
                debugmsg( "模组世界设置编号重复或与本体冲突，已忽略，%s", option.id );
                continue;
            }
            if( option.type == "bool" ) {
                opts.add( option.id, "world_default", option.name, option.description, option.bool_default );
            } else if( option.type == "int" ) {
                opts.add( option.id, "world_default", option.name, option.description,
                          option.int_min, option.int_max, option.int_default );
            } else if( option.type == "int_map" ) {
                std::vector<options_manager::int_and_option> values;
                values.reserve( option.int_values.size() );
                for( const auto &value : option.int_values ) {
                    values.emplace_back( value.first, value.second );
                }
                opts.add( option.id, "world_default", option.name, option.description, values,
                          option.int_default, option.int_default );
                opts.get_option( option.id ).setShowValues( true );
            } else if( option.type == "string_select" ) {
                std::vector<options_manager::id_and_option> values;
                values.reserve( option.string_values.size() );
                for( const auto &value : option.string_values ) {
                    values.emplace_back( value.first, value.second );
                }
                opts.add( option.id, "world_default", option.name, option.description, values,
                          option.string_default );
            }

            registered_world_options.insert( option.id );
            group.options.push_back( &option );
            group.ids.push_back( option.id );
            world->WORLD_OPTIONS[option.id] = opts.get_option( option.id );
            const auto old = saved_values.find( option.id );
            if( old != saved_values.end() ) {
                world->WORLD_OPTIONS[option.id].setValue( old->second );
            }
        }
        if( !group.ids.empty() ) {
            groups.emplace_back( std::move( group ) );
        }
    }

    const auto warn_missing_anchor = []( const MOD_INFORMATION &mod, const std::string &anchor ) {
        DebugLog( D_WARNING, DC_ALL ) << "模组世界设置定位锚点不存在，" << mod.ident.str()
                                      << "，" << anchor;
    };
    const auto move_one = [&]( const MOD_INFORMATION &mod, const std::string &option_id,
    const mod_world_option_position &position ) {
        bool moved = true;
        std::string anchor;
        if( !position.before.empty() ) {
            anchor = position.before;
            moved = opts.move_option_before( option_id, anchor );
        } else if( !position.after.empty() ) {
            anchor = position.after;
            moved = opts.move_option_after( option_id, anchor );
        } else if( position.at == "first" ) {
            moved = opts.move_option_to_page_start( option_id, "world_default" );
        } else if( position.at == "last" ) {
            moved = opts.move_option_to_page_end( option_id, "world_default" );
        }
        if( !moved && !anchor.empty() ) {
            warn_missing_anchor( mod, anchor );
        }
        return moved;
    };

    // 第二遍移动整组，组内顺序保持不变。
    for( const registered_group &group : groups ) {
        const mod_world_option_position &position = group.mod->world_options_position;
        if( position.empty() ) {
            continue;
        }
        if( !position.before.empty() ) {
            for( const std::string &option_id : group.ids ) {
                if( !opts.move_option_before( option_id, position.before ) ) {
                    warn_missing_anchor( *group.mod, position.before );
                    break;
                }
            }
        } else if( !position.after.empty() ) {
            std::string anchor = position.after;
            for( const std::string &option_id : group.ids ) {
                if( !opts.move_option_after( option_id, anchor ) ) {
                    warn_missing_anchor( *group.mod, anchor );
                    break;
                }
                anchor = option_id;
            }
        } else if( position.at == "first" ) {
            for( auto it = group.ids.rbegin(); it != group.ids.rend(); ++it ) {
                opts.move_option_to_page_start( *it, "world_default" );
            }
        } else if( position.at == "last" ) {
            for( const std::string &option_id : group.ids ) {
                opts.move_option_to_page_end( option_id, "world_default" );
            }
        }
    }

    // 第三遍应用单项位置。多个 first 需要倒序处理，避免顺序反转。
    for( const registered_group &group : groups ) {
        for( auto it = group.options.rbegin(); it != group.options.rend(); ++it ) {
            if( ( *it )->position.at == "first" ) {
                move_one( *group.mod, ( *it )->id, ( *it )->position );
            }
        }
        for( const mod_world_option *option : group.options ) {
            if( option->position.empty() || option->position.at == "first" ) {
                continue;
            }
            move_one( *group.mod, option->id, option->position );
        }
    }

    // 所有移动结束后再添加分隔行，避免分隔行被遗留在旧位置。
    for( const registered_group &group : groups ) {
        if( group.mod->world_options_separator &&
            opts.add_separator_before_option( group.ids.front() ) ) {
            registered_world_option_group_heads.insert( group.ids.front() );
        }
        for( const mod_world_option *option : group.options ) {
            if( option->position.separator_before &&
                opts.add_separator_before_option( option->id ) ) {
                registered_world_option_group_heads.insert( option->id );
            }
        }
    }
}
// MOD_CAMP_API_V1_END

void mod_manager::load_mod_info( const cata_path &info_file_path )
{
    const cata_path main_path = info_file_path.parent_path();
    read_from_file_optional_json( info_file_path, [&]( const JsonValue & jsin ) {
        if( jsin.test_object() ) {
            // find type and dispatch single object
            JsonObject jo = jsin.get_object();
            load_modfile( jo, main_path );
        } else if( jsin.test_array() ) {
            // find type and dispatch each object until array close
            for( JsonObject jo : jsin.get_array() ) {
                load_modfile( jo, main_path );
            }
        } else {
            // not an object or an array?
            jsin.throw_error( "expected array or object" );
        }
    } );
}

cata_path mod_manager::get_mods_list_file( const WORLD *world )
{
    return world->folder_path_path() / "mods.json";
}

void mod_manager::save_mods_list( const WORLD *world ) const
{
    if( world == nullptr ) {
        return;
    }
    const cata_path path = get_mods_list_file( world );
    if( world->active_mod_order.empty() ) {
        // If we were called from load_mods_list to prune the list,
        // and it's empty now, delete the file.
        remove_file( path.get_unrelative_path() );
        return;
    }
    write_to_file( path, [&]( std::ostream & fout ) {
        JsonOut json( fout, true ); // pretty-print
        json.write( world->active_mod_order );
    }, _( "list of mods" ) );
}

void mod_manager::load_mods_list( WORLD *world ) const
{
    if( world == nullptr ) {
        return;
    }
    std::vector<mod_id> &amo = world->active_mod_order;
    amo.clear();
    bool obsolete_mod_found = false;
    read_from_file_optional_json( get_mods_list_file( world ), [&]( const JsonArray & jsin ) {
        for( const std::string line : jsin ) {
            const mod_id mod( line );
            if( std::find( amo.begin(), amo.end(), mod ) != amo.end() ) {
                continue;
            }
            const auto iter = mod_replacements.find( mod );
            if( iter != mod_replacements.end() ) {
                if( !iter->second.is_empty() ) {
                    amo.push_back( iter->second );
                }
                obsolete_mod_found = true;
                continue;
            }
            amo.push_back( mod );
        }
    } );
    if( obsolete_mod_found ) {
        // If we found an obsolete mod, overwrite the mod list without the obsolete one.
        save_mods_list( world );
    }
}

const mod_manager::t_mod_list &mod_manager::get_default_mods() const
{
    return default_mods;
}

static bool compare_mod_by_name_and_category( const MOD_INFORMATION *const a,
        const MOD_INFORMATION *const b )
{
    return localized_compare( std::make_pair( a->category, a->name() ),
                              std::make_pair( b->category, b->name() ) );
}

void mod_manager::set_usable_mods()
{
    std::vector<mod_id> available_cores;
    std::vector<mod_id> available_supplementals;
    std::vector<mod_id> ordered_mods;

    std::vector<const MOD_INFORMATION *> mods;
    for( const auto &pair : mod_map ) {
        if( !pair.second.obsolete ) {
            mods.push_back( &pair.second );
        }
    }
    std::sort( mods.begin(), mods.end(), &compare_mod_by_name_and_category );

    for( const MOD_INFORMATION *const modinfo : mods ) {
        if( modinfo->core ) {
            available_cores.push_back( modinfo->ident );
        } else {
            available_supplementals.push_back( modinfo->ident );
        }
    }
    ordered_mods.insert( ordered_mods.begin(), available_supplementals.begin(),
                         available_supplementals.end() );
    ordered_mods.insert( ordered_mods.begin(), available_cores.begin(), available_cores.end() );

    usable_mods = ordered_mods;
}
