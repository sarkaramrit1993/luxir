// Copyright 2020-2026 Yonik Seeley and Luxir contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "luxir/reader/Postings.h"
#include "luxir/util/luxir_util.h"
#include "test/TestIndex.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace luxir;
using namespace luxir::test;

// Serializes the merged segment's files (sorted by name, each as u64 name
// length, name, u64 byte length, bytes) and pins their size and XXH3 hash.
// The constants pin the on-disk format: they must match on every arch and
// CPU tier, so a SIMD codec path that encodes differently fails here.
// Setting dumpEnv also writes the buffer to that path for cross-arch diffs.
// Each test gets its own env var: gtest shuffles by default, so two tests
// truncating one path would leave whichever ran last.
static void checkSegmentFiles(TestIndex& index, const char* dumpEnv,
                              size_t expectedLen, uint64_t expectedHash) {
  uint64_t segId = index.reader->segments()[0].segInfo.seg_id;
  std::string prefix = Postings::getIndexFileNamePrefix(segId);
  std::vector<Directory::FileInfo> infos;
  index.dir.listFiles(infos);
  std::vector<std::string> files;
  for (const auto& info : infos) files.push_back(info.name);
  std::sort(files.begin(), files.end());
  std::string buf;
  for (const auto& name : files) {
    if (!name.starts_with(prefix)) continue;
    auto input = index.dir.openFile(name);
    std::string_view bytes = input->read();
    uint64_t nameLen = name.size();
    uint64_t byteLen = bytes.size();
    buf.append((const char*) &nameLen, sizeof(nameLen));
    buf.append(name);
    buf.append((const char*) &byteLen, sizeof(byteLen));
    buf.append(bytes);
  }
  if (const char* dumpPath = std::getenv(dumpEnv)) {
    std::ofstream dump(dumpPath, std::ios::binary | std::ios::trunc);
    dump.write(buf.data(), (std::streamsize) buf.size());
    ASSERT_TRUE(dump.good());
  }
  EXPECT_EQ(expectedLen, buf.size());
  uint64_t hash = XXH3_64bits(buf.data(), buf.size());
  EXPECT_EQ(expectedHash, hash) << "actual hash 0x" << std::hex << hash;
}

TEST(UnpartitionedByteIdentityTest, DefaultMergeCorpus) {
  auto schema = std::make_shared<Schema>();
  schema->fieldTypeMap["body"] = std::make_shared<TextFieldType>(
      "body", FieldType::INDEX_DOCS_FREQS_POSITIONS);
  TestIndex index;
  index.iw = std::make_unique<IndexWriter>(index.dir, schema);
  index.iw->mergePolicy->setMergeFactor(1000);
  // This test's contract is the UNPARTITIONED byte format; keep it serial
  // regardless of the debug-build tiny default thresholds.
  index.iw->termPartitionMinBytes = INT64_MAX;
  TestField body(index, "body");
  for (int32_t segment = 0; segment < 3; segment++) {
    body.startIndexing();
    for (int32_t doc = 0; doc < 12; doc++) {
      std::string value = "alpha beta common";
      value += " term" + std::to_string(doc % 5);
      value += " seg" + std::to_string(segment);
      body.add(doc, value);
    }
    index.flush();
  }
  index.iw->mergeSegments();
  index.initReader();
  ASSERT_EQ(1u, index.reader->segments().size());

  checkSegmentFiles(index, "LUXIR_BYTE_DUMP", 1217, 0x000d1bbbeb52e5ccULL);
}

// Terms long enough to fill DOCS_BLOCK_SIZE blocks, so the dump covers the
// SIMD-packed PFor paths: "common" fills freq and position blocks (with a
// few large freqs as exceptions), "sparse" is spread wide enough that its
// doc blocks take the packed form rather than a bitset.
TEST(UnpartitionedByteIdentityTest, FullBlockCorpus) {
  auto schema = std::make_shared<Schema>();
  schema->fieldTypeMap["body"] = std::make_shared<TextFieldType>(
      "body", FieldType::INDEX_DOCS_FREQS_POSITIONS);
  TestIndex index;
  index.iw = std::make_unique<IndexWriter>(index.dir, schema);
  index.iw->mergePolicy->setMergeFactor(1000);
  index.iw->termPartitionMinBytes = INT64_MAX;
  TestField body(index, "body");
  for (int32_t segment = 0; segment < 3; segment++) {
    body.startIndexing();
    for (int32_t doc = 0; doc < 400; doc++) {
      int32_t id = segment * 400 + doc;
      int32_t freq = id % 97 == 0 ? 40 : id % 7 + 1;
      std::string value;
      for (int32_t i = 0; i < freq; i++) value += "common ";
      if (id % 9 == 0) value += "sparse ";
      value += "term" + std::to_string(id % 5);
      body.add(doc, value);
    }
    index.flush();
  }
  index.iw->mergeSegments();
  index.initReader();
  ASSERT_EQ(1u, index.reader->segments().size());
  checkSegmentFiles(index, "LUXIR_BYTE_DUMP_BLOCKS", 6405, 0xe917c855b0c17649ULL);
}
