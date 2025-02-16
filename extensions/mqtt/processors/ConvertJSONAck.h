/**
 * @file ConvertAck.h
 * ConvertAck class declaration
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

#include <vector>
#include <string>
#include <memory>

#include "FlowFileRecord.h"
#include "core/ProcessSession.h"
#include "core/Core.h"
#include "core/logging/LoggerConfiguration.h"
#include "ConvertBase.h"


namespace org::apache::nifi::minifi::processors {

/**
 * Purpose: Converts JSON acks into an MQTT consumable by
 * MQTTC2Protocol.
 */
class ConvertJSONAck : public ConvertBase {
 public:
  // Constructor
  /*!
   * Create a new processor
   */
  explicit ConvertJSONAck(const std::string& name, const utils::Identifier& uuid = {})
      : ConvertBase(name, uuid) {
  }
  // Destructor
  ~ConvertJSONAck() override = default;
  // Processor Name
  static constexpr char const* ProcessorName = "ConvertJSONAck";


 public:
  /**
   * Function that's executed when the processor is scheduled.
   * @param context process context.
   * @param sessionFactory process session factory that is used when creating
   * ProcessSession objects.
   */
  void onTrigger(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSession> &session) override;

 protected:
  /**
   * Parse Topic name from the json -- given a known structure that we expect.
   * @param json json representation defined by the restful protocol
   */
  static std::string parseTopicName(const std::string &json);

 private:
  std::shared_ptr<core::logging::Logger> logger_ = core::logging::LoggerFactory<ConvertJSONAck>::getLogger();
};

}  // namespace org::apache::nifi::minifi::processors
