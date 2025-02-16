/**
 * PutOPC class declaration
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
#include <string>
#include <vector>

#include "opc.h"
#include "opcbase.h"
#include "FlowFileRecord.h"
#include "core/Processor.h"
#include "core/ProcessSession.h"
#include "core/Property.h"
#include "controllers/SSLContextService.h"
#include "core/logging/LoggerConfiguration.h"
#include "utils/Id.h"

namespace org::apache::nifi::minifi::processors {

class PutOPCProcessor : public BaseOPCProcessor {
 public:
  static constexpr char const* ProcessorName = "PutOPC";

  static core::Property ParentNodeIDType;
  static core::Property ParentNodeID;
  static core::Property ParentNameSpaceIndex;
  static core::Property ValueType;

  static core::Property TargetNodeIDType;
  static core::Property TargetNodeID;
  static core::Property TargetNodeBrowseName;
  static core::Property TargetNodeNameSpaceIndex;

  static core::Relationship Success;
  static core::Relationship Failure;

  explicit PutOPCProcessor(const std::string& name, const utils::Identifier& uuid = {})
      : BaseOPCProcessor(name, uuid), nameSpaceIdx_(0), parentExists_(false) {
    logger_ = core::logging::LoggerFactory<PutOPCProcessor>::getLogger();
  }

  void onSchedule(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSessionFactory> &factory) override;
  void onTrigger(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSession> &session) override;
  void initialize() override;

 private:
  core::annotation::Input getInputRequirement() const override {
    return core::annotation::Input::INPUT_REQUIRED;
  }

  std::string nodeID_;
  int32_t nameSpaceIdx_{};
  opc::OPCNodeIDType idType_{};
  UA_NodeId parentNodeID_{};
  bool parentExists_{};
  opc::OPCNodeDataType nodeDataType_{};
};

}  // namespace org::apache::nifi::minifi::processors
