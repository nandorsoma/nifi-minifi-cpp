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
#ifndef LIBMINIFI_INCLUDE_CORE_SERIALIZABLECOMPONENT_H_
#define LIBMINIFI_INCLUDE_CORE_SERIALIZABLECOMPONENT_H_

#include <memory>
#include <string>

#include "core/Connectable.h"
#include "core/Core.h"
#include "utils/gsl.h"

namespace org::apache::nifi::minifi::core {

/**
 * Represents a component that is serializable and an extension point of core Component
 */
class SerializableComponent : public core::Connectable {
 public:
  SerializableComponent(const std::string& name) // NOLINT
        : core::Connectable(name) {
    }

  SerializableComponent(const std::string& name, const utils::Identifier& uuid)
      : core::Connectable(name, uuid) {
  }

  ~SerializableComponent() override = default;

  /**
   * Serialize this object into the the store
   * @param store object in which we are serializing data into
   * @return status of this serialization.
   */
  virtual bool Serialize(const std::shared_ptr<core::SerializableComponent> &store) = 0;

  /**
   * Deserialization from the parameter store into the current object
   * @param store from which we are deserializing the current object
   * @return status of this deserialization.
   */
  virtual bool DeSerialize(const std::shared_ptr<core::SerializableComponent> &store) = 0;

  /**
   * Deserializes the current object using buffer
   * @param buffer buffer from which we can deserialize the currenet object
   * @param bufferSize length of buffer from which we can deserialize the current object.
   * @return status of the deserialization.
   */
  virtual bool DeSerialize(gsl::span<const std::byte>) = 0;

  /**
   * Serialization of this object into buffer
   * @param key string that represents this objects identifier
   * @param buffer buffer that contains the serialized object
   * @param bufferSize length of buffer
   * @return status of serialization
   */
  virtual bool Serialize(const std::string& /*key*/, const uint8_t* /*buffer*/, const size_t /*bufferSize*/) {
    return false;
  }

  void yield() override { }

  /**
   * Determines if we are connected and operating
   */
  bool isRunning() override {
    return true;
  }

  /**
   * Determines if work is available by this connectable
   * @return boolean if work is available.
   */
  bool isWorkAvailable() override {
    return true;
  }
};

}  // namespace org::apache::nifi::minifi::core

#endif  // LIBMINIFI_INCLUDE_CORE_SERIALIZABLECOMPONENT_H_

