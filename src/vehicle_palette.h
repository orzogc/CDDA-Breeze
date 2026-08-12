#pragma once
#ifndef CATA_SRC_VEHICLE_PALETTE_H
#define CATA_SRC_VEHICLE_PALETTE_H

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "hsv_color.h"
#include "json.h"
#include "type_id.h"
#include "weighted_list.h"

class VehiclePalette
{
    public:
        VehiclePalette() = default;

        static void load( const JsonObject &jo );
        static void check();
        static void reset();

        int fuzzy_to_index( const vpart_id &id ) const;
        std::vector<std::optional<RGBColor>> pick_colors() const;

    private:
        vpalette_id id;
        std::vector<weighted_int_list<std::string>> colors;
        std::map<std::string, int> fuzzy_color_match;
};

#endif // CATA_SRC_VEHICLE_PALETTE_H
