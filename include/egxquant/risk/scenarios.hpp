#pragma once

#include <array>
#include <string_view>

/// Built-in stress windows: past Egyptian currency shocks, replayed on today's
/// position from the EGX30 and USD/EGP history in the database.

namespace egxq::risk {

struct Scenario {
    std::string_view name;
    std::string_view from;  ///< ISO date, a few sessions before the event
    std::string_view to;
    std::string_view what;
};

inline constexpr std::array<Scenario, 4> kScenarios{{
    {"float-2016", "2016-10-20", "2016-12-30", "CBE floats the pound, 3 Nov 2016"},
    {"deval-2022-03", "2022-03-14", "2022-04-29", "Devaluation of 21 Mar 2022"},
    {"deval-2022-10", "2022-10-20", "2023-01-31", "Flexible rate from 27 Oct 2022, slide into Jan 2023"},
    {"float-2024", "2024-02-25", "2024-04-30", "Float of 6 Mar 2024"},
}};

}  // namespace egxq::risk
