// Lua 由微风作为内嵌库编译，不提供独立解释器。
// 与 CCB 的 Make 和 CMake 接入保持一致，Lua 的 .c 文件按 C++ 编译。
// 因此这里故意不添加 extern "C"，确保所有构建方式使用同一套 C++ ABI。
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
