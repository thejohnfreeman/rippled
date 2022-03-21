cmake_minimum_required (VERSION 3.9.0)

if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()

project(rippled)

# make GIT_COMMIT_HASH define available to all sources
find_package(Git)
if(Git_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --abbrev=40
        OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE gch)
    if(gch)
        set(GIT_COMMIT_HASH "${gch}")
        message(STATUS gch: ${GIT_COMMIT_HASH})
        add_definitions(-DGIT_COMMIT_HASH="${GIT_COMMIT_HASH}")
    endif()
endif() #git

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake/deps")

include (CheckCXXCompilerFlag)
if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.11)
  include (FetchContent)
endif ()
if (MSVC AND CMAKE_VERSION VERSION_LESS 3.12)
  message (FATAL_ERROR "MSVC requires cmake 3.12 or greater for proper boost support")
endif ()
include (ExternalProject)
include (CMakeFuncs) # must come *after* ExternalProject b/c it overrides one function in EP
include (ProcessorCount)
if (target)
  message (FATAL_ERROR "The target option has been removed - use native cmake options to control build")
endif ()

include(RippledSanity)
include(RippledVersion)
include(RippledSettings)
include(RippledNIH)
include(RippledRelease)
# this check has to remain in the top-level cmake
# because of the early return statement
if (packages_only)
  if (NOT TARGET rpm)
    message (FATAL_ERROR "packages_only requested, but targets were not created - is docker installed?")
  endif()
  return ()
endif ()
include(RippledCompiler)
include(RippledInterface)

###

include("${CMAKE_CURRENT_BINARY_DIR}/conan_paths.cmake")

include(deps/Boost)
find_package(OpenSSL 1.1.1 REQUIRED)
set_target_properties(OpenSSL::SSL PROPERTIES
  INTERFACE_COMPILE_DEFINITIONS OPENSSL_NO_SSL2
)
include(deps/Secp256k1)
include(deps/Ed25519-donna)
include(deps/Lz4)
include(deps/Libarchive)
find_package(SOCI REQUIRED)
find_package(SQLite3 REQUIRED)
include(deps/Snappy)
include(deps/Rocksdb)
find_package(NuDB REQUIRED)
include(deps/date)
include(deps/Protobuf)
include(deps/gRPC)
include(deps/cassandra)
include(deps/Postgres)

target_link_libraries(ripple_libs INTERFACE NuDB::NuDB)
target_link_libraries(ripple_libs INTERFACE OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(ripple_libs INTERFACE Ripple::grpc_pbufs)
target_link_libraries(ripple_libs INTERFACE Ripple::pbufs)
target_link_libraries(ripple_libs INTERFACE SOCI::SOCI)
target_link_libraries(ripple_libs INTERFACE SQLite::SQLite3)

###

include(RippledCore)
include(RippledInstall)
include(RippledCov)
include(RippledMultiConfig)
include(RippledDocs)
include(RippledValidatorKeys)
