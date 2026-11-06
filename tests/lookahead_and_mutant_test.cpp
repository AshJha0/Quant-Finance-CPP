// Two guards. First, the look-ahead detector: change everything after bar k
// and nothing decided up to bar k may move, in any engine. Second, tests that
// pin the formulas H20's surviving mutants changed, each with a value worked
// from the published formula rather than from this code.
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/alpha/alpha_backtester.hpp"
#include "quantfinlib/alpha/alpha_context.hpp"
#include "quantfinlib/alpha/factors.hpp"
#include "quantfinlib/alpha/portfolio_construction.hpp"
#include "quantfinlib/backtest/backtester.hpp"
#include "quantfinlib/backtest/execution_aware_backtester.hpp"
#include "quantfinlib/backtest/instant_execution.hpp"
#include "quantfinlib/backtest/portfolio/cross_sectional_momentum.hpp"
#include "quantfinlib/backtest/portfolio/portfolio_backtester.hpp"
#include "quantfinlib/backtest/strategies/bollinger_bands_strategy.hpp"
#include "quantfinlib/backtest/strategies/ema_cross_strategy.hpp"
#include "quantfinlib/backtest/strategies/macd_strategy.hpp"
#include "quantfinlib/backtest/strategies/rsi_strategy.hpp"
#include "quantfinlib/backtest/strategies/sma_cross_strategy.hpp"
#include "quantfinlib/backtest/validation/overfit_probability.hpp"
#include "quantfinlib/backtest/validation/sharpe_validation.hpp"
#include "quantfinlib/data/bar_series.hpp"
#include "quantfinlib/microstructure/almgren_chriss.hpp"
#include "quantfinlib/microstructure/market_impact_model.hpp"
#include "quantfinlib/microstructure/vpin.hpp"
#include "quantfinlib/trading/auto_hedger.hpp"
#include "quantfinlib/util/math_utils.hpp"

using namespace quantfinlib;
namespace abt = alpha_backtester;
namespace pbt = portfolio_backtester;

namespace {

constexpr int N = 120;
constexpr int K = 80;  // last bar that must not notice the change

/** A wandering series; after bar `from` every field is replaced by different data. */
BarSeries walk(const std::string& symbol, unsigned seed, int from) {
    auto b = BarSeries::builder(symbol);
    double close = 100;
    unsigned state = seed;
    auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 16777216.0 - 0.5;
    };
    for (int i = 0; i < N; i++) {
        double open = close * (1 + 0.01 * next());
        close = open * (1 + 0.04 * next());
        double o = open, c = close, v = 1e7;
        if (i > from) {
            o = open * (i % 2 == 0 ? 1.3 : 0.7);
            c = close * (i % 2 == 0 ? 0.6 : 1.5);
            v = 2e7;
        }
        b.add(i, o, std::max(o, c), std::min(o, c), c, v);
    }
    return b.build();
}

BarSeries base(const std::string& s, unsigned seed) { return walk(s, seed, N); }
BarSeries changed(const std::string& s, unsigned seed) { return walk(s, seed, K); }

bool sameUpTo(const std::vector<double>& a, const std::vector<double>& b, int last) {
    for (int i = 0; i <= last; i++) {
        if (a[static_cast<std::size_t>(i)] != b[static_cast<std::size_t>(i)]) {
            return false;
        }
    }
    return true;
}

template <typename Make>
void checkBarEngine(Make make, const BacktestConfig& config) {
    BarSeries a = base("X", 7);
    BarSeries b = changed("X", 7);
    auto s1 = make();
    auto s2 = make();
    BacktestResult r1 = backtester::run(*s1, a, config);
    BacktestResult r2 = backtester::run(*s2, b, config);
    EXPECT_TRUE(sameUpTo(r1.equityCurve(), r2.equityCurve(), K));

    auto s3 = make();
    auto s4 = make();
    InstantExecution m1 = InstantExecution::from(config);
    InstantExecution m2 = InstantExecution::from(config);
    ExecutionAwareResult e1 = ExecutionAwareBacktester::run(*s3, a, config, m1);
    ExecutionAwareResult e2 = ExecutionAwareBacktester::run(*s4, b, config, m2);
    EXPECT_TRUE(sameUpTo(e1.backtest().equityCurve(), e2.backtest().equityCurve(), K));
}

template <typename S, typename... Args>
auto maker(Args... args) {
    return [=] { return std::make_unique<S>(args...); };
}

/** Peeks one bar ahead on purpose: proves the detector can see a cheat. */
class Cheat final : public TradingStrategy {
public:
    std::string name() const override { return "cheat"; }
    void init(const BarSeries& s) override { series_ = &s; }
    Signal onBar(int i) override {
        if (i + 1 >= series_->size()) {
            return Signal::HOLD;
        }
        return series_->close(i + 1) > series_->close(i) ? Signal::BUY : Signal::SELL;
    }

private:
    const BarSeries* series_ = nullptr;
};

}  // namespace

