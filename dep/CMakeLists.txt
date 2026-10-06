# This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

add_subdirectory(threads)

if(SERVERS OR TOOLS)
  add_subdirectory(boost)
  add_subdirectory(zlib)
  add_subdirectory(g3dlite)
  add_subdirectory(recastnavigation)
  add_subdirectory(fmt)
  add_subdirectory(SFMT)
  add_subdirectory(utf8cpp)
  add_subdirectory(valgrind)
  add_subdirectory(openssl)
  if(CMAKE_SYSTEM_NAME MATCHES "Linux" AND NOT NOJEM)
    add_subdirectory(jemalloc)
  endif()
  add_subdirectory(argon2)
  add_subdirectory(short_alloc)
endif()

if(SERVERS)
  add_subdirectory(mysql)
  add_subdirectory(readline)
  add_subdirectory(gsoap)
endif()

if(SERVERS)
  # NATS C client (cluster control-channel transport), vendored at dep/cnats (nats.c 3.8.0).
  # Static only, no TLS/streaming/examples; EXCLUDE_FROM_ALL keeps its install() rules out
  # of ${CMAKE_INSTALL_PREFIX}. BUILD_TESTING is saved/restored as a directory-scoped
  # variable so cnats' enable_testing() cannot switch off TrinityCore's own Catch2 suite.
  set(NATS_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_LIB_SHARED OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_LIB_STATIC ON  CACHE BOOL "" FORCE)
  set(NATS_BUILD_STREAMING  OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_WITH_TLS   OFF CACHE BOOL "" FORCE)
  set(_tc_saved_build_testing "${BUILD_TESTING}")
  set(BUILD_TESTING OFF)
  add_subdirectory(cnats EXCLUDE_FROM_ALL)
  set(BUILD_TESTING "${_tc_saved_build_testing}")
  if(TARGET nats_static)
    # nats.c exports its whole src/ tree as the public include directory, and that tree
    # also holds hash.h, timer.h and util.h. On a case-insensitive filesystem (Windows,
    # macOS) those shadow TrinityCore's Hash.h, Timer.h and Util.h for every consumer of
    # nats_static. Export only the public headers: nats.h plus the status.h/version.h it
    # includes (version.h is generated into cnats/src by cnats' own configure step).
    set(_cnats_public "${CMAKE_CURRENT_BINARY_DIR}/cnats-public")
    file(MAKE_DIRECTORY "${_cnats_public}")
    foreach(_h nats.h status.h version.h)
      configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cnats/src/${_h}" "${_cnats_public}/${_h}" COPYONLY)
    endforeach()
    # cnats carries install(EXPORT) rules, so a build-tree path must stay behind BUILD_INTERFACE.
    set_target_properties(nats_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
      "$<BUILD_INTERFACE:${_cnats_public}>;$<INSTALL_INTERFACE:include>")
    # nats.c forces -std=c99 but uses C11 _Generic; the later flag wins.
    if(NOT MSVC)
      target_compile_options(nats_static PRIVATE -std=c11)
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
      target_compile_options(nats_static PRIVATE -Wno-incompatible-pointer-types-discards-qualifiers)
    endif()
    set_target_properties(nats_static PROPERTIES FOLDER "dep")
  endif()
endif()

if(SERVERS AND BUILD_EFSW)
  add_subdirectory(efsw)
endif()

if(TOOLS)
  add_subdirectory(bzip2)
  add_subdirectory(libmpq)
endif()

if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v2.13.9
    GIT_SHALLOW    1)
  FetchContent_MakeAvailable(Catch2)
endif()
