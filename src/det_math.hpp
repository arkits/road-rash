// Plan 33 Task A11: transcendental functions that give the same bits on every
// platform. Platform libm (Apple, glibc, MSVC CRT, Bionic, musl/Emscripten)
// differs in the last ulp; these use only IEEE-exact operations (+ - * /,
// frexp, ldexp, floor), so with FP contraction off (Task A3) every compiler
// produces identical results. Accuracy is within ~1 ulp (fdlibm algorithms).
#pragma once

namespace rr {
double det_exp(double x);
double det_log(double x);
double det_log2(double x);
double det_pow10(double x); // 10^x
} // namespace rr
