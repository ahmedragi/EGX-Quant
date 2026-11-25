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
)
target_link_libraries(egxquant_tests PRIVATE egxquant::egxquant egxq_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(egxquant_tests)
