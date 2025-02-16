/**
 * @file ExecuteScript.h
 * ExecuteScript class declaration
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

#include <string>
#include <memory>
#include <utility>
#include <optional>

#include "concurrentqueue.h"
#include "core/Processor.h"

#include "ScriptEngine.h"
#include "ScriptProcessContext.h"
#include "utils/Enum.h"

#ifdef LUA_SUPPORT
#include "lua/LuaScriptEngine.h"
#endif  // LUA_SUPPORT

namespace org {
namespace apache {
namespace nifi {
namespace minifi {

#ifdef PYTHON_SUPPORT
namespace python {
class PythonScriptEngine;
}
#endif  // PYTHON_SUPPORT

namespace processors {

class ScriptEngineFactory {
 public:
  ScriptEngineFactory(core::Relationship& success, core::Relationship& failure, std::shared_ptr<core::logging::Logger> logger);

  template<typename T>
  std::enable_if_t<std::is_base_of_v<script::ScriptEngine, T>, std::shared_ptr<T>> createEngine() const {
    auto engine = std::make_shared<T>();

    engine->bind("log", logger_);
    engine->bind("REL_SUCCESS", success_);
    engine->bind("REL_FAILURE", failure_);

    return engine;
  }

 private:
  core::Relationship& success_;
  core::Relationship& failure_;
  std::shared_ptr<core::logging::Logger> logger_;
};

template<typename T, typename = std::enable_if_t<std::is_base_of_v<script::ScriptEngine, T>>>
class ScriptEngineQueue {
 public:
  ScriptEngineQueue(uint8_t max_engine_count, ScriptEngineFactory& engine_factory, std::shared_ptr<core::logging::Logger> logger)
    : max_engine_count_(max_engine_count),
      engine_factory_(engine_factory),
      logger_(logger) {
  }

  std::shared_ptr<script::ScriptEngine> getScriptEngine() {
    std::shared_ptr<script::ScriptEngine> engine;
    // Use an existing engine, if one is available
    if (engine_queue_.try_dequeue(engine)) {
      logger_->log_debug("Using available [%p] script engine instance", engine.get());
      return engine;
    } else {
      const std::lock_guard<std::mutex> lock(counter_mutex_);
      if (engine_instance_count_ < max_engine_count_) {
        ++engine_instance_count_;
        engine = engine_factory_.createEngine<T>();
        logger_->log_info("Created new [%p] script engine instance. Number of instances: %d / %d.", engine.get(), engine_instance_count_, max_engine_count_);
        return engine;
      }
    }

    std::unique_lock<std::mutex> lock(queue_mutex_);
    logger_->log_debug("Waiting for available script engine instance...");
    queue_cv_.wait(lock, [this](){ return engine_queue_.size_approx() > 0; });
    if (!engine_queue_.try_dequeue(engine)) {
      throw std::runtime_error("No script engine available");
    }
    return engine;
  }

  void returnScriptEngine(std::shared_ptr<T>&& engine) {
    const std::lock_guard<std::mutex> lock(queue_mutex_);
    if (engine_queue_.size_approx() < max_engine_count_) {
      logger_->log_debug("Releasing [%p] script engine", engine.get());
      engine_queue_.enqueue(std::move(engine));
    } else {
      logger_->log_info("Destroying script engine because it is no longer needed");
    }
  }

 private:
  const uint8_t max_engine_count_;
  ScriptEngineFactory& engine_factory_;
  std::shared_ptr<core::logging::Logger> logger_;
  moodycamel::ConcurrentQueue<std::shared_ptr<T>> engine_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  uint8_t engine_instance_count_ = 0;
  std::mutex counter_mutex_;
};

class ExecuteScript : public core::Processor {
 public:
  SMART_ENUM(ScriptEngineOption,
    (LUA, "lua"),
    (PYTHON, "python")
  )

  explicit ExecuteScript(const std::string &name, const utils::Identifier &uuid = {})
      : Processor(name, uuid),
        engine_factory_(Success, Failure, logger_) {
  }

  static core::Property ScriptEngine;
  static core::Property ScriptFile;
  static core::Property ScriptBody;
  static core::Property ModuleDirectory;

  static core::Relationship Success;
  static core::Relationship Failure;

  void initialize() override;
  void onSchedule(core::ProcessContext *context, core::ProcessSessionFactory *sessionFactory) override;
  void onTrigger(core::ProcessContext* /*context*/, core::ProcessSession* /*session*/) override {
    logger_->log_error("onTrigger invocation with raw pointers is not implemented");
  }
  void onTrigger(const std::shared_ptr<core::ProcessContext> &context,
                 const std::shared_ptr<core::ProcessSession> &session) override;

 private:
  std::shared_ptr<core::logging::Logger> logger_ = core::logging::LoggerFactory<ExecuteScript>::getLogger();

  ScriptEngineOption script_engine_;
  std::string script_file_;
  std::string script_body_;
  std::optional<std::string> module_directory_;

  ScriptEngineFactory engine_factory_;
#ifdef LUA_SUPPORT
  std::unique_ptr<ScriptEngineQueue<lua::LuaScriptEngine>> script_engine_q_;
#endif  // LUA_SUPPORT
#ifdef PYTHON_SUPPORT
  std::shared_ptr<python::PythonScriptEngine> python_script_engine_;
#endif  // PYTHON_SUPPORT

  template<typename T>
  void triggerEngineProcessor(const std::shared_ptr<script::ScriptEngine> &engine,
                              const std::shared_ptr<core::ProcessContext> &context,
                              const std::shared_ptr<core::ProcessSession> &session) const {
    auto typed_engine = std::static_pointer_cast<T>(engine);
    typed_engine->onTrigger(context, session);
  }
};

} /* namespace processors */
} /* namespace minifi */
} /* namespace nifi */
} /* namespace apache */
} /* namespace org */
