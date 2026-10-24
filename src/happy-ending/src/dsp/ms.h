/**
 * Fairy Tapes · Happy Ending — mid/side encode and decode.
 *
 * M = (L + R) / 2, S = (L - R) / 2; L = M + S, R = M - S. A mono signal (L == R) has S == 0
 * exactly. Templates so that the double-precision filter path and float code share one definition.
 */

#pragma once

namespace FairyTapes::HappyEnding::DSP {

template <typename T>
inline void msEncode(T left, T right, T& mid, T& side) {
  mid = (left + right) * T(0.5);
  side = (left - right) * T(0.5);
}

template <typename T>
inline void msDecode(T mid, T side, T& left, T& right) {
  left = mid + side;
  right = mid - side;
}

} // namespace FairyTapes::HappyEnding::DSP
