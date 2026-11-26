include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(egxquant_tests
    test_rates.cpp
    test_daycount.cpp
    test_cost_of_carry.cpp
    test_basis.cpp
    test_csv.cpp
    test_market_db.cpp
    test_synthetic.cpp
    test_importers.cpp
    test_xlsx.cpp
    test_contract_spec.cpp
    test_futures_import.cpp
    test_stats.cpp
    test_var.cpp
    test_risk_series.cpp
)
target_link_libraries(egxquant_tests PRIVATE egxquant::data egxq_warnings GTest::gtest_main)
target_compile_definitions(egxquant_tests PRIVATE
    EGXQ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

include(GoogleTest)
gtest_discover_tests(egxquant_tests)
