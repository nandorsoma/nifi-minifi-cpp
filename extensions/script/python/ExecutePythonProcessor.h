/**
 * @file ExecutePythonProcessor.h
 * ExecutePythonProcessor class declaration
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

#include "concurrentqueue.h"
#include "core/Processor.h"

#include "../ScriptEngine.h"
#include "../ScriptProcessContext.h"
#include "PythonScriptEngine.h"
#include "core/Property.h"

#pragma GCC visibility push(hidden)

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace python {
namespace processors {

class ExecutePythonProcessor : public core::Processor {
 public:
  explicit ExecutePythonProcessor(const std::string &name, const utils::Identifier &uuid = {})
      : Processor(name, uuid),
        processor_initialized_(false),
        python_dynamic_(false),
        reload_on_script_change_(true) {
  }

  EXTENSIONAPI static const core::Property ScriptFile;
  EXTENSIONAPI static const core::Property ScriptBody;
  EXTENSIONAPI static const core::Property ModuleDirectory;
  EXTENSIONAPI static const core::Property ReloadOnScriptChange;

  EXTENSIONAPI static const core::Relationship Success;
  EXTENSIONAPI static const core::Relationship Failure;

  void initialize() override;
  void onSchedule(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSessionFactory> &sessionFactory) override;
  void onTrigger(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSession> &session) override;

  void setSupportsDynamicProperties() {
    python_dynamic_ = true;
  }

  void addProperty(const std::string &name, const std::string &description, const std::string &defaultvalue, bool required, bool el) {
    python_properties_.emplace_back(
        core::PropertyBuilder::createProperty(name)->withDefaultValue(defaultvalue)->withDescription(description)->isRequired(required)->supportsExpressionLanguage(el)->build());
  }

  std::vector<core::Property> getPythonProperties() const {
    return python_properties_;
  }

  bool getPythonSupportDynamicProperties() {
    return python_dynamic_;
  }

  void setDescription(const std::string &description) {
    description_ = description;
  }

  const std::string &getDescription() const {
    return description_;
  }

  bool supportsDynamicProperties() override {
    return false;
  }

  bool isSingleThreaded() const override {
    return true;
  }

 private:
  std::vector<core::Property> python_properties_;

  std::string description_;

  bool processor_initialized_;
  bool python_dynamic_;

  std::shared_ptr<core::logging::Logger> logger_ = core::logging::LoggerFactory<ExecutePythonProcessor>::getLogger();

  std::string script_to_exec_;
  bool reload_on_script_change_;
  std::optional<std::filesystem::file_time_type> last_script_write_time_;
  std::string script_file_path_;
  std::shared_ptr<core::logging::Logger> python_logger_;
  std::unique_ptr<PythonScriptEngine> python_script_engine_;

  void appendPathForImportModules();
  void loadScriptFromFile();
  void loadScript();
  void reloadScriptIfUsingScriptFileProperty();
  void initalizeThroughScriptEngine();

  std::unique_ptr<PythonScriptEngine> createScriptEngine();
};

} /* namespace processors */
} /* namespace python */
} /* namespace minifi */
} /* namespace nifi */
} /* namespace apache */
} /* namespace org */

#pragma GCC visibility pop
