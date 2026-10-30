#pragma once

// Prediction intervals with coverage guarantees (docs/METHODS.md "Conformal intervals"): split
// conformal (Vovk et al. 2005; Lei et al. 2018) for exchangeable data and adaptive conformal
// inference (Gibbs and Candes 2021), which keeps long-run coverage under distribution shift by
// moving its working miscoverage level after each miss or hit.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace hft::strategy::conformal {

// The ceil((n + 1)(1 - alpha))-th smallest score: |y - prediction| is then within it with
// probability at least 1 - alpha for a new exchangeable point. Infinite when n is too small.
inline double split_quantile(std::vector<double> scores, double alpha) {
    const std::size_t n = scores.size();
    const double rank = std::ceil((static_cast<double>(n) + 1) * (1 - alpha));
    if (n == 0 || rank > static_cast<double>(n)) return std::numeric_limits<double>::infinity();
    const auto k = static_cast<std::size_t>(std::max(rank, 1.0)) - 1;
    std::nth_element(scores.begin(), scores.begin() + static_cast<std::ptrdiff_t>(k), scores.end());
    return scores[k];
}

// Online: quantile() is the current half-width; observe(score) scores the last interval and
// updates alpha_t <- alpha_t + gamma (alpha - miss), over a sliding window of recent scores.
// The window lives in a fixed ring and a sorted array reserved up front, so after the first
// `window` scores an update allocates nothing: the expiring score is replaced in place by one shift.
class Adaptive {
   public:
    Adaptive(double alpha = 0.1, double gamma = 0.005, std::size_t window = 500)
        : alpha_(alpha), gamma_(gamma), at_(alpha), ring_(std::max<std::size_t>(window, 1)) {
        sorted_.reserve(ring_.size());
    }

    double quantile() const {
        if (sorted_.empty() || at_ <= 0) return std::numeric_limits<double>::infinity();
        if (at_ >= 1) return 0;
        const double rank = std::ceil((static_cast<double>(sorted_.size()) + 1) * (1 - at_));
        if (rank > static_cast<double>(sorted_.size())) return std::numeric_limits<double>::infinity();
        return sorted_[static_cast<std::size_t>(std::max(rank, 1.0)) - 1];
    }

    // The score of an interval issued at quantile() before this call.
    void observe(double score, double issued_width) {
        const double miss = score > issued_width ? 1 : 0;
        misses_ += miss, ++n_;
        at_ += gamma_ * (alpha_ - miss);
        if (sorted_.size() < ring_.size()) {
            sorted_.insert(std::upper_bound(sorted_.begin(), sorted_.end(), score), score);
        } else {
            // Move the slot of the expiring score to where the new one belongs.
            auto out = std::lower_bound(sorted_.begin(), sorted_.end(), ring_[head_]);
            auto in = std::upper_bound(sorted_.begin(), sorted_.end(), score);
            if (in > out) std::move(out + 1, in, out), *(in - 1) = score;
            else std::move_backward(in, out, out + 1), *in = score;
        }
        ring_[head_] = score;
        head_ = head_ + 1 == ring_.size() ? 0 : head_ + 1;
    }

    double working_alpha() const { return at_; }
    double miss_rate() const { return n_ ? misses_ / static_cast<double>(n_) : NAN; }
    std::size_t observed() const { return n_; }

   private:
    double alpha_, gamma_, at_;
    std::vector<double> ring_, sorted_;
    std::size_t head_ = 0;
    double misses_ = 0;
    std::size_t n_ = 0;
};

}  // namespace hft::strategy::conformal
