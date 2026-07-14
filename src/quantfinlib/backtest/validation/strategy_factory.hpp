// Port of com.quantfinlib.backtest.validation.StrategyFactory (Java
// functional interface): builds a strategy instance from one parameter
// combination. The @FunctionalInterface becomes a std::function alias
// returning an owning pointer.
#pragma once

#include <functional>
#include <memory>

#include "quantfinlib/backtest/trading_strategy.hpp"
#include "quantfinlib/backtest/validation/parameter_grid.hpp"

namespace quantfinlib {

using StrategyFactory = std::function<std::unique_ptr<TradingStrategy>(const Params& params)>;

}  // namespace quantfinlib
