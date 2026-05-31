# Bundled libjpeg-6b (IJG license). Source list matches SConscript.core exactly.
# Phase 4 will offer libjpeg-turbo behind USE_INTERNAL_JPEG=OFF.

set(JPEG_SOURCES
    jcapimin.c jchuff.c   jcinit.c   jccoefct.c jccolor.c
    jfdctflt.c jcdctmgr.c jcphuff.c  jcmainct.c jcmarker.c
    jcmaster.c jcomapi.c  jcparam.c  jcprepct.c jcsample.c
    jdapimin.c jdapistd.c jdatasrc.c jdcoefct.c jdcolor.c
    jddctmgr.c jdhuff.c   jdinput.c  jdmainct.c jdmarker.c
    jdmaster.c jdpostct.c jdsample.c jdtrans.c  jerror.c
    jidctflt.c jmemmgr.c  jmemnobs.c jutils.c)

add_library(etrm_jpeg STATIC ${JPEG_SOURCES})
target_include_directories(etrm_jpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
etrm_apply_common_definitions(etrm_jpeg)
