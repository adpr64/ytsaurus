/// This file was autogenerated by CMake

#pragma once

// NOTE: has nothing common with DBMS_TCP_PROTOCOL_VERSION,
// only DBMS_TCP_PROTOCOL_VERSION should be incremented on protocol changes.
#define VERSION_REVISION 54477
#define VERSION_NAME "ClickHouse"
#define VERSION_MAJOR 23
#define VERSION_MINOR 8
#define VERSION_PATCH 3
#define VERSION_STRING "23.8.3.1"
#define VERSION_STRING_SHORT "23.8"
/* #undef VERSION_OFFICIAL */
#define VERSION_FULL "ClickHouse 23.8.3.1"
#define VERSION_DESCRIBE "v23.8.3.1-lts"
#define VERSION_INTEGER 23008003

/// These fields are frequently changing and we don't want to have them in the header file to allow caching.
extern const char * VERSION_GITHASH;

#if !defined(VERSION_OFFICIAL)
#   define VERSION_OFFICIAL ""
#endif
