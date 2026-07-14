// Port of com.quantfinlib.volatility.InformationCriteria (Java): AIC / BIC.
//   AIC = 2k - 2 ln L          (Akaike: prediction-oriented)
//   BIC = k ln n - 2 ln L      (Schwarz: consistency-oriented)
// LOWER is better for both. Both criteria only rank models fitted to the
// SAME data with likelihoods on the same scale.
#pragma once

namespace quantfinlib {

class InformationCriteria {
public:
    InformationCriteria() = delete;

    /**
     * Akaike information criterion 2k - 2 ln L.
     * logLikelihood must be finite; parameters >= 0.
     */
    static double aic(double logLikelihood, int parameters);

    /**
     * Bayesian (Schwarz) information criterion k ln n - 2 ln L.
     * logLikelihood must be finite; parameters >= 0; observations >= 1.
     */
    static double bic(double logLikelihood, int parameters, int observations);
};

}  // namespace quantfinlib
