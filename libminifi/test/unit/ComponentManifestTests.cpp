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

#undef LOAD_EXTENSIONS
#include "../TestBase.h"
#include "../Catch.h"
#include "core/ConfigurableComponent.h"
#include "core/controller/ControllerService.h"
#include "core/Resource.h"
#include "core/state/nodes/AgentInformation.h"

using minifi::state::response::SerializedResponseNode;

SerializedResponseNode& get(SerializedResponseNode& node, const std::string& field) {
  REQUIRE(!node.array);
  for (auto& child : node.children) {
    if (child.name == field) return child;
  }
  throw std::logic_error("No field '" + field + "'");
}

namespace test::apple {

class ExampleService : public core::controller::ControllerService {
 public:
  using ControllerService::ControllerService;

  bool canEdit() override { return false; }
  bool supportsDynamicProperties() override { return false; }
  void yield() override {}
  bool isRunning() override { return false; }
  bool isWorkAvailable() override { return false; }
};

REGISTER_RESOURCE(ExampleService, "An example service");

class ExampleProcessor : public core::Processor {
 public:
  using Processor::Processor;
  static core::Property ExampleProperty;

  void initialize() override {
    setSupportedProperties({ExampleProperty});
  }
};

core::Property ExampleProcessor::ExampleProperty(
    core::PropertyBuilder::createProperty("Example Property")
    ->withDescription("An example property")
    ->isRequired(false)
    ->asType<ExampleService>()
    ->build());

REGISTER_RESOURCE(ExampleProcessor, "An example processor");

}  // namespace test::apple

TEST_CASE("Manifest indicates property type requirement") {
  minifi::state::response::ComponentManifest manifest("minifi-system");
  auto nodes = manifest.serialize();
  REQUIRE(nodes.size() == 1);
  REQUIRE(nodes.at(0).name == "componentManifest");

  auto& processors = get(nodes.at(0), "processors").children;

  auto example_proc_it = std::find_if(processors.begin(), processors.end(), [&] (auto& proc) {
    return get(proc, "type").value == "test.apple.ExampleProcessor";
  });
  REQUIRE(example_proc_it != processors.end());

  auto& properties = get(*example_proc_it, "propertyDescriptors").children;

  auto prop_it = std::find_if(properties.begin(), properties.end(), [&] (auto& prop) {
    return get(prop, "name").value == "Example Property";
  });

  REQUIRE(prop_it != properties.end());

  // TODO(adebreceni): based on PropertyBuilder::asType a property could accept
  //    multiple types, these would be dumped into the same object as the type of
  //    field "typeProvidedByValue" is not an array but an object
  auto& type = get(*prop_it, "typeProvidedByValue");

  REQUIRE(get(type, "type").value == "test.apple.ExampleService");
  REQUIRE(get(type, "group").value == GROUP_STR);  // fix string
  REQUIRE(get(type, "artifact").value == "minifi-system");
}
