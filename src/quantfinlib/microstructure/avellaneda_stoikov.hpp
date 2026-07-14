// Port of com.quantfinlib.trading.AvellanedaStoikov (Java package
// `trading`; placed under microstructure/ here per this port's scope).
// Optimal market-making quotes: reservation price shades the mid against
// inventory (r = mid - q*gamma*sigma^2*tau), and the optimal total spread
// balances the volatility cost of holding inventory against the fill-rate
// cost of quoting wide. Units contract: priceVariancePerSecond is the
// variance of the PRICE per second (not the return), inventory is signed
// units of the instrument the mid prices. Pure static-shape math, zero
// allocation.
#pragma once

namespace quantfinlib {

class AvellanedaStoikov {
public:
    /**
     * @param gamma risk aversion, e.g. 0.1 (bigger = shade and widen more)
     * @param kappa fill-intensity decay per unit distance from the touch, e.g. 1.5
     */
    AvellanedaStoikov(double gamma, double kappa);

    /** mid - inventory*gamma*sigma^2*tau; flat inventory or garbage variance returns mid. */
    double reservationPrice(double mid, double inventory, double priceVariancePerSecond,
                             double horizonSeconds) const;

    /** Half of the optimal total spread; never below the liquidity floor (1/gamma)*ln(1+gamma/kappa). */
    double optimalHalfSpread(double priceVariancePerSecond, double horizonSeconds) const;

    double bidQuote(double mid, double inventory, double priceVariancePerSecond,
                     double horizonSeconds) const;

    double askQuote(double mid, double inventory, double priceVariancePerSecond,
                     double horizonSeconds) const;

    double gamma() const { return gamma_; }
    double kappa() const { return kappa_; }

private:
    static double varianceTerm(double priceVariancePerSecond, double horizonSeconds);

    double gamma_;
    double kappa_;
    double liquidityHalfSpread_;
};

}  // namespace quantfinlib
