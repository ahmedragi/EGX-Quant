#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "egxquant/data/importers.hpp"
#include "egxquant/data/xlsx.hpp"
#include "test_util.hpp"

using namespace egxq;

// Fixtures in tests/fixtures/tbills are built by make_fixtures.ps1 in that folder.
#define TBILL_FIXTURE(name) EGXQ_FIXTURE_DIR "/tbills/" name

TEST(Xlsx, ColumnIndex) {
    EXPECT_EQ(xlsx::column_index("A1"), 0u);
    EXPECT_EQ(xlsx::column_index("J206"), 9u);
    EXPECT_EQ(xlsx::column_index("Z3"), 25u);
    EXPECT_EQ(xlsx::column_index("AA10"), 26u);
    EXPECT_THROW(xlsx::column_index("12"), std::invalid_argument);
}

TEST(Xlsx, ExcelSerialDates) {
    EXPECT_EQ(format_date(xlsx::excel_serial_to_date(46350)), "2026-11-24");
    EXPECT_EQ(format_date(xlsx::excel_serial_to_date(44929)), "2023-01-03");
    EXPECT_EQ(format_date(xlsx::excel_serial_to_date(61)), "1900-03-01");
    EXPECT_EQ(format_date(xlsx::excel_serial_to_date(46350.75)), "2026-11-24");  // time of day dropped
    EXPECT_THROW(xlsx::excel_serial_to_date(60), std::invalid_argument);         // Excel's fake 1900-02-29
}

TEST(Xlsx, ReadsSheetsInWorkbookOrder) {
    const auto sheets = xlsx::read_workbook(TBILL_FIXTURE("auctions_small.xlsx"));
    ASSERT_EQ(sheets.size(), 3u);
    EXPECT_EQ(sheets[0].name, "91 days");
    EXPECT_EQ(sheets[1].name, "182 days");  // relationship target given as an absolute path
    EXPECT_EQ(sheets[2].name, "Notes");

    const auto& s = sheets[0];
    ASSERT_EQ(s.rows.size(), 5u);
    EXPECT_EQ(s.rows[0].cells[0], "EGP T-Bills");
    EXPECT_EQ(s.rows[1].cells[4], "Weighted Avg. Yield (%)");  // rich text runs joined
    EXPECT_EQ(s.rows[2].number, 3u);
    EXPECT_EQ(s.rows[2].cells[2], "46350");
    EXPECT_EQ(s.rows[3].cells[1], "EGT0003");     // inline string
    EXPECT_EQ(s.rows[3].cells[2], "17/11/2026");
    EXPECT_EQ(sheets[2].rows[0].cells[0], "Notes & sources");  // entity decoded

    // Sparse row: column B is absent on sheet 2, so it comes back empty.
    EXPECT_EQ(sheets[1].rows[0].cells[1], "");
    EXPECT_EQ(sheets[1].rows[0].cells[2], "Issue Date");
}

TEST(Xlsx, RejectsNonWorkbooks) {
    testutil::TempDir dir;
    testutil::write_text(dir.path() / "fake.xlsx", "date,close\n");
    EXPECT_THROW(xlsx::read_workbook(dir.path() / "fake.xlsx"), std::runtime_error);
    EXPECT_THROW(xlsx::read_workbook(dir.path() / "missing.xlsx"), std::runtime_error);
}

TEST(TbillImport, ReadsAuctionWorkbook) {
    const auto imp = read_tbill_auctions_xlsx(TBILL_FIXTURE("auctions_small.xlsx"));
    ASSERT_EQ(imp.rows.size(), 3u);
    EXPECT_EQ(imp.skipped, 1u);  // row with an empty yield
    ASSERT_EQ(imp.tenors.size(), 2u);
    EXPECT_EQ(imp.tenors[0], 91);
    EXPECT_EQ(imp.tenors[1], 182);

    // Sorted by date, then tenor; dd/MM/yyyy text and Excel serials both read.
    EXPECT_EQ(format_date(imp.rows[0].date), "2026-11-17");
    EXPECT_EQ(imp.rows[0].tenor_days, 91);
    EXPECT_DOUBLE_EQ(imp.rows[0].yield_pct, 24.608);
    EXPECT_EQ(format_date(imp.rows[1].date), "2026-11-24");
    EXPECT_EQ(imp.rows[1].tenor_days, 91);
    EXPECT_DOUBLE_EQ(imp.rows[1].yield_pct, 24.917);
    EXPECT_EQ(imp.rows[2].tenor_days, 182);
    EXPECT_DOUBLE_EQ(imp.rows[2].yield_pct, 25.358);
}

TEST(TbillImport, RejectsMaturityThatDoesNotMatchTenor) {
    try {
        read_tbill_auctions_xlsx(TBILL_FIXTURE("auctions_bad_maturity.xlsx"));
        FAIL() << "expected an exception";
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("[91 days] row 3"), std::string::npos) << msg;
        EXPECT_NE(msg.find("tenor is 91"), std::string::npos) << msg;
    }
}

TEST(TbillImport, MergeKeysOnDateAndTenor) {
    const std::vector<TbillYield> base{{parse_date("2026-11-24"), 91, 24.9}, {parse_date("2026-11-24"), 182, 25.3}};
    const std::vector<TbillYield> update{{parse_date("2026-11-24"), 91, 24.917}, {parse_date("2026-11-17"), 91, 24.6}};
    const auto m = merge_by_date(base, update);
    EXPECT_EQ(m.added, 1u);
    EXPECT_EQ(m.changed, 1u);
    ASSERT_EQ(m.rows.size(), 3u);
    EXPECT_EQ(format_date(m.rows[0].date), "2026-11-17");
    EXPECT_DOUBLE_EQ(m.rows[1].yield_pct, 24.917);
    EXPECT_EQ(m.rows[2].tenor_days, 182);
}
