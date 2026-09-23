# Changelog

## [2026-09-23 00:17] - aarch64 CI guard catches more x86-only code

`.github/workflows/aarch64-syntax.yml`: the file pattern now also matches
`mmintrin.h`, any `*intrin.h`, `cpuid.h`, `__builtin_ia32_*`,
`__builtin_cpu_*`, `target(...)` attributes and pragmas, inline asm, `__m64`
and bare `_pext/_pdep/_lzcnt/_tzcnt/_popcnt_u32/u64` calls, and it scans
`test/` too (added to the `paths` filters, with `libgtest-dev`). Files compile
to an object with `-Wall -Werror` instead of `-fsyntax-only`, so the assembler
rejects x86 asm in emitted code and undoing the `is_utf8` template-id fix
fails. Headers under `src/` go through `-isystem src` like the real build, so
warnings the build never shows can't fail the check. Still g++-14: it rejects
every planted case. Not caught: x86 asm inside an inline function that no
checked file instantiates.

## [2026-09-23 00:17] - byte-identity tests assert a pinned hash

`test/basic_tests/UnpartitionedByteIdentityTest.cpp`: both tests only wrote a
dump when their env var was set and asserted nothing. They now always
serialize the merged segment's files in the dump format and assert its length
(1217 and 6405 bytes, the sizes verified identical on arm64 and amd64) and its
XXH3-64 hash, so a codec path that encodes differently on some arch or CPU
tier fails instead of passing silently. The env vars still write the dump.

## [2026-09-23 04:15] - arm64 image builds GCC 16.2.0 from source

`.devcontainer/Dockerfile`:
- arm64 now builds GCC 16.2.0 from the GNU source tarball (SHA256 pinned; its
  signature checks out against gnu-keyring.gpg) and drops the
  ubuntu-toolchain-r PPA. The PPA's trunk snapshot 16.0.1 20260315 reported
  every hour of Pacific/Apia's skipped 2011-12-30 as `unique` from
  `time_zone::get_info(local_time)`; 16.2.0 reports `nonexistent` from the same
  tzdata 2026c, on both arches. That snapshot predates the libstdc++ tzdb fixes
  for PR116110 and PR124513. Both arches now run the same release.
- arm64 writes its ld.so.conf entry as `00-luxir-gcc.conf`. The directory is
  read in name order and `aarch64-linux-gnu.conf` sorts first, so the system's
  older libstdc++ shadowed GCC's for dynamically linked programs (`GLIBCXX_3.4.31
  not found`). amd64 was unaffected because `x86_64-linux-gnu.conf` sorts last.
  Luxir itself links libstdc++ statically.
- The vcpkg downloads cache mount is keyed by `TARGETARCH`. vcpkg unpacks host
  tools there under version-only paths, so building both arches on one builder
  handed the arm64 build an x86 ninja (`rosetta error: failed to open elf`)
  and vice versa. `dependencies` redeclares `ARG TARGETARCH`, since ARGs do not
  carry into a child stage.

Verified on native arm64 (clean build trees, 0 warnings under `-Werror`):
Debug 2095/2095 pass, 0 skipped, including both Apia tests; ASan 2090 pass, 5
expected allocation-counting skips, 0 sanitizer reports. Cross-arch byte
identity (plan item B2), amd64 built emulated in an amd64 image: the
`FullBlockCorpus` dump (6405 bytes) and the `DefaultMergeCorpus` dump (1217
bytes) are byte-identical between arm64 and amd64.

Cold build times on this host (8 CPUs to the Docker VM, both builds sharing
them): arm64 GCC build about 20 min; full arm64 image about 3 h; amd64
`normal-dependencies` under Rosetta about 3 h; amd64 `luxir_test` under
Rosetta 31 min.

## [2026-09-23 00:01] - CI guard against unguarded x86 intrinsics

`.github/workflows/aarch64-syntax.yml`: on a native `ubuntu-24.04-arm` runner,
syntax-check every file under `src/` and `deps/is_utf8/src` that includes an
x86 intrinsics header or names an `__m128/256/512` type. Files are found by
pattern, so new ones are covered. Runs in well under a minute with apt headers
(Boost, fmt, spdlog, xxHash), no vcpkg. Verified in a noble arm64 container
from a fresh clone: passes on this tree, fails on a planted unguarded
`#include <immintrin.h>`.

