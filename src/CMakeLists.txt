add_library(egxquant
    core/rates.cpp
    core/daycount.cpp
    futures/cost_of_carry.cpp
    futures/basis.cpp
)
add_library(egxquant::egxquant ALIAS egxquant)

target_include_directories(egxquant PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(egxquant PRIVATE egxq_warnings)
target_compile_features(egxquant PUBLIC cxx_std_20)
