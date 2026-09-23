# aarch64 port: remaining work — session handoff prompt

> Paste everything below the line into a fresh Claude Code session in
> `/Users/amritsarkar/gitspace/luxir`. It is self-contained.

---

You are continuing an aarch64 (arm64 Linux) port of Luxir, a C++26 search engine.
The port is functionally complete and committed. Two items remain, plus cleanup.
Read this whole prompt before running anything.

## Ground rules

- Verify claims by running things, not by reasoning about them. This session's
  predecessor was wrong three times about what would break, and right only
  because it ran the code. Where this document states a fact, it was measured.
- Never push and never open a PR. The human owns both.
- `-Werror` is live in Debug builds (`CMakeLists.txt:166`). New warnings are
  build failures, not notes.
- Do not "fix" the two known-failing tests by weakening assertions until you
  have established which layer is actually wrong. See item A.
- Scope lock: do not refactor, reformat, or improve code you were not asked to
  touch.

## Current state

Branch `add-aarch64-port`, 11 commits ahead of `main`, nothing pushed.

```
deac31ea docs: record the aarch64 port and arm64 container usage
969c2aae test: byte oracle for FastPFOR's SIMD bit-packing kernels
4d4473ea codec: build and decode correctly on aarch64
eca29349 is_utf8: fix template-id constructors in the NEON path
7e0ad9ca test: gate the -mno-avx* variant targets on x86-64
fabd62e5 presets: add the arm64 container preset family
0609374a devcontainer: select vcpkg triplets by TARGETARCH
0b2ee323 devcontainer: TARGETARCH-gated toolchain stage for arm64
d4d1ac92 lapack: allow the overlay port on non-x64 Linux
c52b6223 openblas: select TARGET by architecture
cf3c9da9 triplets: use vcpkg's arm64 architecture token, not aarch64
```

Verified on native arm64 hardware:

| Gate | Result |
|---|---|
| arm64 dev image | builds; all 83 vcpkg packages incl. gRPC/FAISS, plus the ASan tree |
| Debug suite | 2088 / 2090 pass, **0 skipped** |
| ASan suite | 2087 pass, 5 expected skips, **0 sanitizer reports** |
| `-Werror` | clean on both builds |

The 5 ASan skips are allocation-counting tests that `test/test/LuxirTest.h:15-21`
compiles out under `__SANITIZE_ADDRESS__`: `KStemTest.sharedDictionaryAndNoTokenAllocations`,
`AnalysisTest.wordSegmentationIsAllocationFree`, `AnalysisTest.standardAsciiPathAllocationFree`,
`AnalysisTest.allocCounterObservesFoldAllocation`, and one in `MemPoolTest`. Expected.

## Environment (important — changed during the previous session)

Docker Desktop allocations were raised because the port could not build at the
old limits. Original settings backed up to `/tmp/docker-settings-backup.json`.

| | was | now |
|---|---|---|
| `DiskSizeMiB` | 40960 | 131072 |
| `MemoryMiB` | 8192 | 16384 |
| `SwapMiB` | 1024 | 2048 |

Settings file: `~/Library/Group Containers/group.com.docker/settings-store.json`.
Changing it requires quitting and restarting Docker Desktop. Growing the disk is
non-destructive; all images survived.

Build commands (host has native arm64 Docker):

```bash
./tools/dev-container build-image                 # rebuild image if needed
./tools/dev-container cmake --preset container-arm64-debug
./tools/dev-container cmake --build --preset container-arm64-debug
./tools/dev-container ./build/container-arm64-debug/bin/luxir_test \
  --gtest_brief=1 --gtest_print_time=0
```

Presets: `container-arm64-{debug,release,asan}`. Image: `luxir-dev:jammy-gcc16`.
Use `--build-arg BUILD_JOBS=N` if memory pressure returns.

---

# Item A: the two failing tests (Pacific/Apia skipped day)

**Status: root cause narrowed, not concluded. Do not guess the fix.**

Failing:
- `FacetTest.calendarDateRangeApiaSkippedDayParityAndWarning` (`test/basic_tests/FacetTest.cpp:1488,1500,1501`)
- `DateFieldTest.skippedQueryGranuleWarnsAndInvalidZoneIsEager` (`test/basic_tests/DateFieldTest.cpp:734`)

Both assert that Samoa's 2011-12-30 does not exist (Samoa jumped the
International Date Line, going from Dec 29 straight to Dec 31). The tests expect
`granuleSkipped == true`, a `calendar_bucket_skipped` warning, and 3 day-buckets;
the code produces `false`, no warning, and 4 buckets.

## What has been established

**tzdata 2026c is correct.** `zdump -v Pacific/Apia` inside the container:

