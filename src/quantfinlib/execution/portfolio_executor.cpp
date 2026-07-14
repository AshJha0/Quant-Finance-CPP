#include "quantfinlib/execution/portfolio_executor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

PortfolioExecutor::Config::Config(double maxNetNotional_, double maxIntervalNotional_)
        : maxNetNotional(maxNetNotional_), maxIntervalNotional(maxIntervalNotional_) {
    if (!(maxNetNotional > 0) || !(maxIntervalNotional > 0)) {
        throw std::invalid_argument("need maxNetNotional > 0 and maxIntervalNotional > 0 (+Inf disables)");
    }
}

PortfolioExecutor::Config PortfolioExecutor::Config::unconstrained() {
    return Config(std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity());
}

PortfolioExecutor::PortfolioExecutor(int maxSymbols, const Config& config)
        : config_(config), maxSymbols_(maxSymbols) {
    if (maxSymbols < 1) {
        throw std::invalid_argument("need maxSymbols >= 1");
    }
    children_.reserve(static_cast<std::size_t>(maxSymbols));
    lastMid_.assign(static_cast<std::size_t>(maxSymbols), 0.0);
    dueNotional_.assign(static_cast<std::size_t>(maxSymbols), 0.0);
    signedRemaining_.assign(static_cast<std::size_t>(maxSymbols), 0.0);
    riskFactor_.assign(static_cast<std::size_t>(maxSymbols), 0.0);
}

void PortfolioExecutor::useRiskModel(EwmaCovariance& model) {
    if (model.symbols() != maxSymbols_) {
        throw std::invalid_argument("risk model covers " + std::to_string(model.symbols())
                                     + " symbols; this portfolio was sized for " + std::to_string(maxSymbols_));
    }
    riskModel_ = &model;
}

int PortfolioExecutor::add(BenchmarkExecutor child) {
    if (count_ == maxSymbols_) {
        throw std::logic_error("portfolio is full (" + std::to_string(maxSymbols_) + " symbols)");
    }
    children_.push_back(child);
    return count_++;
}

void PortfolioExecutor::decide(double scheduleFraction, const std::vector<BenchmarkExecutor::MarketState>& states,
                               std::vector<std::int64_t>& dueOut) {
    if (static_cast<int>(states.size()) < count_ || static_cast<int>(dueOut.size()) < count_) {
        throw std::invalid_argument("states/dueOut must cover all " + std::to_string(count_) + " symbols");
    }
    // 1. Each child's own decision, and its notional at the best-known price.
    for (int i = 0; i < count_; i++) {
        dueOut[static_cast<std::size_t>(i)] =
                children_[static_cast<std::size_t>(i)].dueQuantity(scheduleFraction, states[static_cast<std::size_t>(i)]);
        double mid = states[static_cast<std::size_t>(i)].mid;
        if (mid > 0 && mid < std::numeric_limits<double>::infinity()) {
            lastMid_[static_cast<std::size_t>(i)] = mid;  // NaN fails the > 0 test
        }
    }

    // 2. Leg balance: throttle the leg that would push |net| past the band.
    applyLegBand(dueOut);

    // 3. Capacity: when total demand exceeds the interval budget, allocate
    //    it risk-weighted and cut each symbol to its share.
    if (config_.maxIntervalNotional < std::numeric_limits<double>::infinity()) {
        fillRiskFactors(states);
        double total = 0;
        double sumWeight = 0;
        for (int i = 0; i < count_; i++) {
            auto idx = static_cast<std::size_t>(i);
            double notional = static_cast<double>(dueOut[idx]) * lastMid_[idx];
            dueNotional_[idx] = notional;
            total += notional;
            sumWeight += riskFactor_[idx] * notional;
        }
        if (total > config_.maxIntervalNotional && sumWeight > 0) {
            for (int i = 0; i < count_; i++) {
                auto idx = static_cast<std::size_t>(i);
                if (dueNotional_[idx] <= 0) {
                    continue;
                }
                double weight = riskFactor_[idx] * dueNotional_[idx];
                double allocation = config_.maxIntervalNotional * weight / sumWeight;
                if (dueNotional_[idx] > allocation) {
                    dueOut[idx] = static_cast<std::int64_t>(
                            std::floor(static_cast<double>(dueOut[idx]) * allocation / dueNotional_[idx]));
                }
            }
            // 4. The risk weights cut the two legs asymmetrically, so the
            //    capacity pass can push |net| back over the band the
            //    first pass enforced. Re-apply it -- the band only
            //    reduces dues, so it can never re-violate the budget.
            applyLegBand(dueOut);
        }
    }
}

