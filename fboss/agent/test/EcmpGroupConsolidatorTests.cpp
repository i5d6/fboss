/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/EcmpGroupConsolidator.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/types.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>

using namespace facebook::fboss;
using folly::IPAddress;

const AdminDistance kDefaultAdminDistance = AdminDistance::EBGP;

RouteNextHopSet makeNextHops(int n) {
  CHECK_LT(n, 255);
  RouteNextHopSet h;
  for (int i = 0; i < n; i++) {
    std::stringstream ss;
    ss << std::hex << i + 1;
    auto ipStr = "100::" + ss.str();
    h.emplace(UnresolvedNextHop(IPAddress(ipStr), UCMP_DEFAULT_WEIGHT));
  }
  return h;
}

RouteV6::Prefix makePrefix(int offset) {
  std::stringstream ss;
  ss << std::hex << offset;
  return RouteV6::Prefix(
      folly::IPAddressV6(folly::sformat("2601:db00:2110:{}::", ss.str())), 64);
}

std::shared_ptr<RouteV6> makeRoute(
    const RouteV6::Prefix& pfx,
    const RouteNextHopSet& nextHops) {
  RouteNextHopEntry nhopEntry(nextHops, kDefaultAdminDistance);
  auto rt = std::make_shared<RouteV6>(
      RouteV6::makeThrift(pfx, ClientID(0), nhopEntry));
  rt->setResolved(nhopEntry);
  return rt;
}

ForwardingInformationBaseV6* fib(std::shared_ptr<SwitchState>& newState) {
  return newState->getFibs()
      ->getNode(RouterID(0))
      ->getFibV6()
      ->modify(RouterID(0), &newState);
}
const std::shared_ptr<ForwardingInformationBaseV6> cfib(
    const std::shared_ptr<SwitchState>& newState) {
  return newState->getFibs()->getNode(RouterID(0))->getFibV6();
}

class NextHopIdAllocatorTest : public ::testing::Test {
 public:
  RouteNextHopSet defaultNhops() const {
    return makeNextHops(54);
  }
  HwSwitchMatcher hwMatcher() const {
    return HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)}));
  }
  void consolidate(const std::shared_ptr<SwitchState>& state) {
    consolidator_.consolidate(StateDelta(state_, state));
    state_ = state;
    state_->publish();
  }
  RouteV6::Prefix nextPrefix() const {
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    for (auto offset = 0; offset < std::numeric_limits<uint16_t>::max();
         ++offset) {
      auto pfx = makePrefix(offset);
      if (!fib6->exactMatch(pfx)) {
        return pfx;
      }
    }
    CHECK(false) << " Should never get here";
  }
  void SetUp() override {
    FLAGS_consolidate_ecmp_groups = true;
    state_ = std::make_shared<SwitchState>();
    auto fibContainer =
        std::make_shared<ForwardingInformationBaseContainer>(RouterID(0));
    auto mfib = std::make_shared<MultiSwitchForwardingInformationBaseMap>();
    mfib->updateForwardingInformationBaseContainer(
        std::move(fibContainer), hwMatcher());
    state_->resetForwardingInformationBases(mfib);
    state_->publish();
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    for (auto i = 0; i < 10; ++i) {
      auto pfx = makePrefix(i);
      auto route = makeRoute(pfx, defaultNhops());
      fib6->addNode(pfx.str(), std::move(route));
    }
    consolidate(newState);
  }
  std::optional<EcmpGroupConsolidator::NextHopGroupId> getNhopId(
      const RouteNextHopSet& nhops) const {
    std::optional<EcmpGroupConsolidator::NextHopGroupId> nhopId;
    auto nitr = consolidator_.getNhopsToId().find(nhops);
    if (nitr != consolidator_.getNhopsToId().end()) {
      nhopId = nitr->second;
    }
    return nhopId;
  }
  std::shared_ptr<SwitchState> state_;
  EcmpGroupConsolidator consolidator_;
};

TEST_F(NextHopIdAllocatorTest, init) {
  const auto& nhops2Id = consolidator_.getNhopsToId();
  EXPECT_EQ(nhops2Id.size(), 1);
  auto id = *getNhopId(defaultNhops());
  EXPECT_EQ(id, 1);
  // All routes point to same nhop group
  EXPECT_EQ(consolidator_.getRouteUsageCount(id), cfib(state_)->size());
}

TEST_F(NextHopIdAllocatorTest, addRouteSameNhops) {
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  fib6->addNode(makeRoute(nextPrefix(), defaultNhops()));
  EXPECT_EQ(fib6->size(), routesBefore + 1);
  consolidate(newState);
  const auto& nhops2Id = consolidator_.getNhopsToId();
  EXPECT_EQ(nhops2Id.size(), 1);
  auto id = *getNhopId(defaultNhops());
  EXPECT_EQ(id, 1);
  // All routes point to same nhop group
  EXPECT_EQ(consolidator_.getRouteUsageCount(id), cfib(state_)->size());
}

