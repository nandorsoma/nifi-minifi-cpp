/**
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

#include <string>
#include <memory>

#include "SplunkHECProcessor.h"
#include "utils/gsl.h"
#include "rapidjson/stringbuffer.h"

namespace org::apache::nifi::minifi::extensions::splunk {

class QuerySplunkIndexingStatus final : public SplunkHECProcessor {
 public:
  explicit QuerySplunkIndexingStatus(const std::string& name, const utils::Identifier& uuid = {})
      : SplunkHECProcessor(name, uuid) {
  }
  QuerySplunkIndexingStatus(const QuerySplunkIndexingStatus&) = delete;
  QuerySplunkIndexingStatus(QuerySplunkIndexingStatus&&) = delete;
  QuerySplunkIndexingStatus& operator=(const QuerySplunkIndexingStatus&) = delete;
  QuerySplunkIndexingStatus& operator=(QuerySplunkIndexingStatus&&) = delete;
  ~QuerySplunkIndexingStatus() override = default;

  EXTENSIONAPI static const core::Property MaximumWaitingTime;
  EXTENSIONAPI static const core::Property MaxQuerySize;

  EXTENSIONAPI static const core::Relationship Acknowledged;
  EXTENSIONAPI static const core::Relationship Unacknowledged;
  EXTENSIONAPI static const core::Relationship Undetermined;
  EXTENSIONAPI static const core::Relationship Failure;

  void onTrigger(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSession> &session) override;
  void initialize() override;
  void onSchedule(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSessionFactory> &sessionFactory) override;

  bool isSingleThreaded() const override {
    return true;
  }

 protected:
  uint32_t batch_size_ = 1000;
  std::chrono::milliseconds max_age_ = std::chrono::hours(1);
};

}  // namespace org::apache::nifi::minifi::extensions::splunk
