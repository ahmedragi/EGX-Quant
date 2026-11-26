#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "egxquant/core/daycount.hpp"

/// EGX30 index futures contract terms, from the EGX "Futures Contract
/// Specifications" document (version dated 13-07-2026):
///
///   - Contract size: 1 EGP x index points.
///   - Code: EGX30FMMMYY, e.g. EGX30FDEC26.
///   - Expiry / last trading day: the third Wednesday of the expiry month, or the
///     preceding business day if that Wednesday is not a business day.
///   - Two consecutive quarterly contracts (Mar, Jun, Sep, Dec) are listed; a new
///     one is listed the business day after each expiry.
///   - Final settlement: the EGX30 close on the expiry date, cash settled T+1.
///
/// Holidays are not modelled: expiry_date() returns the third Wednesday. EGX
/// publishes each actual expiry date in the specifications document; check
/// new contracts against it.

namespace egxq::egx30 {

inline constexpr double kMultiplier = 1.0;  ///< EGP per index point

/// Third Wednesday of a month.
Date third_wednesday(const std::chrono::year_month& ym);

/// Expiry (last trading day) of the contract expiring in this month.
Date expiry_date(const std::chrono::year_month& ym);

/// True for March, June, September and December.
bool is_quarterly_month(const std::chrono::month& m);

/// Exchange contract code, e.g. "EGX30FDEC26".
std::string contract_code(const std::chrono::year_month& ym);

/// Expiry month of a contract, from either the code ("EGX30FDEC26") or the
/// bulletin name ("EGX30 Index Futures - December 2026"). Case-insensitive.
/// Returns nullopt for anything that is not an EGX30 index future.
std::optional<std::chrono::year_month> parse_contract(std::string_view text);

/// The two contracts listed on a date: the next two quarterly expiries on or
/// after it (a contract still trades on its expiry date).
std::array<std::chrono::year_month, 2> listed_contracts(const Date& d);

}  // namespace egxq::egx30