TEST_F(NextHopIdAllocatorTest, addRouteNewNhops) {
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  auto newNhops = defaultNhops();
  newNhops.erase(newNhops.begin());
  fib6->addNode(makeRoute(nextPrefix(), newNhops));
  EXPECT_EQ(fib6->size(), routesBefore + 1);
  consolidate(newState);
  const auto& nhops2Id = consolidator_.getNhopsToId();
  EXPECT_EQ(nhops2Id.size(), 2);
  auto idDefaultNhops = *getNhopId(defaultNhops());
  EXPECT_EQ(idDefaultNhops, 1);
  // All but one routes point to same nhop group
  EXPECT_EQ(
      cfib(state_)->size() - 1,
      consolidator_.getRouteUsageCount(idDefaultNhops));
  auto idNewNhops = *getNhopId(newNhops);
  EXPECT_EQ(idNewNhops, 2);
  // One route points to new nhop group
  EXPECT_EQ(consolidator_.getRouteUsageCount(2), 1);
}

TEST_F(NextHopIdAllocatorTest, addRemoveRouteNewNhopsUnresolved) {
  auto newState = state_->clone();
  const auto& nhops2Id = consolidator_.getNhopsToId();
  auto groupId = *getNhopId(defaultNhops());
  EXPECT_EQ(groupId, 1);
  EXPECT_EQ(nhops2Id.size(), 1);
  EXPECT_EQ(nhops2Id.find(defaultNhops())->second, groupId);
  auto newNhops = defaultNhops();
  newNhops.erase(newNhops.begin());
  auto newRoute = makeRoute(nextPrefix(), newNhops);
  newRoute->clearForward();
  {
    auto fib6 = fib(newState);
    auto routesBefore = fib6->size();
    // All routes point to same nhop group
    EXPECT_EQ(routesBefore, consolidator_.getRouteUsageCount(groupId));
    fib6->addNode(newRoute);
    EXPECT_EQ(fib6->size(), routesBefore + 1);
    consolidate(newState);
    // New nhops don't get a id, since no resolved routes point to it
    EXPECT_FALSE(getNhopId(newNhops).has_value());
    // All routes point to same nhop group, new route is unresolved
    EXPECT_EQ(consolidator_.getRouteUsageCount(groupId), routesBefore);
  }
  {
    auto newerState = newState->clone();
    auto fib6 = fib(newerState);
    auto routesBefore = fib6->size();
    // All resolved routes point to same nhop group
    EXPECT_EQ(routesBefore - 1, consolidator_.getRouteUsageCount(groupId));
    fib6->removeNode(newRoute);
    EXPECT_EQ(fib6->size(), routesBefore - 1);
    consolidate(newerState);
    const auto& nhops2Id = consolidator_.getNhopsToId();
    EXPECT_EQ(nhops2Id.size(), 1);
    EXPECT_EQ(*getNhopId(defaultNhops()), groupId);
    EXPECT_FALSE(getNhopId(newNhops).has_value());
    // All resolved routes point to same nhop group
    EXPECT_EQ(consolidator_.getRouteUsageCount(groupId), cfib(state_)->size());
  }
}

TEST_F(NextHopIdAllocatorTest, updateRouteNhops) {
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  auto newNhops = defaultNhops();
  newNhops.erase(newNhops.begin());
  fib6->updateNode(makeRoute(makePrefix(0), newNhops));
  EXPECT_EQ(fib6->size(), routesBefore);
  consolidate(newState);
  const auto& nhops2Id = consolidator_.getNhopsToId();
  EXPECT_EQ(nhops2Id.size(), 2);
  auto defaultNhopsId = *getNhopId(defaultNhops());
  auto newNhopsId = *getNhopId(newNhops);
  EXPECT_EQ(defaultNhopsId, 1);
  EXPECT_EQ(newNhopsId, 2);
  // All but one route point to defaultNhops
  EXPECT_EQ(consolidator_.getRouteUsageCount(defaultNhopsId), routesBefore - 1);
  EXPECT_EQ(consolidator_.getRouteUsageCount(newNhopsId), 1);
}

TEST_F(NextHopIdAllocatorTest, updateRouteToUnresolved) {
  auto defaultNhopsId = *getNhopId(defaultNhops());
  EXPECT_EQ(defaultNhopsId, 1);
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto updatedRoute = fib6->exactMatch(makePrefix(0))->clone();
  updatedRoute->clearForward();
  auto routesBefore = fib6->size();
  // All routes point to defaultNhops
  EXPECT_EQ(consolidator_.getRouteUsageCount(defaultNhopsId), routesBefore);
  fib6->updateNode(updatedRoute);
  EXPECT_EQ(fib6->size(), routesBefore);
  consolidate(newState);
  const auto& nhops2Id = consolidator_.getNhopsToId();
  EXPECT_EQ(nhops2Id.size(), 1);
  EXPECT_EQ(*getNhopId(defaultNhops()), defaultNhopsId);
  // All but newly unresolved route point to defaultNhops
  EXPECT_EQ(consolidator_.getRouteUsageCount(defaultNhopsId), routesBefore - 1);
}
