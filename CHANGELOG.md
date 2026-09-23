# Changelog

## [2026-09-23] - aarch64 (arm64 Linux) port

Luxir now builds, tests and runs on arm64 Linux from the same devcontainer
Dockerfile as x86-64. Verified on native arm64 hardware.

- `.devcontainer/Dockerfile` gates the toolchain and vcpkg triplet on
  `TARGETARCH`; arm64 builds GCC 16.2.0 from the pinned GNU tarball (the PPA
  trunk snapshot mis-handled Pacific/Apia's skipped day, failing two date
  tests). The vcpkg downloads cache is keyed by arch.
- vcpkg triplets and overlay ports: correct arm64 architecture and target
  selection (`CMAKE_SYSTEM_PROCESSOR`, openblas `TARGET`, lapack `supports`).
- x86 intrinsics headers are guarded; FastPFOR's NEON shim provides the SSE
  types, with the missing interleave mappings added so transposes stay one code
  path on both arches. `deps/is_utf8` gets a C++20 template-id fix (ARM-only
  code path). The `-mno-avx*` test variants are x86-64 only.
- Tests and CI: `FastPForKernelTest` byte-compares the SIMD bit-packing kernels
  against scalar ones; `UnpartitionedByteIdentityTest` now asserts pinned
  length and hash and covers SIMD-filled blocks; a new arm64 workflow
  syntax-checks x86 intrinsics on a native runner.
- Verified on native arm64, 0 warnings under `-Werror`: Debug 2095/2095, ASan
  2090 pass with 5 expected skips and 0 sanitizer reports; index dumps
  byte-identical between arm64 and amd64.

## [2026-09-23] - PFor exception writer no longer leaks stack bytes to disk

The final partial group was packed unmasked as a full 32 values, so unused
stack slots landed in the last residual word and on-disk bytes varied with
build mode and earlier encodes. Readers ignore those bits, so indexes read
fine. The slots are now zeroed before packing; the byte-identity test's pinned
hash updates accordingly.