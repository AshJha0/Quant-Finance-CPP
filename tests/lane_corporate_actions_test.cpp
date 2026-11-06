// Mergers and delistings in both bar engines: same events, same sources, and
// no sight of a deal before its effective bar.
#include <cmath>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/alpha/alpha_backtester.hpp"
#include "quantfinlib/alpha/alpha_context.hpp"
#include "quantfinlib/backtest/corporate_actions.hpp"
#include "quantfinlib/backtest/portfolio/portfolio_backtester.hpp"

using namespace quantfinlib;
namespace pbt = portfolio_backtester;
namespace abt = alpha_backtester;

namespace {

BarSeries flatOpenClose(const std::string& symbol, const std::vector<double>& px) {
    auto b = BarSeries::builder(symbol);
    std::int64_t t = 0;
    for (double p : px) {
        b.add(t++, p, p, p, p, 1e12);
    }
    return b.build();
}

class Weights final : public PortfolioStrategy {
public:
    explicit Weights(std::map<int, std::map<std::string, double>> script) : script_(std::move(script)) {}
    std::string name() const override { return "w"; }
    void init(const std::map<std::string, BarSeries>&) override {}
    std::map<std::string, double> targetWeights(int i) override {
        auto it = script_.upper_bound(i);
        return it == script_.begin() ? std::map<std::string, double>{} : std::prev(it)->second;
    }

private:
    std::map<int, std::map<std::string, double>> script_;
};

const std::vector<double> kTarget = {50, 50, 50, 50, 50, 50};
const std::vector<double> kAcquirer = {100, 100, 100, 120, 120, 120};

std::map<std::string, BarSeries> panel() {
    return {{"ACQ", flatOpenClose("ACQ", kAcquirer)}, {"TGT", flatOpenClose("TGT", kTarget)}};
}

pbt::Config frictionless() {
    pbt::Config c = pbt::Config::defaults();
    c.initialCapital = 1000;
    c.commissionRate = 0;
    c.maxParticipationRate = 0;
    c.borrowRateAnnual = 0;
    c.financingRateAnnual = 0;
    return c.withRebalanceEvery(100);   // one decision at bar 0, then hold
}

/** Buys 20 TGT at bar 1's open and holds. */
pbt::Result holdTarget(const pbt::Config& c) {
    Weights s({{0, {{"TGT", 1.0}}}});
    return pbt::run(s, panel(), c);
}

abt::Config alphaConfig() {
    abt::Config c;
    c.startIndex = 0;
    c.rebalanceEveryBars = 100;
    c.commissionBps = 0;
    c.halfSpreadBps = 0;
    c.slippageBps = 0;
    c.capital = 0;
    c.maxParticipationRate = 0;
    c.borrowBpsAnnual = 0;
    c.financingBpsAnnual = 0;
    return c;
}

AlphaContext alphaPanel() {
    return AlphaContext::of({{"ACQ", flatOpenClose("ACQ", kAcquirer)}, {"TGT", flatOpenClose("TGT", kTarget)}});
}

abt::Result alphaHoldTarget(const AlphaContext& ctx, const abt::Config& c) {
    AlphaFactor factor("f", [](const AlphaContext& x, int) { return std::vector<double>(x.symbols().size(), 1.0); });
    return abt::run(ctx, factor, c, [](const AlphaContext&, const std::vector<double>&, int) {
        return std::vector<double>{0.0, 1.0};   // symbols sort as ACQ, TGT
    });
}

}  // namespace

// ------------------------------------------------------------ portfolio

TEST(portfolioCashMergerPaysTheDealPrice) {
    pbt::Config c = frictionless();
    c.mergers.push_back(Merger{"TGT", 3, 60, 0, ""});
    pbt::Result r = holdTarget(c);
    EXPECT_NEAR(r.equityCurve[2], 1000, 1e-9);
    EXPECT_NEAR(r.equityCurve[3], 20 * 60.0, 1e-9);
    EXPECT_TRUE(r.finalPositions.empty());
    EXPECT_TRUE(r.lifecycleEventsApplied == 1);
}

TEST(portfolioStockMergerConvertsIntoTheAcquirerAtTheRatio) {
    pbt::Config c = frictionless();
    c.mergers.push_back(Merger{"TGT", 3, 0, 0.5, "ACQ"});
    pbt::Result r = holdTarget(c);
    EXPECT_NEAR(r.finalPositions.at("ACQ"), 10, 1e-9);
    EXPECT_TRUE(r.finalPositions.count("TGT") == 0);
    EXPECT_NEAR(r.equityCurve[5], 10 * 120.0, 1e-9);
    EXPECT_NEAR(r.totalTurnoverNotional, 1000, 1e-9);   // conversion is not a trade
}

