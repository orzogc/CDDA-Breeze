cmake_minimum_required(VERSION 3.1.4)

message(STATUS "Process LANGUAGES variable --")

if(NOT GETTEXT_MSGFMT_EXECUTABLE)
    message(FATAL_ERROR
            "Gettext not found. Install gettext package or disable \
             localization with \"-DLOCALIZE=OFF\". \
             See doc/COMPILING/COMPILING-CMAKE.md for details and more info.")
endif()

foreach (LANG ${LANGUAGES})
    message(STATUS "Add translation for ${LANG}: ${LANG}.po")
endforeach ()

cmake_path(GET GETTEXT_MSGFMT_EXECUTABLE PARENT_PATH _path)
find_program(XGETTEXT_MSGFMT_EXECUTABLE
    xgettext
    HINTS
        ${_path}
)
if(NOT ${XGETTEXT_MSGFMT_EXECUTABLE})
    set(XGETTEXT_MSGFMT_EXECUTABLE xgettext)
endif()

find_program(GETTEXT_MSGCAT_EXECUTABLE
    msgcat
    HINTS
        ${_path}
)
if(NOT GETTEXT_MSGCAT_EXECUTABLE)
    set(GETTEXT_MSGCAT_EXECUTABLE msgcat)
endif()

# Generate cataclysm-dda.pot
add_custom_target(
        translations
        COMMAND ${CMAKE_SOURCE_DIR}/lang/update_pot.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_custom_target(
        translations_prepare
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

set(DIRS "${LANGUAGES}")
list(TRANSFORM DIRS REPLACE "\(.+\)" lang/mo/\\1/LC_MESSAGES)
add_custom_command(
        TARGET translations_prepare
        PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E
                make_directory ${DIRS}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

foreach (LANG ${LANGUAGES})
    set(_PO ${CMAKE_SOURCE_DIR}/lang/po/${LANG}.po)
    set(_PO_INPUT ${_PO})
    set(_PO_DEPENDS ${_PO})

    # Breeze keeps the generated Transifex PO untouched.  Hand-maintained fixes
    # live in lang/po_overrides and win on duplicate msgids.
    set(_PO_OVERRIDE ${CMAKE_SOURCE_DIR}/lang/po_overrides/${LANG}.po)
    if(EXISTS ${_PO_OVERRIDE})
        set(_MERGED_PO ${CMAKE_CURRENT_BINARY_DIR}/${LANG}.merged.po)
        add_custom_command(
                OUTPUT ${_MERGED_PO}
                COMMAND ${GETTEXT_MSGCAT_EXECUTABLE} --use-first ${_PO_OVERRIDE} ${_PO} -o ${_MERGED_PO}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                DEPENDS ${_PO_OVERRIDE} ${_PO}
        )
        set(_PO_INPUT ${_MERGED_PO})
        list(APPEND _PO_DEPENDS ${_MERGED_PO})
    endif()

    set(_MO ${CMAKE_SOURCE_DIR}/lang/mo/${LANG}/LC_MESSAGES/cataclysm-dda.mo)
    add_custom_command(
            OUTPUT ${_MO}
            COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} -f ${_PO_INPUT} -o ${_MO}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            DEPENDS ${_PO_DEPENDS} translations_prepare
    )
    list(APPEND _MO_FILES ${_MO})
    if (RELEASE)
        if ("${CMAKE_SYSTEM_NAME}" MATCHES "Windows")
            install(DIRECTORY ${CMAKE_SOURCE_DIR}/lang/mo/${LANG} DESTINATION ${LOCALE_DIR}/mo)
        else ()
            install(DIRECTORY ${CMAKE_SOURCE_DIR}/lang/mo/${LANG} DESTINATION ${LOCALE_DIR})
        endif ()
    endif ()
endforeach()

# Auto-Compile translation on release builds only
if (RELEASE)
    add_custom_target(locale ALL DEPENDS ${_MO_FILES})
else()
    add_custom_target(locale DEPENDS ${_MO_FILES})
endif()
