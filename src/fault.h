#pragma once
#ifndef CATA_SRC_FAULT_H
#define CATA_SRC_FAULT_H

#include <iosfwd>
#include <map>
#include <new>
#include <set>
#include <string>

#include "calendar.h"
#include <optional>
#include "translations.h"
#include "type_id.h"

class JsonObject;

struct mending_method {
    public:
        std::string id;
        translation name;
        translation description;
        translation success_msg;
        time_duration time;
        std::map<std::string, std::string> set_variables;
        std::map<skill_id, int> skills;
        std::optional<fault_id> turns_into;
        std::optional<fault_id> also_mends;
        std::optional<int> heal_stages;

        requirement_data get_requirements() const;
    private:
        friend class fault;
        std::vector<std::pair<requirement_id, int>> requirements;
};

class fault
{
    public:
        fault() : id_( fault_id( "null" ) ) {}

        const fault_id &id() const {
            return id_;
        }

        bool is_null() const {
            return id_ == fault_id( "null" );
        }

        std::string name() const {
            return name_.translated();
        }

        std::string description() const {
            return description_.translated();
        }

        const std::map<std::string, mending_method> &mending_methods() const {
            return mending_methods_;
        }

        const mending_method *find_mending_method( const std::string &id ) const {
            if( mending_methods_.find( id ) != mending_methods_.end() ) {
                return &mending_methods_.at( id );
            } else {
                return nullptr;
            }
        }

        bool has_flag( const std::string &flag ) const {
            return flags.count( flag );
        }

        /** Load fault from JSON definition */
        static void load_fault( const JsonObject &jo );

        /** Get all currently loaded faults */
        static const std::map<fault_id, fault> &all();

        /** Clear all loaded faults (invalidating any pointers) */
        static void reset();

        /** Checks all loaded from JSON are valid */
        static void check_consistency();

    private:
        fault_id id_;
        translation name_;
        translation description_;
        std::map<std::string, mending_method> mending_methods_;
        std::set<std::string> flags;
};

#endif // CATA_SRC_FAULT_H
