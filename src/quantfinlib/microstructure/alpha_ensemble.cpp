#include "quantfinlib/microstructure/alpha_ensemble.hpp"

#include <cmath>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

AlphaEnsemble::AlphaEnsemble(int components, double icAlpha)
        : components_(components), icAlpha_(icAlpha) {
    if (components < 1 || icAlpha <= 0 || icAlpha > 1) {
        throw std::invalid_argument("need components >= 1, icAlpha in (0,1]");
    }
    meanSig_.assign(static_cast<std::size_t>(components), 0.0);
    meanRet_.assign(static_cast<std::size_t>(components), 0.0);
    varSig_.assign(static_cast<std::size_t>(components), 0.0);
    varRet_.assign(static_cast<std::size_t>(components), 0.0);
    covar_.assign(static_cast<std::size_t>(components), 0.0);
    snapshot_.assign(static_cast<std::size_t>(components), 0.0);
}

void AlphaEnsemble::onObservation(const std::vector<double>& values, double realizedReturn) {
    requireLength(values);
    if (hasSnapshot_ && std::isfinite(realizedReturn)) {
        bool scored = false;
        for (int c = 0; c < components_; c++) {
            auto idx = static_cast<std::size_t>(c);
            double s = snapshot_[idx];
            if (!std::isfinite(s)) {
                continue;
            }
            meanSig_[idx] += icAlpha_ * (s - meanSig_[idx]);
            meanRet_[idx] += icAlpha_ * (realizedReturn - meanRet_[idx]);
            double ds = s - meanSig_[idx];
            double dr = realizedReturn - meanRet_[idx];
            varSig_[idx] += icAlpha_ * (ds * ds - varSig_[idx]);
            varRet_[idx] += icAlpha_ * (dr * dr - varRet_[idx]);
            covar_[idx] += icAlpha_ * (ds * dr - covar_[idx]);
            scored = true;
        }
        if (scored) {
            samples_++;
        }
    }
    for (int c = 0; c < components_; c++) {
        snapshot_[static_cast<std::size_t>(c)] = values[static_cast<std::size_t>(c)];
    }
    hasSnapshot_ = true;
}

double AlphaEnsemble::combined(const std::vector<double>& values) const {
    requireLength(values);
    if (static_cast<double>(samples_) * icAlpha_ < 1) {
        return 0;
    }
    double weighted = 0;
    for (int c = 0; c < components_; c++) {
        double ic = componentIC(c);
        if (ic > 0 && std::isfinite(values[static_cast<std::size_t>(c)])) {
            weighted += ic * values[static_cast<std::size_t>(c)];
        }
    }
    return math_utils::clamp(weighted, -1, 1);
}

double AlphaEnsemble::componentIC(int c) const {
    auto idx = static_cast<std::size_t>(c);
    double denom = std::sqrt(varSig_[idx] * varRet_[idx]);
    return denom > 0 ? covar_[idx] / denom : 0;
}

void AlphaEnsemble::requireLength(const std::vector<double>& a) const {
    if (static_cast<int>(a.size()) < components_) {
        throw std::invalid_argument("array too short for the ensemble");
    }
}

}  // namespace quantfinlib