TEST(portfolioMixedMergerPaysBothLegs) {
    pbt::Config c = frictionless();
    c.mergers.push_back(Merger{"TGT", 3, 10, 0.25, "ACQ"});
    pbt::Result r = holdTarget(c);
    EXPECT_NEAR(r.finalPositions.at("ACQ"), 5, 1e-9);
    EXPECT_NEAR(r.equityCurve[3], 20 * 10.0 + 5 * 120.0, 1e-9);
}

TEST(portfolioShortConvertsToAShortInTheAcquirerAndPaysTheCash) {
    pbt::Config c = frictionless();
    c.mergers.push_back(Merger{"TGT", 3, 10, 0.25, "ACQ"});
    Weights s({{0, {{"TGT", -1.0}}}});
    pbt::Result r = pbt::run(s, panel(), c);
    EXPECT_NEAR(r.finalPositions.at("ACQ"), -5, 1e-9);
    EXPECT_NEAR(r.equityCurve[3], 2000 - 200 - 5 * 120.0, 1e-9);
}

TEST(portfolioMergerCannotBeSeenBeforeItsEffectiveBar) {
    pbt::Config none = frictionless();
    pbt::Config rich = frictionless();
    rich.mergers.push_back(Merger{"TGT", 4, 5000, 3, "ACQ"});
    pbt::Result a = holdTarget(none);
    pbt::Result b = holdTarget(rich);
    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(a.equityCurve[static_cast<std::size_t>(i)] == b.equityCurve[static_cast<std::size_t>(i)]);
    }
    EXPECT_TRUE(b.equityCurve[4] > a.equityCurve[4]);
}

TEST(portfolioNoTradeInTheTargetFromTheEffectiveBar) {
    pbt::Config c = frictionless().withRebalanceEvery(1);
    c.mergers.push_back(Merger{"TGT", 3, 60, 0, ""});
    Weights s({{0, {{"TGT", 1.0}}}});
    pbt::Result r = pbt::run(s, panel(), c);
    EXPECT_TRUE(r.finalPositions.empty());
    EXPECT_NEAR(r.equityCurve[5], 1200, 1e-9);
}

TEST(portfolioReadsTheUniverseLikeTheAlphaEngine) {
    PointInTimeUniverse u;
    u.recordDelisting("TGT", 3, -0.5);
    pbt::Config c = frictionless();
    c.universe = &u;
    EXPECT_NEAR(holdTarget(c).equityCurve[3], 500, 1e-9);

    PointInTimeUniverse deal;
    deal.recordMerger("TGT", 3, 0, 0.5, "ACQ");
    c.universe = &deal;
    EXPECT_NEAR(holdTarget(c).finalPositions.at("ACQ"), 10, 1e-9);
}

// ------------------------------------------------------------ alpha

TEST(alphaCashMergerPaysTheDealPrice) {
    abt::Config c = alphaConfig();
    c.mergers.push_back(Merger{"TGT", 3, 60, 0, ""});
    abt::Result r = alphaHoldTarget(alphaPanel(), c);
    EXPECT_NEAR(r.netEquity[2], 1.0, 1e-12);
    EXPECT_NEAR(r.netEquity[3], 1.2, 1e-12);
    EXPECT_NEAR(r.netEquity[5], 1.2, 1e-12);
}

TEST(alphaStockMergerFollowsTheAcquirerAfterConversion) {
    // Acquirer rises after the effective bar here, so the held stock must show it.
    auto b = std::vector<double>{100, 100, 100, 120, 120, 180};
    AlphaContext ctx = AlphaContext::of({{"ACQ", flatOpenClose("ACQ", b)}, {"TGT", flatOpenClose("TGT", kTarget)}});
    abt::Config c = alphaConfig();
    c.mergers.push_back(Merger{"TGT", 3, 0, 0.5, "ACQ"});
    abt::Result r = alphaHoldTarget(ctx, c);
    EXPECT_NEAR(r.netEquity[3], 60.0 / 50, 1e-12);
    EXPECT_NEAR(r.netEquity[5], 90.0 / 50, 1e-12);
}