```
Pacific/Apia  Fri Dec 30 09:59:59 2011 UT = Thu Dec 29 23:59:59 2011 -10 isdst=1
Pacific/Apia  Fri Dec 30 10:00:00 2011 UT = Sat Dec 31 00:00:00 2011 +14 isdst=1
```

Local time jumps Dec 29 → Dec 31. The zone line in `/usr/share/zoneinfo/tzdata.zi`
is `-11 WS -11/-10 2011 D 29 24`. The skipped day is in the data.

**libstdc++'s `std::chrono` tzdb disagrees with its own tzdata.** A probe built
with the container's GCC 16 reports every hour of that local day as `unique`:

```
tzdb version: 2026c
  2011-12-30 00:00 -> unique      <-- should be nonexistent
  2011-12-30 01:00 -> unique
  2011-12-30 12:00 -> unique
  2011-12-30 23:00 -> unique
```

So the failure is in `std::chrono::time_zone::get_info(local_time)`, not in
luxir's date code, not in tzdata, and not in anything architecture-specific.

## The open question that decides the fix

The two container images do **not** run the same GCC build:

- amd64: Compiler Explorer **GCC 16.2.0 release** tarball
- arm64: `ubuntu-toolchain-r/test` PPA **GCC 16 snapshot `16-20260315-1ubuntu1~22~ppa1`** (trunk `r16-8100-g3aca3bae8ee`)

This asymmetry is deliberate and documented (Compiler Explorer publishes x86-64
only), but it means **the two images ship different libstdc++ builds**. If
16.2.0's tzdb handles the Apia transition correctly and the trunk snapshot
regressed it, then these tests fail only on arm64 in practice, and the earlier
claim that "x86-64 fails identically" is wrong.

**Resolve this first. Everything else about item A depends on it.**

- [x] **A1. Determine whether GCC 16.2.0's libstdc++ has the same behavior.**
  **Done 2026-09-22:** 16.2.0 (amd64) reports `NONEXISTENT` for all four
  hours; the arm64 snapshot 16.0.1 20260315 reports `unique`. Same tzdata
  2026c in both. The snapshot regressed it.

Cheapest path that avoids a full amd64 dependency build: build only the
`toolchain` stage for amd64 (it has no vcpkg work) and run the probe there.

```bash
docker build --platform linux/amd64 -f .devcontainer/Dockerfile \
  --target toolchain -t luxir-toolchain:amd64 . > /tmp/tc-amd64.log 2>&1
echo "exit=$?"
```

Then run this probe in **both** images and compare:

```bash
cat > /tmp/tzprobe.cpp <<'EOF'
#include <chrono>
#include <cstdio>
using namespace std::chrono;
static const char* k(int r){ return r==local_info::unique?"unique"
  : r==local_info::nonexistent?"NONEXISTENT"
  : r==local_info::ambiguous?"ambiguous":"?"; }
int main(){
  auto* z = locate_zone("Pacific/Apia");
  printf("gcc %d.%d.%d  tzdb %s\n", __GNUC__, __GNUC_MINOR__,
         __GNUC_PATCHLEVEL__, get_tzdb().version.c_str());
  for (int h : {0,1,12,23})
    printf("  2011-12-30 %02d:00 -> %s\n", h,
           k(z->get_info(local_days{year{2011}/December/30}+hours{h}).result));
  return 0;
}
EOF
for img in luxir-toolchain:amd64 luxir-dev:jammy-gcc16; do
  echo "=== $img ==="
  docker run --rm -v /tmp/tzprobe.cpp:/tmp/p.cpp "$img" \
    sh -c 'g++ -std=c++26 -O1 /tmp/p.cpp -o /tmp/p && /tmp/p'
done
```

Note the amd64 run is emulated and slow but this is one small compile.

- [x] **A2. Branch on the result.**
  **Done 2026-09-23:** the PPA had nothing newer and Compiler Explorer has no
  arm64 tarball, so with sign-off the arm64 stage now builds GCC 16.2.0 from
  the GNU source tarball. Both Apia tests pass; full Debug and ASan suites
  green. See CHANGELOG.

**If both report `unique`** — the behavior is common to GCC 16 and predates the
port. It is a pre-existing bug the port merely surfaced. Correct action: leave
the tests failing, open an issue describing the libstdc++ behavior, and add a
note. Do **not** weaken the assertions; they encode correct real-world history.
Record the finding in `CHANGELOG.md` under the existing "Known unrelated
failure" paragraph, correcting its explanation (it currently blames tzdata,
which this investigation disproved).

**If 16.2.0 reports `NONEXISTENT` and the snapshot reports `unique`** — the
trunk snapshot regressed it, and the port's GCC asymmetry is the proximate
cause. Options, in preference order:

