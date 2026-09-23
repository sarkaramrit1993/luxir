// Copyright 2020-2026 Yonik Seeley and Luxir contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "luxir/codec/Codec.h"  // __m128i for the current target
#include "FastPFOR/headers/bitpackinghelpers.h"
#include "FastPFOR/headers/simdbitpacking.h"
#include "FastPFOR/headers/usimdbitpacking.h"

// The codec round-trip tests encode and decode with the same SIMD kernel, so a
// symmetric bug in that kernel passes them all. bitpacking.cpp contains no SIMD,
// so its scalar kernels are a genuine cross-implementation reference. The SIMD
// kernels pack four interleaved lanes of 32 values: output word w*4 + lane
// equals scalar word w of that lane's deinterleaved sequence.
namespace {

// `masking` picks the scalar counterpart: fastpack masks each value to `bits`,
// fastpackwithoutmask assumes the caller already did. The SIMD kernels split the
// same way (simdpack masks, usimdpackwithoutmask does not), and on aarch64 the
// mask is a real NEON operation, so both need covering.
std::vector<uint32_t> scalarPackInterleaved(const uint32_t* in, uint32_t bits,
                                            bool masking) {
  std::vector<uint32_t> out(4 * bits, 0u);
  for (uint32_t lane = 0; lane < 4; ++lane) {
    uint32_t laneIn[32], laneOut[32];
    for (uint32_t j = 0; j < 32; ++j) laneIn[j] = in[j * 4 + lane];
    std::memset(laneOut, 0, sizeof laneOut);
    if (masking) {
      FastPForLib::fastpack(laneIn, laneOut, bits);
    } else {
      FastPForLib::fastpackwithoutmask(laneIn, laneOut, bits);
    }
    for (uint32_t w = 0; w < bits; ++w) out[w * 4 + lane] = laneOut[w];
  }
  return out;
}

std::vector<uint32_t> maskedBlock(std::mt19937& rng, uint32_t bits) {
  const uint32_t mask = (bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
  std::vector<uint32_t> in(128);
  for (uint32_t i = 0; i < 128; ++i) in[i] = rng() & mask;
  return in;
}

std::vector<uint32_t> unmaskedBlock(std::mt19937& rng) {
  std::vector<uint32_t> in(128);
  for (uint32_t i = 0; i < 128; ++i) in[i] = rng();
  return in;
}

}  // namespace

TEST(FastPForKernelTest, usimdPackMatchesScalarBytes) {
  std::mt19937 rng(4242);
  for (uint32_t bits = 1; bits <= 32; ++bits) {
    const std::vector<uint32_t> in = maskedBlock(rng, bits);
    std::vector<uint32_t> simd(4 * bits + 8, 0u);
    FastPForLib::usimdpackwithoutmask(in.data(), (__m128i*) simd.data(), bits);
    const std::vector<uint32_t> scalar =
        scalarPackInterleaved(in.data(), bits, /*masking=*/false);
    EXPECT_EQ(0, std::memcmp(simd.data(), scalar.data(),
                             4 * bits * sizeof(uint32_t)))
        << "usimdpackwithoutmask diverges from scalar at bits=" << bits;
  }
}

TEST(FastPForKernelTest, simdPackMasksLikeScalarFastpack) {
  // simdpack masks each value to `bits` before packing, and encodeBlockPFor
  // relies on that for exception values. Pre-masked input never exercises the
  // mask, so feed unmasked input and compare against the masking scalar.
  std::mt19937 rng(4243);
  for (uint32_t bits = 1; bits <= 31; ++bits) {
    const std::vector<uint32_t> in = unmaskedBlock(rng);
    std::vector<uint32_t> simd(4 * bits + 8, 0u);
    FastPForLib::simdpack(in.data(), (__m128i*) simd.data(), bits);
    const std::vector<uint32_t> scalar =
        scalarPackInterleaved(in.data(), bits, /*masking=*/true);
    EXPECT_EQ(0, std::memcmp(simd.data(), scalar.data(),
                             4 * bits * sizeof(uint32_t)))
        << "simdpack's mask diverges from scalar at bits=" << bits;
  }
}

TEST(FastPForKernelTest, usimdUnpackRecoversScalarPackedBytes) {
  // Decodes a buffer produced entirely by scalar code, so a SIMD unpack bug
  // cannot hide behind a matching SIMD pack bug.
  std::mt19937 rng(4244);
  for (uint32_t bits = 1; bits <= 32; ++bits) {
    const std::vector<uint32_t> in = maskedBlock(rng, bits);
    std::vector<uint32_t> scalar =
        scalarPackInterleaved(in.data(), bits, /*masking=*/false);
    scalar.resize(4 * bits + 8, 0u);
    std::vector<uint32_t> back(128, 0u);
    FastPForLib::usimdunpack((const __m128i*) scalar.data(), back.data(), bits);
    EXPECT_EQ(in, back)
        << "usimdunpack diverges from scalar layout at bits=" << bits;
  }
}

TEST(FastPForKernelTest, simdUnpackRecoversScalarPackedBytes) {
  // simdunpack is a separate entry point from usimdunpack and is on the
  // production decode path, so cover it explicitly.
  std::mt19937 rng(4245);
  for (uint32_t bits = 1; bits <= 32; ++bits) {
    const std::vector<uint32_t> in = maskedBlock(rng, bits);
    std::vector<uint32_t> scalar =
        scalarPackInterleaved(in.data(), bits, /*masking=*/false);
    scalar.resize(4 * bits + 8, 0u);
    std::vector<uint32_t> back(128, 0u);
    FastPForLib::simdunpack((const __m128i*) scalar.data(), back.data(), bits);
    EXPECT_EQ(in, back)
        << "simdunpack diverges from scalar layout at bits=" << bits;
  }
}
