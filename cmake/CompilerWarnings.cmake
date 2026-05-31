# Shared compiler settings for the ET-RM tree.
#
# This is 2005-era C. We do NOT treat warnings as errors, and we silence the
# CRT "deprecation" noise (strcpy/sprintf/etc.) so real diagnostics stay visible.
# Per-target stricter flags can be layered on later (e.g. for new src/sys code).

function(etrm_apply_common_definitions target)
    if(MSVC)
        target_compile_definitions(${target} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_DEPRECATE
            WIN32
            _WINDOWS)
        # 4244/4267: int<->size_t narrowing (pervasive in 32->64 bit port; audited later)
        # 4996: "deprecated" CRT      4101: unreferenced local      4018: signed/unsigned
        target_compile_options(${target} PRIVATE /wd4244 /wd4267 /wd4996 /wd4101 /wd4018)
    else()
        target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
        target_compile_options(${target} PRIVATE
            -fno-strict-aliasing
            -Wno-implicit-function-declaration)
    endif()
endfunction()