1. Pin a different arm64 GCC 16 that behaves like 16.2.0. Check what else
   `ubuntu-toolchain-r/test` publishes for jammy/arm64:
   ```bash
   curl -s "https://api.launchpad.net/1.0/~ubuntu-toolchain-r/+archive/ubuntu/test?ws.op=getPublishedBinaries&binary_name=g%2B%2B-16&exact_match=true&status=Published" \
     | python3 -c "import json,sys; d=json.load(sys.stdin); [print(e['binary_package_version'], e['distro_arch_series_link'].rsplit('/',1)[-1]) for e in d['entries']]"
   ```
   Change `GCC_ARM64_APT_VERSION` in `.devcontainer/Dockerfile` and rebuild the
   toolchain stage only (fast) to re-probe before committing to a full rebuild.
2. If no suitable version exists, document it as a known arm64 limitation in
   `docs/dev/container-build.md` next to the existing point-release note, and
   leave the tests failing with a clear explanation.

Do not skip or `GTEST_SKIP()` these tests on aarch64 without explicit human
sign-off. They assert correct behavior; hiding them loses the signal.

- [x] **A3. Report, do not silently fix.**
  **Done:** the suite is green on arm64 Debug and ASan; reported with counts.

Whatever the outcome, state plainly in the final report whether the branch has a
green suite. It currently does not, and that should not be rounded off.

---

# Item B: cross-arch index byte identity

**Status: not started. Genuinely expensive. Confirm with the human before
spending hours on B2.**

The goal is proving an index written on arm64 is byte-identical to one written
on x86-64, so an arm64 node's segments are readable by an x86 node.

Indirect evidence is already strong but is not a byte diff:
- `FastPForKernelTest` (committed) proves FastPFOR's SIMD bit-packing kernels
  produce byte-identical output to its pure-scalar kernels across all 32 widths,
  in both pack variants and both unpack entry points.
- `PForTest.pfordT4TransposeExactness` (committed) validates the T4 lane
  transpose against the unconditionally-scalar encoder.
- Encode-side `LinearPack` is AVX2-gated (`src/luxir/codec/LinearPack.h:141`) and
  therefore scalar on arm64.

- [x] **B1. Make the existing dump hook reach the block codec.**

`test/basic_tests/UnpartitionedByteIdentityTest.cpp:43` already reads a
`LUXIR_BYTE_DUMP` env var and writes index bytes. Nothing in the repo uses it.
Its corpus is 3 segments x 12 docs = 36 docs, and `Postings.h:20` sets
`DOCS_BLOCK_SIZE = 128`, so no term ever fills a block and the dump **never
reaches `simdpack`** — the kernel most at risk.

Add a second test in that file with one term on at least 128 documents (300 is
comfortable), fully deterministic (fixed ids, fixed values, no randomness, no
clock).

Critical detail: the existing dump opens with `std::ios::trunc` (line 52) and
`test/test/LuxirTest.cpp:307` appends `--gtest_shuffle` unconditionally. Two
tests writing one truncating path under a shuffled suite means whichever ran
last wins, at random. **Give the new test its own env var**, e.g.
`LUXIR_BYTE_DUMP_BLOCKS`, and leave `LUXIR_BYTE_DUMP` alone.

This task is cheap and worth doing regardless of whether B2 happens.

- [x] **B2. Produce and diff dumps from both architectures.**
  **Done 2026-09-23:** amd64 built emulated (`--target normal-dependencies`
  only; the ASan tree is not needed for a dump). Both dumps byte-identical:
  6405 bytes (`FullBlockCorpus`) and 1217 bytes (`DefaultMergeCorpus`),
  rechecked after the arm64 compiler change.

This requires a full amd64 dependency build under QEMU on Apple Silicon. Expect
hours. **Ask the human before starting it.**

```bash
docker build --platform linux/amd64 -f .devcontainer/Dockerfile --target dev \
  -t luxir-dev:amd64 . > /tmp/amd64.log 2>&1

LUXIR_DEV_IMAGE=luxir-dev:amd64 ./tools/dev-container cmake --preset container-debug
LUXIR_DEV_IMAGE=luxir-dev:amd64 ./tools/dev-container cmake --build --preset container-debug

FILTER='UnpartitionedByteIdentityTest.<new test name>'

./tools/dev-container env LUXIR_BYTE_DUMP_BLOCKS=/workspace/luxir/build/arm64.dump \
  ./build/container-arm64-debug/bin/luxir_test \
  --gtest_filter="$FILTER" --gtest_shuffle=0 --gtest_brief=1 --gtest_print_time=0

LUXIR_DEV_IMAGE=luxir-dev:amd64 ./tools/dev-container \
  env LUXIR_BYTE_DUMP_BLOCKS=/workspace/luxir/build/amd64.dump \
  ./build/container-debug/bin/luxir_test \
  --gtest_filter="$FILTER" --gtest_shuffle=0 --gtest_brief=1 --gtest_print_time=0

ls -l build/arm64.dump build/amd64.dump
[ -s build/arm64.dump ] && [ -s build/amd64.dump ] || { echo "EMPTY DUMP - gate is void"; exit 1; }
cmp build/arm64.dump build/amd64.dump && echo "cross-arch byte identity: OK"
```

