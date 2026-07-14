// Port of com.quantfinlib.risk.StressTester (Java): stress testing and
// scenario analysis over factor exposures (optional gammas): named
// scenarios through the delta-gamma P&L d'Dx + 0.5 Dx'G Dx (stylized
// blackMonday1987 / lehman2008 / covidMarch2020 templates for a
// [equity, rates(bp/1e4), FX-USD, commodity, vol-points/1e2] ordering),
// sensitivity ladders (delta-only and delta-gamma), and CLOSED-FORM
// reverse stress: the most-probable shock losing exactly L is
// Dx* = -(L/(d'Sd)) * Sd, with Mahalanobis distance L/sqrt(d'Sd).
// Losses are negative P&L. NaN exposures throw at the gate.
#pragma once

#include <vector>

namespace quantfinlib {
namespace stress_tester {

using Matrix = std::vector<std::vector<double>>;

/** Delta-only scenario P&L: d'Dx. */
double scenarioPnl(const std::vector<double>& exposures, const std::vector<double>& shocks);

/** Delta-gamma scenario P&L: d'Dx + 0.5 Dx'G Dx. */
double scenarioPnl(const std::vector<double>& exposures, const Matrix& gamma,
                   const std::vector<double>& shocks);

/**
 * One factor swept over [-range, +range] in `steps` increments,
 * everything else flat — the sensitivity ladder. Returns P&L per rung,
 * ascending shock. DELTA-ONLY.
 */
std::vector<double> sensitivityLadder(const std::vector<double>& exposures, int factor,
                                      double range, int steps);

/**
 * The delta-gamma ladder: the same sweep with the swept factor's own
 * curvature 0.5 * G_ff * shock^2 included (cross-gammas stay out: the
 * other factors are flat by construction).
 */
std::vector<double> sensitivityLadder(const std::vector<double>& exposures,
                                      const Matrix& gamma, int factor,
                                      double range, int steps);

/** The reverse-stress answer: the most-probable shock vector and its distance. */
struct ReverseStress {
    std::vector<double> shocks;
    double mahalanobisSigmas;
};

/**
 * The most-probable factor move (under Gaussian factors with covariance
 * S) that loses exactly targetLoss on a linear book — closed form.
 *
 * @param targetLoss positive loss to reverse-engineer, currency units
 */
ReverseStress reverseStress(const std::vector<double>& exposures, const Matrix& covariance,
                            double targetLoss);

// Historical templates — STARTING POINTS, not certified replays.
// Factor order: [equity, rates(Dr as a fraction, +50bp = +0.005),
// FX (USD strength), commodity, vol (D vol points as a fraction)].

/** 1987-10-19 stylized: equities -20%, flight-to-quality rates, vol explosion. */
std::vector<double> blackMonday1987();

/** 2008-09-15 (Lehman week) stylized: -9% equities, -40bp, USD bid, oil down, vol +16pts. */
std::vector<double> lehman2008();

/** 2020-03-16 stylized: -12% equities, -30bp, USD squeeze, oil collapse, VIX ATH. */
std::vector<double> covidMarch2020();

}  // namespace stress_tester
}  // namespace quantfinlib
