# splines — camera/path spline math (C++). Source list matches SConscript.core.

set(SPLINES_SOURCES
    math_angles.cpp
    math_matrix.cpp
    math_quaternion.cpp
    math_vector.cpp
    q_parse.cpp
    q_shared.c
    splines.cpp
    util_str.cpp)

add_library(etrm_splines STATIC ${SPLINES_SOURCES})
target_include_directories(etrm_splines PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ETRM_SRC}/game)
etrm_apply_common_definitions(etrm_splines)