TEST(shippedStrategiesIgnoreTheFutureInBothBarEnginesAndBothTimings) {
    for (BacktestConfig c : {BacktestConfig::defaults(), BacktestConfig::defaults().withSameBarCloseFill()}) {
        checkBarEngine(maker<SmaCrossStrategy>(5, 20), c);
        checkBarEngine(maker<EmaCrossStrategy>(5, 20), c);
        checkBarEngine(maker<RsiStrategy>(14, 30.0, 70.0), c);
        checkBarEngine(maker<MacdStrategy>(12, 26, 9), c);
        checkBarEngine(maker<BollingerBandsStrategy>(20, 2.0), c);
    }
}

TEST(detectorSeesAStrategyThatPeeks) {
    BacktestConfig c = BacktestConfig::defaults().withSameBarCloseFill();
    Cheat s1, s2;
    BacktestResult r1 = backtester::run(s1, base("X", 7), c);
    BacktestResult r2 = backtester::run(s2, changed("X", 7), c);
    EXPECT_TRUE(!sameUpTo(r1.equityCurve(), r2.equityCurve(), K + 1));
}

TEST(portfolioEngineIgnoresTheFuture) {
    auto panel = [](bool change) {
        std::map<std::string, BarSeries> data;
        for (unsigned s = 0; s < 4; s++) {
            std::string name = "S" + std::to_string(s);
            data.emplace(name, change ? changed(name, s + 1) : base(name, s + 1));
        }
        return data;
    };
    for (pbt::Config c : {pbt::Config::defaults(), pbt::Config::defaults().withIdealisedExecution()}) {
        CrossSectionalMomentum s1(CrossSectionalMomentum::Config(10, 1, 5, 0.5));
        CrossSectionalMomentum s2(CrossSectionalMomentum::Config(10, 1, 5, 0.5));
        pbt::Result r1 = pbt::run(s1, panel(false), c);
        pbt::Result r2 = pbt::run(s2, panel(true), c);
        EXPECT_TRUE(sameUpTo(r1.equityCurve, r2.equityCurve, K));
    }
}

