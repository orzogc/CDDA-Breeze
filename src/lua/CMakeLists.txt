set(LUA_SOURCE_NAMES
    lapi.c
    lauxlib.c
    lbaselib.c
    lcode.c
    lcorolib.c
    lctype.c
    ldblib.c
    ldebug.c
    ldo.c
    ldump.c
    lfunc.c
    lgc.c
    linit.c
    liolib.c
    llex.c
    lmathlib.c
    lmem.c
    loadlib.c
    lobject.c
    lopcodes.c
    loslib.c
    lparser.c
    lstate.c
    lstring.c
    lstrlib.c
    ltable.c
    ltablib.c
    ltm.c
    lundump.c
    lutf8lib.c
    lvm.c
    lzio.c)
list(TRANSFORM LUA_SOURCE_NAMES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/" OUTPUT_VARIABLE LUA_SOURCES)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
file(GLOB LUA_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
add_library(liblua ${LUA_SOURCES} ${LUA_HEADERS})
target_include_directories(liblua SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(liblua PRIVATE /w)
else()
    target_compile_options(liblua PRIVATE -w)
endif()
