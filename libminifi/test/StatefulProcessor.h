/**
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <memory>
#include <utility>
#include <vector>
#include "core/Processor.h"
#include "core/CoreComponentState.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace processors {

class StatefulProcessor : public core::Processor {
 public:
  using core::Processor::Processor;

  using HookType = std::function<void(core::CoreComponentStateManager&)>;

  void onSchedule(const std::shared_ptr<core::ProcessContext>& context, const std::shared_ptr<core::ProcessSessionFactory>&) override;

  void onTrigger(const std::shared_ptr<core::ProcessContext>&, const std::shared_ptr<core::ProcessSession>&) override;

  void setHooks(HookType onScheduleHook, std::vector<HookType> onTriggerHooks);

  [[nodiscard]] bool hasFinishedHooks() const;

 private:
  mutable std::mutex mutex_;
  core::CoreComponentStateManager* state_manager_;
  HookType on_schedule_hook_;
  std::vector<HookType> on_trigger_hooks_;
  size_t on_trigger_hook_index_ = 0;
};

} /* namespace processors */
} /* namespace minifi */
} /* namespace nifi */
} /* namespace apache */
} /* namespace org */