namespace {

AlphaContext alphaPanel(bool change) {
    std::unordered_map<std::string, BarSeries> data;
    for (unsigned s = 0; s < 5; s++) {
        std::string name = "S" + std::to_string(s);
        data.emplace(name, change ? changed(name, s + 11) : base(name, s + 11));
    }
    return AlphaContext::of(data);
}

/** Runs and returns the raw scores each rebalance decided on, by bar. */
std::map<int, std::vector<double>> decisions(const AlphaContext& ctx, const AlphaFactor& factor,
                                             const abt::Config& c, abt::Result* out) {
    std::map<int, std::vector<double>> seen;
    abt::WeightBuilder record = [&seen](const AlphaContext&, const std::vector<double>& scores,
                                        int t) {
        seen[t] = scores;
        return portfolio_construction::zScoreWeights(scores, 1.0, 0.5);
    };
    *out = abt::run(ctx, factor, c, record);
    return seen;
}

bool sameScores(const std::vector<double>& a, const std::vector<double>& b) {
    for (std::size_t i = 0; i < a.size(); i++) {
        if (a[i] != b[i] && !(std::isnan(a[i]) && std::isnan(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST(alphaDecisionsUpToTheCutIgnoreTheFuture) {
    AlphaContext a = alphaPanel(false);
    AlphaContext b = alphaPanel(true);
    std::vector<AlphaFactor> list = {factors::momentum(20, 1), factors::meanReversion(10),
                                     factors::rsi(14), factors::lowVolatility(20),
                                     factors::macd(12, 26, 9), factors::bollinger(20, 2.0)};
    abt::Config defaults = abt::Config::defaults(40);
    defaults.rebalanceEveryBars = 1;
    defaults.capital = 1e6;
    for (const abt::Config& c : {defaults, defaults.withIdealisedExecution()}) {
        for (const AlphaFactor& f : list) {
            abt::Result r1, r2;
            auto d1 = decisions(a, f, c, &r1);
            auto d2 = decisions(b, f, c, &r2);
            for (int t = c.startIndex; t <= K; t++) {
                EXPECT_TRUE(sameScores(d1.at(t), d2.at(t)));
            }
            EXPECT_TRUE(sameUpTo(r1.netEquity, r2.netEquity, K - c.startIndex));
        }
    }
}

TEST(alphaEngineScoresTheDecisionBarNotALaterOne) {
    AlphaContext ctx = alphaPanel(false);
    std::vector<int> asked;
    AlphaFactor spy("spy", [&asked](const AlphaContext& c, int index) {
        asked.push_back(index);
        return std::vector<double>(static_cast<std::size_t>(c.symbolCount()), 0.0);
    });
    std::vector<int> decided;
    abt::Config c = abt::Config::defaults(40);
    c.rebalanceEveryBars = 7;
    abt::run(ctx, spy, c,
             [&decided](const AlphaContext& x, const std::vector<double>&, int t) {
                 decided.push_back(t);
                 return std::vector<double>(static_cast<std::size_t>(x.symbolCount()), 0.0);
             });
    EXPECT_TRUE(!decided.empty() && asked == decided);
}

// ------------------------------------------------------ surviving mutants

TEST(almgrenChrissKappaFollowsTheDiscreteCoshRelation) {
    // Almgren and Chriss (2000): cosh(kappa tau) = 1 + kappa~^2 tau^2 / 2 with
    // kappa~^2 = lambda sigma^2 / eta~. Here tau = 1, eta~ = 1, lambda sigma^2
    // = 0.5, so cosh(kappa) = 1.25 and kappa = ln 2 exactly.
    AlmgrenChriss::Params p(1000, 5, 5, 1.0, 1.0, 0.0, 0.5);
    AlmgrenChriss::Trajectory t = AlmgrenChriss::optimalTrajectory(p);
    EXPECT_NEAR(t.kappa, std::log(2.0), 1e-12);
    // x_1 = X sinh(4k)/sinh(5k); sinh(k ln 2) = (2^k - 2^-k)/2.
    EXPECT_NEAR(t.holdings[1], 1000 * (16 - 1.0 / 16) / (32 - 1.0 / 32), 1e-9);
}

TEST(probabilisticSharpeUsesRawKurtosisMinusOne) {
    // Bailey and Lopez de Prado (2012): the variance term is
    // 1 - skew SR + (kurt - 1)/4 SR^2. Normal returns (kurt 3), SR 1, n 5:
    // z = 1 * 2 / sqrt(1.5) = 1.63299, Phi(z) = 0.94876.
    EXPECT_NEAR(sharpe_validation::probabilisticSharpe(1.0, 0.0, 5, 0.0, 3.0), 0.94876, 2e-5);
    // kurt 1 removes the term: z = 2, Phi = 0.97725.
    EXPECT_NEAR(sharpe_validation::probabilisticSharpe(1.0, 0.0, 5, 0.0, 1.0), 0.97725, 2e-5);
}

TEST(pboCountsAMedianOutOfSampleRankAsOverfit) {
    // Bailey et al. (2015): PBO counts logits lambda <= 0. Three variants, four
    // blocks. On every split the in-sample winner lands on the out-of-sample
    // median: rank 2 of 3, w = 1/2, lambda = 0.
    std::vector<std::vector<double>> returns = {{3, 2, 1}, {3, 2, 1}, {3, 2, 1}, {3, 2, 1},
                                                {2, 3, 1}, {2, 3, 1}, {2, 3, 1}, {2, 3, 1}};
    auto mean = [](const std::vector<double>& r) { return math_utils::mean(r); };
    overfit_probability::Result r = overfit_probability::cscv(returns, 4, mean);
    EXPECT_TRUE(r.combinations == 6);
    EXPECT_NEAR(r.logits[0], 0, 1e-12);
    EXPECT_NEAR(r.pbo, 1.0, 1e-12);
}

TEST(squareRootImpactQuadruplesSizeToDoubleCost) {
    MarketImpactModel m = *MarketImpactModel::estimate(base("X", 3), 40, 20);
    double one = m.squareRootImpactBps(1000);
    EXPECT_TRUE(one > 0);
    EXPECT_NEAR(m.squareRootImpactBps(4000) / one, 2.0, 1e-12);
    EXPECT_NEAR(m.squareRootImpactBps(100000) / one, 10.0, 1e-12);
}

TEST(vpinImbalanceIsAbsoluteSoSellingCountsLikeBuying) {
    // Easley, Lopez de Prado and O'Hara (2012): VPIN averages |V_buy - V_sell| / V.
    Vpin sells(100, 4);
    sells.onTrade(100, false);
    EXPECT_NEAR(sells.vpin(), 1.0, 1e-12);
    Vpin mixed(100, 4);
    mixed.onTrade(30, true);
    mixed.onTrade(70, false);
    EXPECT_NEAR(mixed.vpin(), 0.4, 1e-12);
}

TEST(autoHedgerSendsTheExcessOverTheBandNotTheWholePosition) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000);
    gate.priceCollarPct(0.5);
    gate.setReferencePrice(0, 100.0);
    HftOrderGateway gw(gate);
    std::atomic<std::int64_t> qty{0};
    gw.addOrderListener([&qty](std::int64_t, int, Side, std::int64_t q, double, std::int64_t) {
        qty.store(q, std::memory_order_release);
    });
    gw.start();
    gate.onFill(0, Side::BUY, 800);  // 300 over a 500 band
    AutoHedger hedger(gw, 2, 500, 0);
    hedger.onTick(0, 100.0, 1, 1);
    for (int i = 0; i < 2000 && qty.load(std::memory_order_acquire) == 0; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    gw.close();
    EXPECT_TRUE(qty.load() == 300);
}

QTEST_MAIN()
