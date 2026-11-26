# miniz: single-file zip/deflate library, used to read .xlsx workbooks.
include(FetchContent)
FetchContent_Declare(
    miniz
    URL      https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
    URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(miniz)

add_library(egxq_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
add_library(miniz::miniz ALIAS egxq_miniz)
target_include_directories(egxq_miniz SYSTEM PUBLIC ${miniz_SOURCE_DIR})
# Read-only, in-memory use: files are loaded by our own code (portable paths).
target_compile_definitions(egxq_miniz PUBLIC
    MINIZ_NO_STDIO
    MINIZ_NO_TIME
    MINIZ_NO_ARCHIVE_WRITING_APIS)
set_target_properties(egxq_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