TEST(alphaMergerCannotBeSeenBeforeItsEffectiveBar) {
    abt::Config rich = alphaConfig();
    rich.mergers.push_back(Merger{"TGT", 4, 5000, 3, "ACQ"});
    abt::Result a = alphaHoldTarget(alphaPanel(), alphaConfig());
    abt::Result b = alphaHoldTarget(alphaPanel(), rich);
    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(a.netEquity[static_cast<std::size_t>(i)] == b.netEquity[static_cast<std::size_t>(i)]);
    }
    EXPECT_TRUE(b.netEquity[4] > a.netEquity[4]);
}

TEST(bothEnginesAgreeOnAMixedDeal) {
    Merger deal{"TGT", 3, 10, 0.25, "ACQ"};
    pbt::Config pc = frictionless();
    pc.mergers.push_back(deal);
    abt::Config ac = alphaConfig();
    ac.mergers.push_back(deal);
    pbt::Result p = holdTarget(pc);
    abt::Result a = alphaHoldTarget(alphaPanel(), ac);
    // The alpha run trades at bar 1's open as well, so both hold through bar 5.
    EXPECT_NEAR(p.equityCurve[5] / 1000, a.netEquity[5], 1e-12);
}

TEST(alphaReadsMergersFromTheUniverse) {
    auto u = std::make_shared<PointInTimeUniverse>();
    u->recordMerger("TGT", 3, 60, 0, "");
    u->addMembership("TGT", 0);
    u->addMembership("ACQ", 0);
    abt::Result r = alphaHoldTarget(alphaPanel().withUniverse(u), alphaConfig());
    EXPECT_NEAR(r.netEquity[3], 1.2, 1e-12);
}

// ------------------------------------------------------------ schedule

TEST(scheduleRefusesWhatItCannotApply) {
    std::vector<std::string> sym = {"ACQ", "TGT"};
    std::vector<std::int64_t> ts = {0, 1, 2, 3};
    using corporate_actions::schedule;
    EXPECT_THROWS(schedule(sym, ts, nullptr, {}, {Merger{"TGT", 2, 0, 1, "MISSING"}}));
    EXPECT_THROWS(schedule(sym, ts, nullptr, {}, {Merger{"TGT", 2, 0, 1, "TGT"}}));
    EXPECT_THROWS(schedule(sym, ts, nullptr, {}, {Merger{"TGT", 2, 0, 0, ""}}));
    EXPECT_THROWS(schedule(sym, ts, nullptr, {}, {Merger{"TGT", 2, -1, 0, ""}}));
    EXPECT_THROWS(schedule(sym, ts, nullptr, {}, {Merger{"TGT", 2, std::nan(""), 0, ""}}));
    EXPECT_THROWS(schedule(sym, ts, nullptr, {}, {Merger{"TGT", 0, 1, 0, ""}}));
    EXPECT_THROWS(schedule(sym, ts, nullptr, {}, {Merger{"TGT", 4, 1, 0, ""}}));
    EXPECT_THROWS(schedule(sym, ts, nullptr, {}, {Merger{"NOPE", 2, 1, 0, ""}}));
    EXPECT_THROWS(schedule(sym, ts, nullptr, {Delisting{"TGT", 1, 0}}, {Merger{"TGT", 2, 1, 0, ""}}));
    EXPECT_THROWS(schedule(sym, ts, nullptr, {Delisting{"ACQ", 2, 0}}, {Merger{"TGT", 2, 0, 1, "ACQ"}}));
    // A dead acquirer is fine if it dies after the deal.
    EXPECT_TRUE(schedule(sym, ts, nullptr, {Delisting{"ACQ", 3, 0}}, {Merger{"TGT", 2, 0, 1, "ACQ"}})[1].merger);
    PointInTimeUniverse u;
    u.recordMerger("TGT", 2, 0, 1, "GONE");
    EXPECT_THROWS(schedule(sym, ts, &u, {}, {}));
    u = PointInTimeUniverse();
    u.recordMerger("TGT", 99, 0, 1, "GONE");   // after the panel: never applied
    EXPECT_TRUE(schedule(sym, ts, &u, {}, {})[1].bar == -1);
}

TEST(configEventReplacesTheUniverseEvent) {
    PointInTimeUniverse u;
    u.recordDelisting("TGT", 1, -1);
    auto out = corporate_actions::schedule({"TGT"}, {0, 1, 2}, &u, {Delisting{"TGT", 2, 0.1}}, {});
    EXPECT_TRUE(out[0].bar == 2);
    EXPECT_NEAR(out[0].delistingReturn, 0.1, 1e-12);
}

QTEST_MAIN()
