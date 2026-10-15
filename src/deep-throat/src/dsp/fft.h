// Fairy Tapes · Deep Throat — radix-2 complex FFT (in place) and a power spectrum helper.
// Header-only, no VST3 SDK dependency.
#pragma once

#include <cmath>
#include <vector>

namespace FairyTapes::DeepThroat::DSP {

constexpr bool isPowerOfTwo(int n) { return n > 0 && (n & (n - 1)) == 0; }

constexpr int nextPow2(int n) {
  int p = 1;
  while (p < n) p <<= 1;
  return p;
}

namespace detail {

inline void bitReverse(float* re, float* im, int n) {
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      const float tr = re[i]; re[i] = re[j]; re[j] = tr;
      const float ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }
}

// One butterfly stage set; tw(k, len) returns the twiddle exp(-2*pi*i*k/len) as (cos, -sin).
template <typename Twiddle>
inline void butterflies(float* re, float* im, int n, Twiddle tw) {
  for (int len = 2; len <= n; len <<= 1) {
    const int half = len >> 1;
    for (int k = 0; k < half; ++k) {
      float wr, wi;
      tw(k, len, wr, wi);
      for (int i = k; i < n; i += len) {
        const int j = i + half;
        const float xr = re[j] * wr - im[j] * wi;
        const float xi = re[j] * wi + im[j] * wr;
        re[j] = re[i] - xr;
        im[j] = im[i] - xi;
        re[i] += xr;
        im[i] += xi;
      }
    }
  }
}

} // namespace detail

// Forward complex FFT of size n (power of two), in place, with precomputed twiddles.
// prepare() allocates; forward() never does.
class Fft {
public:
  void prepare(int n) {
    n_ = isPowerOfTwo(n) ? n : nextPow2(n);
    cos_.assign(n_ / 2 + 1, 0.0f);
    sin_.assign(n_ / 2 + 1, 0.0f);
    for (int k = 0; k <= n_ / 2; ++k) {
      const double a = -2.0 * 3.14159265358979323846 * k / n_;
      cos_[k] = static_cast<float>(std::cos(a));
      sin_[k] = static_cast<float>(std::sin(a));
    }
  }
  int size() const { return n_; }
  void forward(float* re, float* im) const {
    detail::bitReverse(re, im, n_);
    const float* c = cos_.data();
    const float* s = sin_.data();
    const int n = n_;
    detail::butterflies(re, im, n_, [c, s, n](int k, int len, float& wr, float& wi) {
      const int idx = k * (n / len);
      wr = c[idx];
      wi = s[idx];
    });
  }

private:
  int n_ = 0;
  std::vector<float> cos_, sin_;
};

// |X_k|^2 for k = 0..n/2 of the real input in[0..n) (no window, no scaling).
// n must be a power of two, at most kMaxSize (otherwise outPower is zeroed). Never allocates:
// the work buffers live on the stack and twiddles come from a per-stage recurrence in double.
inline void fftPowerSpectrum(const float* in, int n, float* outPower) {
  constexpr int kMaxSize = 8192;
  if (!isPowerOfTwo(n) || n > kMaxSize) {
    if (n >= 2) for (int k = 0; k <= n / 2; ++k) outPower[k] = 0.0f;
    return;
  }
  float re[kMaxSize];
  float im[kMaxSize];
  for (int i = 0; i < n; ++i) { re[i] = in[i]; im[i] = 0.0f; }
  detail::bitReverse(re, im, n);
  // Per stage: w_k = exp(-2*pi*i*k/len), generated incrementally (k is visited in order).
  double cr = 1.0, ci = 0.0, stepR = 1.0, stepI = 0.0;
  int curLen = 0;
  detail::butterflies(re, im, n, [&](int k, int len, float& wr, float& wi) {
    if (len != curLen) {
      curLen = len;
      const double a = -2.0 * 3.14159265358979323846 / len;
      stepR = std::cos(a);
      stepI = std::sin(a);
      cr = 1.0;
      ci = 0.0;
    } else if (k > 0) {
      const double t = cr * stepR - ci * stepI;
      ci = cr * stepI + ci * stepR;
      cr = t;
    }
    wr = static_cast<float>(cr);
    wi = static_cast<float>(ci);
  });
  for (int k = 0; k <= n / 2; ++k) outPower[k] = re[k] * re[k] + im[k] * im[k];
}

} // namespace FairyTapes::DeepThroat::DSP
