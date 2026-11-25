add_executable(egx_pricer egx_pricer/main.cpp)
target_link_libraries(egx_pricer PRIVATE egxquant::egxquant egxq_warnings)
