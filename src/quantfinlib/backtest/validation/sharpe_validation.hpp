// Port of com.quantfinlib.backtest.validation.SharpeValidation: Sharpe
// significance tests (Bailey & Lopez de Prado) — probabilistic Sharpe
// ratio, expected max Sharpe of N zero-skill trials, the deflated Sharpe
// (multiple-testing haircut), and the closed-form minimum track record
// length. All Sharpe inputs are PER-PERIOD (not annualized).
// minTrackRecordLength returns +infinity when the observed Sharpe does
// not exceed the benchmark. Gates map to std::invalid_argument.
#pragma once

#include <vector>

namespace quantfinlib {
namespace sharpe_validation {

/** Probability the true Sharpe exceeds benchmarkSharpe, in [0, 1]. */
double probabilisticSharpe(double observedSharpe, double benchmarkSharpe,
                           int nObs, double skewness, double kurtosis);

/**
 * Expected maximum Sharpe among `trials` independent zero-skill strategies
 * whose Sharpe estimates have the given cross-trial variance.
 */
double expectedMaxSharpe(int trials, double varianceOfTrialSharpes);

/**
 * Deflated Sharpe: PSR of the winner against the expected-max benchmark
 * implied by all trials. Near 1 = the edge survives the haircut.
 *
 * @param trialSharpes per-period Sharpe of every trial (incl. the winner)
 */
double deflatedSharpe(double observedSharpe, const std::vector<double>& trialSharpes,
                      int nObs, double skewness, double kurtosis);

/**
 * Minimum track record length: periods of THIS performance needed before
 * probabilisticSharpe clears `confidence` that the true Sharpe exceeds
 * the benchmark. n* = 1 + (1 - g3*SR + (g4-1)/4*SR^2) * (z / (SR - SR*))^2.
 */
double minTrackRecordLength(double observedSharpe, double benchmarkSharpe,
                            double skewness, double kurtosis, double confidence);

}  // namespace sharpe_validation
}  // namespace quantfinlib
