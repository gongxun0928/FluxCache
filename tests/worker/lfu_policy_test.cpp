#include "worker/cache/eviction_policy.h"
#include "worker/cache/eviction_policy_factory.h"
#include "worker/cache/lfu_policy.h"
#include "common/types.h"
#include <gtest/gtest.h>

namespace fluxcache {

class LfuPolicyTest : public ::testing::Test {
 protected:
  void SetUp() override { policy_ = std::make_unique<LfuPolicy>(); }

  std::unique_ptr<LfuPolicy> policy_;
};

TEST_F(LfuPolicyTest, PickVictim_Empty_ReturnsNullopt) {
  auto victim = policy_->PickVictim();
  EXPECT_FALSE(victim.has_value());
}

TEST_F(LfuPolicyTest, PickVictim_SinglePage_ReturnsThatPage) {
  PageId id{MakeBlockId(1, 0), 0};
  policy_->OnInsert(id);
  auto victim = policy_->PickVictim();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(victim->block_id, id.block_id);
  EXPECT_EQ(victim->page_index, id.page_index);
}

TEST_F(LfuPolicyTest, PickVictim_LowestFreqFirst) {
  PageId a{MakeBlockId(1, 0), 0};
  PageId b{MakeBlockId(2, 0), 0};
  PageId c{MakeBlockId(3, 0), 0};
  policy_->OnInsert(a);
  policy_->OnInsert(b);
  policy_->OnInsert(c);
  policy_->OnAccess(a);
  policy_->OnAccess(a);  // a freq=3, b freq=1, c freq=1
  // Among freq=1: b and c. LRU order: b (inserted first) then c. Victim = b.
  auto victim = policy_->PickVictim();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(victim->block_id, b.block_id);
}

TEST_F(LfuPolicyTest, PickVictim_SameFreq_LruOrder) {
  PageId a{MakeBlockId(1, 0), 0};
  PageId b{MakeBlockId(2, 0), 0};
  policy_->OnInsert(a);
  policy_->OnInsert(b);
  // Both freq=1. a inserted first -> LRU. Victim = a.
  auto victim = policy_->PickVictim();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(victim->block_id, a.block_id);
}

TEST_F(LfuPolicyTest, OnAccess_IncreasesFreq) {
  PageId a{MakeBlockId(1, 0), 0};
  PageId b{MakeBlockId(2, 0), 0};
  policy_->OnInsert(a);
  policy_->OnInsert(b);
  policy_->OnAccess(a);
  policy_->OnAccess(a);
  policy_->OnAccess(a);  // a freq=4, b freq=1. Victim = b.
  auto victim = policy_->PickVictim();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(victim->block_id, b.block_id);
}

TEST_F(LfuPolicyTest, OnRemove_ExcludesFromEviction) {
  PageId a{MakeBlockId(1, 0), 0};
  PageId b{MakeBlockId(2, 0), 0};
  policy_->OnInsert(a);
  policy_->OnInsert(b);
  policy_->OnRemove(a);
  auto victim = policy_->PickVictim();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(victim->block_id, b.block_id);
}

TEST_F(LfuPolicyTest, GetOrderedFromMru_MfuToLfu) {
  PageId a{MakeBlockId(1, 0), 0};
  PageId b{MakeBlockId(2, 0), 0};
  PageId c{MakeBlockId(3, 0), 0};
  policy_->OnInsert(a);
  policy_->OnInsert(b);
  policy_->OnInsert(c);
  policy_->OnAccess(b);
  policy_->OnAccess(b);  // b freq=3, a freq=1, c freq=1
  // MFU to LFU: b first (freq=3), then a and c (freq=1, c MRU then a LRU).
  auto ordered = policy_->GetOrderedFromMru();
  ASSERT_EQ(ordered.size(), 3u);
  EXPECT_EQ(ordered[0].block_id, b.block_id);
  EXPECT_EQ(ordered[1].block_id, c.block_id);
  EXPECT_EQ(ordered[2].block_id, a.block_id);
}

TEST_F(LfuPolicyTest, Factory_CreatesLFU) {
  auto p = CreateEvictionPolicy(EvictionPolicyType::kLFU);
  ASSERT_NE(p, nullptr);
  PageId id{MakeBlockId(1, 0), 0};
  p->OnInsert(id);
  auto victim = p->PickVictim();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(victim->block_id, id.block_id);
}

}  // namespace fluxcache
