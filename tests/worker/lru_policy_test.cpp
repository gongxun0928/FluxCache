#include "worker/cache/eviction_policy.h"
#include "worker/cache/eviction_policy_factory.h"
#include "worker/cache/lru_policy.h"
#include "common/types.h"
#include <gtest/gtest.h>

namespace fluxcache {

class LruPolicyTest : public ::testing::Test {
 protected:
  void SetUp() override { policy_ = std::make_unique<LruPolicy>(); }

  std::unique_ptr<LruPolicy> policy_;
};

TEST_F(LruPolicyTest, PickVictim_Empty_ReturnsNullopt) {
  auto victim = policy_->PickVictim();
  EXPECT_FALSE(victim.has_value());
}

TEST_F(LruPolicyTest, PickVictim_SinglePage_ReturnsThatPage) {
  PageId id{MakeBlockId(1, 0), 0};
  policy_->OnInsert(id);
  auto victim = policy_->PickVictim();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(victim->block_id, id.block_id);
  EXPECT_EQ(victim->page_index, id.page_index);
}

TEST_F(LruPolicyTest, PickVictim_OrderByInsertion) {
  PageId a{MakeBlockId(1, 0), 0};
  PageId b{MakeBlockId(2, 0), 0};
  PageId c{MakeBlockId(3, 0), 0};
  policy_->OnInsert(a);
  policy_->OnInsert(b);
  policy_->OnInsert(c);
  // Insert order: a, b, c. LRU order: a (oldest), b, c (newest).
  // PickVictim returns LRU = a.
  auto victim = policy_->PickVictim();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(victim->block_id, a.block_id);
}

TEST_F(LruPolicyTest, OnAccess_ChangesEvictionOrder) {
  PageId a{MakeBlockId(1, 0), 0};
  PageId b{MakeBlockId(2, 0), 0};
  PageId c{MakeBlockId(3, 0), 0};
  policy_->OnInsert(a);
  policy_->OnInsert(b);
  policy_->OnInsert(c);
  // Before access: LRU order a, b, c. Victim = a.
  policy_->OnAccess(a);
  // After access a: LRU order b, c, a. Victim = b.
  auto victim = policy_->PickVictim();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(victim->block_id, b.block_id);
}

TEST_F(LruPolicyTest, OnRemove_ExcludesFromEviction) {
  PageId a{MakeBlockId(1, 0), 0};
  PageId b{MakeBlockId(2, 0), 0};
  policy_->OnInsert(a);
  policy_->OnInsert(b);
  policy_->OnRemove(a);
  auto victim = policy_->PickVictim();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(victim->block_id, b.block_id);
}

TEST_F(LruPolicyTest, Factory_CreatesLRU) {
  auto p = CreateEvictionPolicy(EvictionPolicyType::kLRU);
  ASSERT_NE(p, nullptr);
  PageId id{MakeBlockId(1, 0), 0};
  p->OnInsert(id);
  auto victim = p->PickVictim();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(victim->block_id, id.block_id);
}

}  // namespace fluxcache
