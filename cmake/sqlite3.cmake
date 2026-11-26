# SQLite, built from the official amalgamation (one C file, no system install needed).
include(FetchContent)
FetchContent_Declare(
    sqlite3
    URL      https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
    URL_HASH SHA256=77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite3)

add_library(egxq_sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
add_library(SQLite::SQLite3 ALIAS egxq_sqlite3)
target_include_directories(egxq_sqlite3 SYSTEM PUBLIC ${sqlite3_SOURCE_DIR})
# Single-threaded use, no extension loading: avoids linking pthread/dl on Unix.
target_compile_definitions(egxq_sqlite3 PRIVATE
    SQLITE_THREADSAFE=0
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_FOREIGN_KEYS=1)
if(UNIX AND NOT APPLE)
    target_link_libraries(egxq_sqlite3 PRIVATE m)
endif()
set_target_properties(egxq_sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
