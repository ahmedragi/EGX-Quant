# Pricing core: no third-party dependencies.
add_library(egxquant
    core/rates.cpp
    core/daycount.cpp
    futures/cost_of_carry.cpp
    futures/basis.cpp
    futures/contract_spec.cpp
    risk/stats.cpp
    risk/var.cpp
    risk/jump_diffusion.cpp
    risk/calibration.cpp
)
add_library(egxquant::egxquant ALIAS egxquant)

target_include_directories(egxquant PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(egxquant PRIVATE egxq_warnings)
target_compile_features(egxquant PUBLIC cxx_std_20)

# Data pipeline: CSV I/O, SQLite market database, synthetic data generator.
add_library(egxquant_data
    data/csv.cpp
    data/market_csv.cpp
    data/market_db.cpp
    data/synthetic.cpp
    data/importers.cpp
    data/xlsx.cpp
    data/markup_util.cpp
    data/html_table.cpp
    data/basis_history.cpp
    data/risk_series.cpp
)
add_library(egxquant::data ALIAS egxquant_data)

target_include_directories(egxquant_data PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(egxquant_data
    PUBLIC  egxquant::egxquant
    PRIVATE SQLite::SQLite3 miniz::miniz egxq_warnings)
target_compile_features(egxquant_data PUBLIC cxx_std_20)
