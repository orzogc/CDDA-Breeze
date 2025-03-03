#pragma once
#ifndef CATA_SRC_DISEASE_H
#define CATA_SRC_DISEASE_H

#include <iosfwd>
#include <new>
#include <set>
#include <vector>

#include "calendar.h"
#include <optional>
#include "type_id.h"

class JsonObject;

class disease_type
{
    public:
        static void load_disease_type( const JsonObject &jo, const std::string &src );
        static void reset();
        void load( const JsonObject &jo, const std::string & );
        static const std::vector<disease_type> &get_all();
        static void check_disease_consistency();
        bool was_loaded = false;

        diseasetype_id id;
        std::vector<std::pair<diseasetype_id, mod_id>> src;
        time_duration min_duration = 1_turns;
        time_duration max_duration = 1_turns;
        int min_intensity = 1;
        int max_intensity = 1;
        /**Affected body parts*/
        std::set<bodypart_str_id> affected_bodyparts;
        /**If not empty this sets the health threshold above which you're immune to the disease*/
        std::optional<int> health_threshold;
        /**effect applied by this disease*/
        efftype_id symptoms;

};
#endif // CATA_SRC_DISEASE_H

