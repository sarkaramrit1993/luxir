// Copyright 2020-2026 Yonik Seeley and Luxir contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <bit>
#include <map>
#include <string>

#include "test/LuxirTest.h"
#include "test/TestUtils.h"
#include "test/CollectionHelper.h"
#include "test/LocalReq.h"

using namespace luxir;
using namespace luxir::test;

// BM25 scores must be bit-identical on every platform and build type. The
// constants were computed with separate multiply and add (-ffp-contract=off).
// d3 for "a" and d3/d5 for "b" round differently in the last bit if
// 1 + termFreq * invNorm is fused into an FMA.
TEST(ScoreBitsTest, bm25ScoresArePinned) {
  CollectionHelper helper;
  ASSERT_TRUE(helper.indexAll({
    flatdoc("id", "d0", "body_w", "a a c c c"),
    flatdoc("id", "d1", "body_w", "a b b b b b b z z z"),
    flatdoc("id", "d2", "body_w", "b z z z z"),
    flatdoc("id", "d3", "body_w", "a a a a a b b b b b c c"),
    flatdoc("id", "d4", "body_w", "c c c c c c c c c c z z"),
    flatdoc("id", "d5", "body_w", "b b b b b c c c z z z z"),
  }, UpdateMessage::COMMIT).success);

  using Bits = std::map<std::string, uint32_t>;
  const std::map<std::string, Bits> expected = {
    {"body_w:a", {{"d0", 0x3eff1ef1}, {"d1", 0x3e9cbbfe}, {"d3", 0x3f096713}}},
    {"body_w:b", {{"d1", 0x3ebad8d7}, {"d2", 0x3e7ddf50}, {"d3", 0x3eaf2b4a}, {"d5", 0x3eaf2b4a}}},
    {"body_w:c", {{"d0", 0x3eb36fc9}, {"d3", 0x3e82dec1}, {"d4", 0x3ec5728c}, {"d5", 0x3e98432a}}},
  };
  for (const auto& [query, want] : expected) {
    SCOPED_TRACE(query);
    auto req = localReq(helper.getSearchEngine());
    req->collection("main");
    req->topDocs("q").exprQuery(query).getScores().fields({"id"}).limit(10);
    req->execute();
    ASSERT_TRUE(req->ok()) << req->errorMsg();
    auto docs = req->getDocs("q");
    const auto* column = req->docList("q")->columns.find("_score_");
    ASSERT_NE(nullptr, column);
    const auto* scores = std::get_if<api::ColFloat>(&column->kind);
    ASSERT_NE(nullptr, scores);
    ASSERT_EQ(docs.size(), scores->v.size());
    Bits got;
    for (size_t i = 0; i < docs.size(); i++) {
      got[std::get<std::string>(*find(docs[i], "id"))] = std::bit_cast<uint32_t>(scores->v[i]);
    }
    EXPECT_EQ(want, got);
  }
}
