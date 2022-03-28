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

include(deps/Boost)
find_package(OpenSSL 1.1.1 REQUIRED)
set_target_properties(OpenSSL::SSL PROPERTIES
  INTERFACE_COMPILE_DEFINITIONS OPENSSL_NO_SSL2
)
add_subdirectory(src/secp256k1)
add_subdirectory(src/ed25519-donna)
find_package(lz4 REQUIRED)
# Target names with :: are not allowed in a generator expression.
# We need to pull the include directories and imported location properties
# from separate targets.
add_library(lz4_headers ALIAS lz4::lz4)
add_library(lz4_library ALIAS CONAN_LIB::lz4_lz4)
find_package(LibArchive REQUIRED)
find_package(SOCI REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Snappy REQUIRED)
add_library(snappy_headers ALIAS Snappy::snappy)
add_library(snappy_library ALIAS CONAN_LIB::Snappy_snappy)
find_package(RocksDB REQUIRED)
set_target_properties(RocksDB::rocksdb PROPERTIES
  INTERFACE_COMPILE_DEFINITIONS RIPPLE_ROCKSDB_AVAILABLE=1
)

find_package(NuDB REQUIRED)
find_package(date REQUIRED)
include(deps/Protobuf)
include(deps/gRPC)

target_link_libraries(ripple_libs INTERFACE
  ed25519::ed25519
  LibArchive::LibArchive
  lz4::lz4
  NuDB::NuDB
  OpenSSL::Crypto
  OpenSSL::SSL
  Ripple::grpc_pbufs
  Ripple::pbufs
  RocksDB::rocksdb
  secp256k1::secp256k1
  SOCI::SOCI
  SQLite::SQLite3
)

if(reporting)
  find_package(cassandra-cpp-driver REQUIRED)
  find_package(PostgreSQL REQUIRED)
  target_link_libraries(ripple_libs INTERFACE
    cassandra-cpp-driver::cassandra-cpp-driver
    PostgreSQL::PostgreSQL
  )
endif()

###

include(RippledCore)
include(RippledInstall)
include(RippledCov)
include(RippledMultiConfig)
include(RippledDocs)
include(RippledValidatorKeys)