void PortfolioExecutor::fillRiskFactors(const std::vector<BenchmarkExecutor::MarketState>& states) {
    bool modeled = false;
    if (riskModel_ != nullptr) {
        // Tail entries stay zero for good: nothing else writes
        // signedRemaining and count never shrinks, so unadded handles
        // carry no risk without re-zeroing.
        for (int i = 0; i < count_; i++) {
            auto idx = static_cast<std::size_t>(i);
            double sgn = children_[idx].side() == Side::BUY ? 1 : -1;
            signedRemaining_[idx] = sgn * static_cast<double>(children_[idx].remaining()) * lastMid_[idx];
        }
        if (riskModel_->marginalContribution(signedRemaining_, riskFactor_) > 0) {
            for (int i = 0; i < count_; i++) {
                auto idx = static_cast<std::size_t>(i);
                riskFactor_[idx] = 1 + math_utils::clamp(riskFactor_[idx], 0, 1);
            }
            modeled = true;
        }
    }
    if (!modeled) {
        for (int i = 0; i < count_; i++) {
            auto idx = static_cast<std::size_t>(i);
            double vol = states[idx].volatility;
            riskFactor_[idx] = 1 + (vol > 0 ? vol : 0);  // NaN -> 0
        }
    }
}

void PortfolioExecutor::applyLegBand(std::vector<std::int64_t>& dueOut) {
    if (config_.maxNetNotional == std::numeric_limits<double>::infinity()) {
        return;
    }
    double buyDue = 0;
    double sellDue = 0;
    for (int i = 0; i < count_; i++) {
        auto idx = static_cast<std::size_t>(i);
        double notional = static_cast<double>(dueOut[idx]) * lastMid_[idx];
        if (children_[idx].side() == Side::BUY) {
            buyDue += notional;
        } else {
            sellDue += notional;
        }
    }
    double net = buyFilledNotional_ - sellFilledNotional_;
    double projected = net + buyDue - sellDue;
    if (projected > config_.maxNetNotional && buyDue > 0) {
        double allowed = std::max(0.0, config_.maxNetNotional - net + sellDue);
        scaleSide(Side::BUY, allowed / buyDue, dueOut);
    } else if (projected < -config_.maxNetNotional && sellDue > 0) {
        double allowed = std::max(0.0, config_.maxNetNotional + net + buyDue);
        scaleSide(Side::SELL, allowed / sellDue, dueOut);
    }
}

void PortfolioExecutor::scaleSide(Side side, double scale, std::vector<std::int64_t>& dueOut) {
    double s = math_utils::clamp(scale, 0, 1);
    for (int i = 0; i < count_; i++) {
        auto idx = static_cast<std::size_t>(i);
        if (children_[idx].side() == side && dueOut[idx] > 0 && lastMid_[idx] > 0) {
            dueOut[idx] = static_cast<std::int64_t>(std::floor(static_cast<double>(dueOut[idx]) * s));
        }
    }
}

void PortfolioExecutor::onFill(int handle, std::int64_t qty, double price) {
    auto idx = static_cast<std::size_t>(handle);
    children_[idx].onFill(qty);
    if (qty > 0 && price > 0 && price < std::numeric_limits<double>::infinity()) {
        lastMid_[idx] = price;
        double notional = static_cast<double>(qty) * price;
        if (children_[idx].side() == Side::BUY) {
            buyFilledNotional_ += notional;
        } else {
            sellFilledNotional_ += notional;
        }
    }
}

bool PortfolioExecutor::done() const {
    for (int i = 0; i < count_; i++) {
        if (!children_[static_cast<std::size_t>(i)].done()) {
            return false;
        }
    }
    return true;
}

}  // namespace quantfinlib
