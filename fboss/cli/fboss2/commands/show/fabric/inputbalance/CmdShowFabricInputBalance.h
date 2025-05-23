/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <string>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/fabric/inputbalance/gen-cpp2/model_types.h"

namespace facebook::fboss {

struct CmdShowFabricInputBalanceTraits : public BaseCommandTraits {
  using ParentCmd = CmdShowFabric;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_SWITCH_NAME_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = cli::ShowFabricInputBalanceModel;
};
class CmdShowFabricInputBalance : public CmdHandler<
                                      CmdShowFabricInputBalance,
                                      CmdShowFabricInputBalanceTraits> {
 public:
  using ObjectArgType = CmdShowFabricInputBalanceTraits::ObjectArgType;
  using RetType = CmdShowFabricInputBalanceTraits::RetType;

  RetType queryClient(
      const HostInfo& /* hostInfo */,
      const ObjectArgType& /* queriedSwitchNames */) {
    // TODO(zecheng): Query hw agent for reachability and port information

    return createModel();
  }

  RetType createModel() {
    // TODO(zecheng): Create model for input balance
    return {};
  }

  void printOutput(const RetType& model, std::ostream& out = std::cout) {
    // TODO(zecheng): Print output of input balance
  }
};
} // namespace facebook::fboss
