add_executable(egx_pricer egx_pricer/main.cpp)
target_link_libraries(egx_pricer PRIVATE egxquant::egxquant egxq_warnings)

add_executable(egx_synth egx_synth/main.cpp)
target_link_libraries(egx_synth PRIVATE egxquant::data egxq_warnings)

add_executable(egx_db egx_db/main.cpp)
target_link_libraries(egx_db PRIVATE egxquant::data egxq_warnings)

add_executable(egx_basis egx_basis/main.cpp)
target_link_libraries(egx_basis PRIVATE egxquant::data egxq_warnings)

add_executable(egx_var egx_var/main.cpp)
target_link_libraries(egx_var PRIVATE egxquant::data egxq_warnings)

add_executable(egx_mc egx_mc/main.cpp)
target_link_libraries(egx_mc PRIVATE egxquant::data egxq_warnings)

add_executable(egx_hedge egx_hedge/main.cpp)
target_link_libraries(egx_hedge PRIVATE egxquant::data egxq_warnings)

add_executable(egx_stress egx_stress/main.cpp)
target_link_libraries(egx_stress PRIVATE egxquant::data egxq_warnings)