## [2026-09-22 23:50] - byte-identity dump that reaches the SIMD block codec

`test/basic_tests/UnpartitionedByteIdentityTest.cpp`: the existing
`LUXIR_BYTE_DUMP` corpus is 36 docs, so no posting list fills a 128-value block
and its dump never touched `simdpack`. New `FullBlockCorpus` (1200 docs, dumped
via `LUXIR_BYTE_DUMP_BLOCKS`) fills freq, position and packed doc blocks; under
gdb it makes 116 `simdpack` calls at bit widths 1, 3 and 4 and 6 packed doc-block
encodes, against zero for the old corpus. Separate env var because gtest
shuffles by default and both dumps truncate. Dump writing moved to a shared
helper.

## [2026-09-22 22:45] - aarch64 (arm64 Linux) port

Luxir now builds, tests and runs on arm64 Linux from the same devcontainer
Dockerfile as x86-64. Verified on native arm64 hardware: 2088 of 2090 tests
pass, 0 skipped.

Dependencies and container:
- `deps/triplets/common/linux-aarch64.cmake`: use vcpkg's `arm64` architecture
  token. `aarch64` is what vcpkg derives, not what a triplet sets; with it no
  toolchain branch matched, `CMAKE_SYSTEM_PROCESSOR` stayed empty, and ports
  silently lost their arm64 handling.
- `deps/ports/openblas/portfile.cmake`: select `TARGET` by architecture.
  `NEHALEM` is x86-64 only and has no `kernel/arm64` counterpart.
- `deps/ports/lapack/vcpkg.json`: `supports` was `linux & x64`, which made vcpkg
  refuse lapack, and transitively faiss, on arm64.
- `.devcontainer/Dockerfile`: `TARGETARCH`-gated toolchain and vcpkg triplets.
  arm64 takes GCC 16 from the ubuntu-toolchain-r PPA (Compiler Explorer
  publishes x86-64 only) and symlinks it into the same `/opt/gcc` layout.
  `gpg-agent` is installed explicitly, because `add-apt-repository` needs it and
  `--no-install-recommends` strips it.
- `.devcontainer/check-toolchain.sh`: derive `-march` and the expected dynamic
  loader from `uname -m`.
- `CMakePresets.json`: add `container-arm64-{debug,release,asan}`.

Source:
- `src/luxir/codec/Codec.h`: `__m128i` was used unguarded while its include was
  guarded, so aarch64 could not compile. Include FastPFOR's NEON shim there.
- `src/luxir/codec/Codec.cpp`: add the four SSE->NEON interleave
  correspondences that FastPFOR's shim omits (it covers only what FastPFOR's own
  kernels use). `t4InverseDelta` keeps a single vectorized body on both
  architectures.
- `deps/is_utf8/src/is_utf8.cpp`: `simd16<bool>`'s constructors used a
  template-id as the constructor name, which C++20 forbids and GCC 16 rejects.
  ARM-only code that had never been compiled here.
- `test/CMakeLists.txt`: gate the `-mno-avx*` variant targets on x86-64.

Tests:
- `test/basic_tests/FastPForKernelTest.cpp`: new byte oracle comparing FastPFOR's
  SIMD bit-packing kernels against its pure-scalar ones. Every pre-existing codec
  test round-trips through the same kernel in both directions, so a symmetric
  SIMD bug passed all of them.
- `test/basic_tests/PForTest.cpp`: `pfordT4TransposeExactness` exercises the T4
  lane transpose through the public codec, where the scalar encoder is an
  independent oracle for the vectorized decoder.

The two Pacific/Apia tests
(`FacetTest.calendarDateRangeApiaSkippedDayParityAndWarning`,
`DateFieldTest.skippedQueryGranuleWarnsAndInvalidZoneIsEager`) failed on the
first arm64 image because of the PPA's GCC 16 trunk snapshot; fixed by building
GCC 16.2.0 from source (see the entry above).
