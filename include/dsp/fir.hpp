#pragma once
#include "dsp/functions.hpp"

#if defined(__arm__) || defined(__aarch64__)
	#include <arm_neon.h>
#endif

namespace rack {

/** Perform a direct convolution
x[-len + 1] to x[0] must be defined
*/
inline float convolve(const float *x, const float *kernel, int len) {
#if defined(__arm__) || defined(__aarch64__)
	int steps = len >> 2;
	float32x4_t res = vmovq_n_f32(0);
	for (int j = 0; j < steps; j++)
	{
		res = vmlaq_f32(res, vld1q_f32(x), vld1q_f32(kernel));
		x -= 4;
		kernel += 4;
	}
	return res[0]+res[1]+res[2]+res[3];
#else
	float y = 0.0;
	for (int i = 0; i < len; i++) {
		y += x[-i] * kernel[i];
	}

	return y;
#endif
}

inline void blackmanHarrisWindow(float *x, int n) {
	const float a0 = 0.35875;
	const float a1 = 0.48829;
	const float a2 = 0.14128;
	const float a3 = 0.01168;
	for (int i = 0; i < n; i++) {
		x[i] *= a0
			- a1 * cosf(2 * M_PI * i / (n - 1))
			+ a2 * cosf(4 * M_PI * i / (n - 1))
			- a3 * cosf(6 * M_PI * i / (n - 1));
	}
}

inline void boxcarFIR(float *x, int n, float cutoff) {
	for (int i = 0; i < n; i++) {
		float t = (float)i / (n - 1) * 2.0 - 1.0;
		x[i] = sinc(t * n * cutoff);
	}
}

} // namespace rack
