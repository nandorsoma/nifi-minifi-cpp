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
#ifndef LIBMINIFI_INCLUDE_AGENT_BUILD_DESCRIPTION_H_
#define LIBMINIFI_INCLUDE_AGENT_BUILD_DESCRIPTION_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "core/controller/ControllerService.h"
#include "core/ClassLoader.h"
#include "core/expect.h"
#include "core/Property.h"
#include "core/Relationship.h"
#include "core/Processor.h"
#include "core/Annotation.h"
#include "io/validation.h"

namespace org::apache::nifi::minifi {

class ClassDescription {
 public:
  explicit ClassDescription(std::string name)
      : class_name_(std::move(name)) {
  }

  explicit ClassDescription(std::string name, std::map<std::string, core::Property> props, bool dyn_prop)
      : class_name_(std::move(name)),
        class_properties_(std::move(props)),
        dynamic_properties_(dyn_prop) {
  }

  explicit ClassDescription(std::string name, std::map<std::string, core::Property> props, std::vector<core::Relationship> class_relationships, bool dyn_prop, bool dyn_rel)
      : class_name_(std::move(name)),
        class_properties_(std::move(props)),
        class_relationships_(std::move(class_relationships)),
        dynamic_properties_(dyn_prop),
        dynamic_relationships_(dyn_rel) {
  }

  std::string class_name_;
  std::map<std::string, core::Property> class_properties_;
  std::vector<core::Relationship> class_relationships_;
  bool dynamic_properties_ = false;
  std::string inputRequirement_;
  bool isSingleThreaded_ = false;
  bool dynamic_relationships_ = false;
  bool is_controller_service_ = false;
};

struct Components {
  std::vector<ClassDescription> processors_;
  std::vector<ClassDescription> controller_services_;
  std::vector<ClassDescription> other_components_;

  [[nodiscard]] bool empty() const noexcept {
    return processors_.empty() && controller_services_.empty() && other_components_.empty();
  }
};

struct BundleDetails {
  std::string artifact;
  std::string group;
  std::string version;
};

class ExternalBuildDescription {
 private:
  static std::vector<struct BundleDetails> &getExternal() {
    static std::vector<struct BundleDetails> external_groups;
    return external_groups;
  }

  static std::map<std::string, struct Components> &getExternalMappings() {
    static std::map<std::string, struct Components> external_mappings;
    return external_mappings;
  }

 public:
  static void addExternalComponent(const BundleDetails& details, const ClassDescription& description) {
    bool found = false;
    for (const auto &d : getExternal()) {
      if (d.artifact == details.artifact) {
        found = true;
        break;
      }
    }
    if (!found) {
      getExternal().push_back(details);
    }
    if (description.is_controller_service_) {
      getExternalMappings()[details.artifact].controller_services_.push_back(description);
    } else {
      getExternalMappings()[details.artifact].processors_.push_back(description);
    }
  }

  static struct Components getClassDescriptions(const std::string &group) {
    return getExternalMappings()[group];
  }

  static std::vector<struct BundleDetails> getExternalGroups() {
    return getExternal();
  }
};

class BuildDescription {
 public:
  struct Components getClassDescriptions(const std::string& group = "minifi-system") {
    if (class_mappings_[group].empty()) {
      for (const auto& clazz : core::ClassLoader::getDefaultClassLoader().getClasses(group)) {
        std::string class_name = clazz;
        auto lastOfIdx = clazz.find_last_of("::");
        if (lastOfIdx != std::string::npos) {
          lastOfIdx++;  // if a value is found, increment to move beyond the .
          size_t nameLength = clazz.length() - lastOfIdx;
          class_name = clazz.substr(lastOfIdx, nameLength);
        }
        auto obj = core::ClassLoader::getDefaultClassLoader().instantiate(class_name, class_name);

        std::unique_ptr<core::ConfigurableComponent> component{
          utils::dynamic_unique_cast<core::ConfigurableComponent>(std::move(obj))
        };

        std::string classDescriptionName = clazz;
        utils::StringUtils::replaceAll(classDescriptionName, "::", ".");
        ClassDescription description(classDescriptionName);
        if (component) {
          auto processor = dynamic_cast<core::Processor*>(component.get());
          const bool is_processor = processor != nullptr;
          const bool is_controller_service = LIKELY(is_processor == true) ? false : dynamic_cast<core::controller::ControllerService*>(component.get()) != nullptr;

          component->initialize();
          description.class_properties_ = component->getProperties();
          description.dynamic_properties_ = component->supportsDynamicProperties();
          description.dynamic_relationships_ = component->supportsDynamicRelationships();
          if (is_processor) {
            description.inputRequirement_ = processor->getInputRequirementAsString();
            description.isSingleThreaded_ = processor->isSingleThreaded();
            description.class_relationships_ = processor->getSupportedRelationships();
            class_mappings_[group].processors_.emplace_back(description);
          } else if (is_controller_service) {
            class_mappings_[group].controller_services_.emplace_back(description);
          } else {
            class_mappings_[group].other_components_.emplace_back(description);
          }
        }
      }
    }
    return class_mappings_[group];
  }

 private:
  std::map<std::string, struct Components> class_mappings_;
};

}  // namespace org::apache::nifi::minifi

#endif  // LIBMINIFI_INCLUDE_AGENT_BUILD_DESCRIPTION_H_