`LUXIR_DEV_IMAGE` is mandatory — `tools/dev-container:8` otherwise defaults to
the arm64 image and reports a false pass. The emptiness check is not ceremony:
`cmp` on two empty files succeeds and prints OK.

If B2 is skipped, say so explicitly. Do not substitute a weaker check and
describe it as equivalent.

---

# Item C: cleanup and close-out

- [x] **C1.** `LUXIR-NOTES.md` at repo root is untracked scratch from an earlier
  session and is now stale and partly wrong. Delete it, or confirm with the
  human first if unsure.
  **Done:** read, then moved to the macOS Trash (recoverable).
- [x] **C2.** Re-read `docs/superpowers/plans/2026-09-22-aarch64-port.md` against
  what actually shipped and correct anything stale. Known drift: it describes a
  scalar `t4InverseDelta` fallback that was replaced by four SSE→NEON interleave
  mappings, and a `DYNAMIC_LIST` OpenBLAS pin that was dropped.
- [x] **C3.** Correct the `CHANGELOG.md` "Known unrelated failure" paragraph
  once item A concludes — it currently attributes the failure to tzdata, which
  is wrong.
- [x] **C4.** Decide with the human whether to restore Docker Desktop's original
  allocations from `/tmp/docker-settings-backup.json`. The larger ones are what
  make the build work; restoring them will break it again.
  **Decided:** keep the larger allocations; the build needs them.
- [x] **C5.** Consider a cheap CI guard. `.github/workflows/` has only
  `proto-docs.yml` and `site-docs.yml` — nothing prevents a future unguarded
  `#include <immintrin.h>` from re-breaking aarch64. A cross-compile syntax
  check runs in seconds and needs no container:
  ```bash
  sudo apt-get install -y g++-aarch64-linux-gnu
  deps/fetch_sources.sh
  aarch64-linux-gnu-g++ -std=c++2b -fsyntax-only -march=armv8-a \
    -Isrc -Ideps -Ideps/FastPFOR/headers -x c++ src/luxir/codec/Codec.h
  ```
  Adding CI is a separate decision — propose it, do not just add it.
  **Done:** `.github/workflows/aarch64-syntax.yml`.

---

# Traps this session hit, so you do not repeat them

1. **`[ cond ] && cmd` as a script's last statement returns 1 when the condition
   is false.** This produced three false "build failed" and one false "build
   succeeded" report. End wrapper scripts with an explicit `exit`.
2. **`grep -c` returning 0 exits 1** and will kill a `&&` chain mid-diagnostic.
3. **Dependency analysis must carry flags through.** `software-properties-common`
   depends on `gpg`, but `gpg` only *Recommends* `gpg-agent`, and
   `--no-install-recommends` strips it. `add-apt-repository` then fails on key
   import. Three review rounds cleared this trap and were wrong.
4. **Filtering files out of a grep hides bugs in exactly those files.** The
   `Codec.cpp` intrinsics were missed for hours because a sweep excluded the
   files an earlier commit had touched.
5. **Reviews swept `src/` only.** The `is_utf8` NEON C++20 violation lived in
   `deps/`, was ARM-only, and failed for a reason unrelated to intrinsics.
6. **A silent `cmake_install` exit 1 with an empty stderr meant the Docker VM
   disk was full**, not a compile problem. Check `docker run --rm <img> df -h /`
   before theorising.
7. **`gcc-ar --version` reports binutils, never GCC.** Assert symlink resolution
   instead: `readlink -f /opt/gcc/bin/gcc-ar` should end in `-16`.
8. **Check upstream before hand-writing anything.** A scalar transpose was
   written where four documented SSE→NEON one-liners were the established idiom,
   already described in `deps/FastPFOR/headers/fastpfor_neon.h`'s own header
   comment. The replacement was a net deletion.

# Non-goals

- Do not change `LUXIR_CPU_TARGET` from `armv8-a`.
- Do not add sse2neon or any new dependency.
- Do not edit `deps/FastPFOR/` — it is untracked and cloned by
  `deps/fetch_sources.sh`; edits there vanish.
- Do not bump the vcpkg baseline.
- Do not touch the x86-64 code paths.
