/**
 * @file CronDrivenSchedulingAgent.cpp
 * CronDrivenSchedulingAgent class implementation
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
#include "CronDrivenSchedulingAgent.h"
#include <chrono>
#include <memory>
#include <thread>
#include <iostream>
#include "core/Processor.h"
#include "core/ProcessContext.h"
#include "core/ProcessSessionFactory.h"
#include "core/Property.h"

using namespace std::literals::chrono_literals;

namespace org {
namespace apache {
namespace nifi {
namespace minifi {

utils::TaskRescheduleInfo CronDrivenSchedulingAgent::run(core::Processor* processor, const std::shared_ptr<core::ProcessContext> &processContext,
                                        const std::shared_ptr<core::ProcessSessionFactory> &sessionFactory) {
  if (this->running_ && processor->isRunning()) {
    auto uuid = processor->getUUID();
    std::chrono::system_clock::time_point result;
    std::chrono::system_clock::time_point from = std::chrono::system_clock::now();
    {
      std::lock_guard<std::mutex> locK(mutex_);

      auto sched_f = schedules_.find(uuid);
      if (sched_f != std::end(schedules_)) {
        result = last_exec_[uuid];
        if (from >= result) {
          result = sched_f->second.cron_to_next(from);
          last_exec_[uuid] = result;
        } else {
          // we may be woken up a little early so that we can honor our time.
          // in this case we can return the next time to run with the expectation
          // that the wakeup mechanism gets more granular.
          return utils::TaskRescheduleInfo::RetryIn(std::chrono::duration_cast<std::chrono::milliseconds>(result - from));
        }
      } else {
        Bosma::Cron schedule(processor->getCronPeriod());
        result = schedule.cron_to_next(from);
        last_exec_[uuid] = result;
        schedules_.insert(std::make_pair(uuid, schedule));
      }
    }

    if (result > from) {
      bool shouldYield = this->onTrigger(processor, processContext, sessionFactory);

      if (processor->isYield()) {
        // Honor the yield
        return utils::TaskRescheduleInfo::RetryIn(processor->getYieldTime());
      } else if (shouldYield && this->bored_yield_duration_ > 0ms) {
        // No work to do or need to apply back pressure
        return utils::TaskRescheduleInfo::RetryIn(this->bored_yield_duration_);
      }
    }
    return utils::TaskRescheduleInfo::RetryIn(std::chrono::duration_cast<std::chrono::milliseconds>(result - from));
  }
  return utils::TaskRescheduleInfo::Done();
}

} /* namespace minifi */
} /* namespace nifi */
} /* namespace apache */
} /* namespace org */
