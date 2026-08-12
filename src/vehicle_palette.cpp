#include "vehicle_palette.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "debug.h"
#include "type_id.h"

static std::unordered_map<vpalette_id, VehiclePalette> vehicle_color_palettes;

template<>
const VehiclePalette &string_id<VehiclePalette>::obj() const
{
    const auto iter = vehicle_color_palettes.find( *this );
    if( iter == vehicle_color_palettes.end() ) {
        debugmsg( "invalid vehicle color palette id %s", c_str() );
        static const VehiclePalette dummy{};
        return dummy;
    }
    return iter->second;
}

template<>
bool string_id<VehiclePalette>::is_valid() const
{
    return vehicle_color_palettes.find( *this ) != vehicle_color_palettes.end();
}

void VehiclePalette::load( const JsonObject &jo )
{
    VehiclePalette &palette = vehicle_color_palettes[vpalette_id( jo.get_string( "id" ) )];
    if( jo.has_bool( "clear" ) && jo.get_bool( "clear" ) ) {
        palette.fuzzy_color_match.clear();
        palette.colors.clear();
    }
    palette.id = vpalette_id( jo.get_string( "id" ) );
    for( const JsonObject obj : jo.get_array( "palette" ) ) {
        const int index = static_cast<int>( palette.colors.size() );
        for( const std::string &part_id : obj.get_string_array( "fuzzy_ids" ) ) {
            palette.fuzzy_color_match[part_id] = index;
        }
        weighted_int_list<std::string> weights;
        for( const JsonObject col : obj.get_array( "colors" ) ) {
            weights.add( col.get_string( "color" ), col.get_int( "weight" ) );
        }
        palette.colors.push_back( weights );
    }
}

void VehiclePalette::check()
{
    for( const auto &palette : vehicle_color_palettes ) {
        for( const auto &colorlist : palette.second.colors ) {
            for( const auto &entry : colorlist ) {
                if( !RGBColor::try_parse( entry.obj ) ) {
                    debugmsg( "Invalid Color %s in Vehicle Palette %s", entry.obj, palette.first.str() );
                }
            }
        }
    }
}

int VehiclePalette::fuzzy_to_index( const vpart_id &id ) const
{
    for( const auto &[fuzzy, index] : fuzzy_color_match ) {
        if( id.str() == fuzzy || id.str().find( fuzzy ) != std::string::npos ) {
            return index;
        }
    }
    return -1;
}

std::vector<std::optional<RGBColor>> VehiclePalette::pick_colors() const
{
    std::vector<std::optional<RGBColor>> result;
    result.reserve( colors.size() );
    for( const weighted_int_list<std::string> &colorlist : colors ) {
        const std::string *colorstr = colorlist.pick();
        if( colorstr == nullptr ) {
            result.emplace_back();
            continue;
        }
        const std::optional<RGBColor> color = RGBColor::try_parse( *colorstr );
        if( !color ) {
            debugmsg( "Invalid Color %s in Vehicle Palette %s", *colorstr, id.str() );
        }
        result.push_back( color );
    }
    return result;
}

void VehiclePalette::reset()
{
    vehicle_color_palettes.clear();
}
