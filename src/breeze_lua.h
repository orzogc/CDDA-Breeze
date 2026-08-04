// BREEZE_LUA_CAMP_API_V1
#pragma once
#ifndef CATA_SRC_BREEZE_LUA_H
#define CATA_SRC_BREEZE_LUA_H

#include <string>

class basecamp;

namespace breeze_lua
{
constexpr const char *api_version = "1.0";

void load_world();
void shutdown();
// 运行所有模组中同名的可选函数，为后续事件接口提供统一入口。
void run_hook( const std::string &function_name );
bool run_camp_action( const std::string &source_mod, const std::string &function_name,
                       basecamp &camp );
} // namespace breeze_lua

#endif // CATA_SRC_BREEZE_LUA_H
