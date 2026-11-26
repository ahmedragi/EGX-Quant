#include <gtest/gtest.h>

#include <chrono>

#include "egxquant/data/market_data.hpp"
#include "egxquant/futures/contract_spec.hpp"

using namespace egxq;
using namespace std::chrono;

TEST(Egx30Contract, ExpiryMatchesPublishedTable) {
    // "EGX30 Index Futures Contract Expiry Months" table, EGX specifications (13-07-2026).
    EXPECT_EQ(format_date(egx30::expiry_date(year{2026} / September)), "2026-09-16");
    EXPECT_EQ(format_date(egx30::expiry_date(year{2026} / December)), "2026-12-16");
    EXPECT_EQ(format_date(egx30::expiry_date(year{2027} / March)), "2027-03-17");
    EXPECT_EQ(format_date(egx30::expiry_date(year{2027} / June)), "2027-06-16");
    EXPECT_EQ(format_date(egx30::expiry_date(year{2027} / September)), "2027-09-15");
    EXPECT_EQ(format_date(egx30::expiry_date(year{2027} / December)), "2027-12-15");
}

TEST(Egx30Contract, ThirdWednesday) {
    EXPECT_EQ(format_date(egx30::third_wednesday(year{2026} / July)), "2026-07-15");  // July 1 is a Wednesday
    EXPECT_EQ(format_date(egx30::third_wednesday(year{2026} / November)), "2026-11-18");
}

TEST(Egx30Contract, Codes) {
    EXPECT_EQ(egx30::contract_code(year{2026} / December), "EGX30FDEC26");
    EXPECT_EQ(egx30::contract_code(year{2027} / March), "EGX30FMAR27");
    EXPECT_TRUE(egx30::is_quarterly_month(June));
    EXPECT_FALSE(egx30::is_quarterly_month(July));
}

TEST(Egx30Contract, ParsesCodesAndBulletinNames) {
    EXPECT_EQ(egx30::parse_contract("EGX30FDEC26"), year{2026} / December);
    EXPECT_EQ(egx30::parse_contract("egx30fmar27"), year{2027} / March);
    EXPECT_EQ(egx30::parse_contract("EGX30 Index Futures - December 2026"), year{2026} / December);
    EXPECT_EQ(egx30::parse_contract("  EGX30 Index Futures - March 2027 "), year{2027} / March);
    EXPECT_EQ(egx30::parse_contract("EGX30 Index Futures-Mar2027"), year{2027} / March);

    EXPECT_FALSE(egx30::parse_contract("Commercial International Bank Futures - December 2026"));
    EXPECT_FALSE(egx30::parse_contract("COMIFDEC26"));
    EXPECT_FALSE(egx30::parse_contract("EGX30FXYZ26"));
    EXPECT_FALSE(egx30::parse_contract("EGX30 Index Futures - Smarch 2027"));
}

TEST(Egx30Contract, ListedContractsRollAfterExpiry) {
    auto listed = egx30::listed_contracts(parse_date("2026-11-26"));
    EXPECT_EQ(egx30::contract_code(listed[0]), "EGX30FDEC26");
    EXPECT_EQ(egx30::contract_code(listed[1]), "EGX30FMAR27");

    listed = egx30::listed_contracts(parse_date("2026-12-16"));  // expiry day: still trades
    EXPECT_EQ(egx30::contract_code(listed[0]), "EGX30FDEC26");

    listed = egx30::listed_contracts(parse_date("2026-12-17"));
    EXPECT_EQ(egx30::contract_code(listed[0]), "EGX30FMAR27");
    EXPECT_EQ(egx30::contract_code(listed[1]), "EGX30FJUN27");
}
