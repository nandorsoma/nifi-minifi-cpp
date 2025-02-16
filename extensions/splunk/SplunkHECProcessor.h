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

#include "controllers/SSLContextService.h"
#include "core/Processor.h"


namespace org::apache::nifi::minifi::utils {
class HTTPClient;
}

namespace org::apache::nifi::minifi::extensions::splunk {

class SplunkHECProcessor : public core::Processor {
 public:
  EXTENSIONAPI static const core::Property Hostname;
  EXTENSIONAPI static const core::Property Port;
  EXTENSIONAPI static const core::Property Token;
  EXTENSIONAPI static const core::Property SplunkRequestChannel;
  EXTENSIONAPI static const core::Property SSLContext;

  explicit SplunkHECProcessor(const std::string& name, const utils::Identifier& uuid = {})
      : Processor(name, uuid) {
  }
  ~SplunkHECProcessor() override = default;

  void initialize() override;
  void onSchedule(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSessionFactory> &sessionFactory) override;

  core::annotation::Input getInputRequirement() const override {
    return core::annotation::Input::INPUT_REQUIRED;
  }

 protected:
  std::string getNetworkLocation() const;
  std::shared_ptr<minifi::controllers::SSLContextService> getSSLContextService(core::ProcessContext& context) const;
  void initializeClient(utils::HTTPClient& client, const std::string &url, const std::shared_ptr<minifi::controllers::SSLContextService> ssl_context_service) const;

  std::string token_;
  std::string hostname_;
  std::string port_;
  std::string request_channel_;
};
}  // namespace org::apache::nifi::minifi::extensions::splunk
