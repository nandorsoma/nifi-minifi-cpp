/**
 * @file Processor.cpp
 * Processor class implementation
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
#include "core/Processor.h"

#include <time.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Connection.h"
#include "core/Connectable.h"
#include "core/logging/LoggerConfiguration.h"
#include "core/ProcessorConfig.h"
#include "core/ProcessContext.h"
#include "core/ProcessSessionFactory.h"
#include "io/StreamFactory.h"
#include "utils/gsl.h"

using namespace std::literals::chrono_literals;

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace core {

Processor::Processor(const std::string& name)
    : Connectable(name),
      ConfigurableComponent(),
      logger_(logging::LoggerFactory<Processor>::getLogger()) {
  has_work_.store(false);
  // Setup the default values
  state_ = DISABLED;
  strategy_ = TIMER_DRIVEN;
  _triggerWhenEmpty = false;
  scheduling_period_nano_ = MINIMUM_SCHEDULING_NANOS;
  run_duration_nano_ = DEFAULT_RUN_DURATION;
  yield_period_msec_ = DEFAULT_YIELD_PERIOD_SECONDS;
  penalization_period_ = DEFAULT_PENALIZATION_PERIOD;
  max_concurrent_tasks_ = DEFAULT_MAX_CONCURRENT_TASKS;
  active_tasks_ = 0;
  incoming_connections_Iter = this->incoming_connections_.begin();
  logger_->log_debug("Processor %s created UUID %s", name_, getUUIDStr());
}

Processor::Processor(const std::string& name, const utils::Identifier& uuid)
    : Connectable(name, uuid),
      ConfigurableComponent(),
      logger_(logging::LoggerFactory<Processor>::getLogger()) {
  has_work_.store(false);
  // Setup the default values
  state_ = DISABLED;
  strategy_ = TIMER_DRIVEN;
  _triggerWhenEmpty = false;
  scheduling_period_nano_ = MINIMUM_SCHEDULING_NANOS;
  run_duration_nano_ = DEFAULT_RUN_DURATION;
  yield_period_msec_ = DEFAULT_YIELD_PERIOD_SECONDS;
  penalization_period_ = DEFAULT_PENALIZATION_PERIOD;
  max_concurrent_tasks_ = DEFAULT_MAX_CONCURRENT_TASKS;
  active_tasks_ = 0;
  incoming_connections_Iter = this->incoming_connections_.begin();
  logger_->log_debug("Processor %s created with uuid %s", name_, getUUIDStr());
}

bool Processor::isRunning() {
  return (state_ == RUNNING && active_tasks_ > 0);
}

void Processor::setScheduledState(ScheduledState state) {
  state_ = state;
  if (state == STOPPED) {
    notifyStop();
  }
}

bool Processor::addConnection(Connectable* conn) {
  enum class SetAs{
    NONE,
    OUTPUT,
    INPUT,
  };
  SetAs result = SetAs::NONE;

  if (isRunning()) {
    logger_->log_warn("Can not add connection while the process %s is running", name_);
    return false;
  }
  const auto connection = dynamic_cast<Connection*>(conn);
  if (!connection) {
    return false;
  }

  std::lock_guard<std::mutex> lock(getGraphMutex());

  auto updateGraph = gsl::finally([&] {
    if (result == SetAs::INPUT) {
      updateReachability(lock);
    } else if (result == SetAs::OUTPUT) {
      updateReachability(lock, true);
    }
  });

  utils::Identifier srcUUID = connection->getSourceUUID();
  utils::Identifier destUUID = connection->getDestinationUUID();

  if (uuid_ == destUUID) {
    // Connection is destination to the current processor
    if (incoming_connections_.find(connection) == incoming_connections_.end()) {
      incoming_connections_.insert(connection);
      connection->setDestination(this);
      logger_->log_debug("Add connection %s into Processor %s incoming connection", connection->getName(), name_);
      incoming_connections_Iter = this->incoming_connections_.begin();
      result = SetAs::OUTPUT;
    }
  }
  if (uuid_ == srcUUID) {
    for (const auto& rel : connection->getRelationships()) {
      const auto relationship = rel.getName();
      // Connection is source from the current processor
      auto &&it = outgoing_connections_.find(relationship);
      if (it != outgoing_connections_.end()) {
        // We already has connection for this relationship
        std::set<Connectable*> existedConnection = it->second;
        if (existedConnection.find(connection) == existedConnection.end()) {
          // We do not have the same connection for this relationship yet
          existedConnection.insert(connection);
          connection->setSource(this);
          outgoing_connections_[relationship] = existedConnection;
          logger_->log_debug("Add connection %s into Processor %s outgoing connection for relationship %s", connection->getName(), name_, relationship);
          result = SetAs::INPUT;
        }
      } else {
        // We do not have any outgoing connection for this relationship yet
        std::set<Connectable*> newConnection;
        newConnection.insert(connection);
        connection->setSource(this);
        outgoing_connections_[relationship] = newConnection;
        logger_->log_debug("Add connection %s into Processor %s outgoing connection for relationship %s", connection->getName(), name_, relationship);
        result = SetAs::INPUT;
      }
    }
  }
  return result != SetAs::NONE;
}

bool Processor::flowFilesOutGoingFull() const {
  std::lock_guard<std::mutex> lock(mutex_);

  for (const auto& connection_pair : outgoing_connections_) {
    // We already has connection for this relationship
    std::set<Connectable*> existedConnection = connection_pair.second;
    const bool has_full_connection = std::any_of(begin(existedConnection), end(existedConnection), [](const Connectable* conn) {
      auto connection = dynamic_cast<const Connection*>(conn);
      return connection && connection->isFull();
    });
    if (has_full_connection) { return true; }
  }

  return false;
}

void Processor::onTrigger(ProcessContext *context, ProcessSessionFactory *sessionFactory) {
  auto session = sessionFactory->createSession();

  try {
    // Call the virtual trigger function
    onTrigger(context, session.get());
    session->commit();
  } catch (std::exception &exception) {
    logger_->log_warn("Caught \"%s\" (%s) during Processor::onTrigger of processor: %s (%s)",
        exception.what(), typeid(exception).name(), getUUIDStr(), getName());
    session->rollback();
    throw;
  } catch (...) {
    logger_->log_warn("Caught unknown exception during Processor::onTrigger of processor: %s (%s)", getUUIDStr(), getName());
    session->rollback();
    throw;
  }
}

void Processor::onTrigger(const std::shared_ptr<ProcessContext> &context, const std::shared_ptr<ProcessSessionFactory> &sessionFactory) {
  auto session = sessionFactory->createSession();

  try {
    // Call the virtual trigger function
    onTrigger(context, session);
    session->commit();
  } catch (std::exception &exception) {
    logger_->log_warn("Caught \"%s\" (%s) during Processor::onTrigger of processor: %s (%s)",
        exception.what(), typeid(exception).name(), getUUIDStr(), getName());
    session->rollback();
    throw;
  } catch (...) {
    logger_->log_warn("Caught unknown exception during Processor::onTrigger of processor: %s (%s)", getUUIDStr(), getName());
    session->rollback();
    throw;
  }
}

bool Processor::isWorkAvailable() {
  // We have work if any incoming connection has work
  std::lock_guard<std::mutex> lock(mutex_);
  bool hasWork = false;

  try {
    for (const auto &conn : incoming_connections_) {
      auto connection = dynamic_cast<Connection*>(conn);
      if (!connection) {
        continue;
      }
      if (connection->isWorkAvailable()) {
        hasWork = true;
        break;
      }
    }
  } catch (...) {
    logger_->log_error("Caught an exception while checking if work is available;"
                       " unless it was positively determined that work is available, assuming NO work is available!");
  }

  return hasWork;
}

// must hold the graphMutex
void Processor::updateReachability(const std::lock_guard<std::mutex>& graph_lock, bool force) {
  bool didChange = force;
  for (auto& outIt : outgoing_connections_) {
    for (auto& outConn : outIt.second) {
      auto connection = dynamic_cast<Connection*>(outConn);
      if (!connection) {
        continue;
      }
      auto dest = dynamic_cast<Processor*>(connection->getDestination());
      if (!dest) {
        continue;
      }
      if (reachable_processors_[connection].insert(dest).second) {
        didChange = true;
      }
      for (auto& reachedIt : dest->reachable_processors_) {
        for (auto &reached_proc : reachedIt.second) {
          if (reachable_processors_[connection].insert(reached_proc).second) {
            didChange = true;
          }
        }
      }
    }
  }
  if (didChange) {
    // propagate the change to sources
    for (auto& inConn : incoming_connections_) {
      auto connection = dynamic_cast<Connection*>(inConn);
      if (!connection) {
        continue;
      }
      auto source = dynamic_cast<Processor*>(connection->getSource());
      if (!source) {
        continue;
      }
      source->updateReachability(graph_lock);
    }
  }
}

bool Processor::partOfCycle(Connection* conn) {
  auto source = dynamic_cast<Processor*>(conn->getSource());
  if (!source) {
    return false;
  }
  auto it = source->reachable_processors_.find(conn);
  if (it == source->reachable_processors_.end()) {
    return false;
  }
  return it->second.find(source) != it->second.end();
}

bool Processor::isThrottledByBackpressure() const {
  bool isThrottledByOutgoing = ([&] {
    for (auto &outIt : outgoing_connections_) {
      for (auto &out : outIt.second) {
        auto connection = dynamic_cast<Connection*>(out);
        if (!connection) {
          continue;
        }
        if (connection->isFull()) {
          return true;
        }
      }
    }
    return false;
  })();
  bool isForcedByIncomingCycle = ([&] {
    for (auto &inConn : incoming_connections_) {
      auto connection = dynamic_cast<Connection*>(inConn);
      if (!connection) {
        continue;
      }
      if (partOfCycle(connection) && connection->isFull()) {
        return true;
      }
    }
    return false;
  })();
  return isThrottledByOutgoing && !isForcedByIncomingCycle;
}

Connectable* Processor::pickIncomingConnection() {
  std::lock_guard<std::mutex> rel_guard(relationship_mutex_);

  auto beginIt = incoming_connections_Iter;
  Connectable* inConn;
  do {
    inConn = getNextIncomingConnectionImpl(rel_guard);
    auto connection = dynamic_cast<Connection*>(inConn);
    if (!connection) {
      continue;
    }
    if (partOfCycle(connection) && connection->isFull()) {
      return inConn;
    }
  } while (incoming_connections_Iter != beginIt);

  // we did not find a preferred connection
  return getNextIncomingConnectionImpl(rel_guard);
}

void Processor::validateAnnotations() const {
  switch (getInputRequirement()) {
    case annotation::Input::INPUT_REQUIRED: {
      if (!hasIncomingConnections()) {
        throw Exception(PROCESS_SCHEDULE_EXCEPTION, "INPUT_REQUIRED was specified for the processor, but no incoming connections were found");
      }
      break;
    }
    case annotation::Input::INPUT_ALLOWED:
      break;
    case annotation::Input::INPUT_FORBIDDEN: {
      if (hasIncomingConnections()) {
        throw Exception(PROCESS_SCHEDULE_EXCEPTION, "INPUT_FORBIDDEN was specified for the processor, but there are incoming connections");
      }
    }
  }
}

std::string Processor::getInputRequirementAsString() const {
  switch (getInputRequirement()) {
    case annotation::Input::INPUT_REQUIRED:
      return "INPUT_REQUIRED";
    case annotation::Input::INPUT_ALLOWED:
      return "INPUT_ALLOWED";
    case annotation::Input::INPUT_FORBIDDEN:
      return "INPUT_FORBIDDEN";
  }

  return "ERROR_no_such_input_requirement";
}

void Processor::setMaxConcurrentTasks(const uint8_t tasks) {
  if (isSingleThreaded() && tasks > 1) {
    logger_->log_warn("Processor %s can not be run in parallel, its \"max concurrent tasks\" value is too high. "
                      "It was set to 1 from %" PRIu8 ".", name_, tasks);
    max_concurrent_tasks_ = 1;
    return;
  }

  max_concurrent_tasks_ = tasks;
}

void Processor::yield() {
  yield_expiration_ = std::chrono::system_clock::now() + yield_period_msec_.load();
}

void Processor::yield(std::chrono::milliseconds delta_time) {
  yield_expiration_ = std::chrono::system_clock::now() + delta_time;
}

bool Processor::isYield() {
  return yield_expiration_.load() >= std::chrono::system_clock::now();
}

void Processor::clearYield() {
  yield_expiration_ = std::chrono::system_clock::time_point();
}

std::chrono::milliseconds Processor::getYieldTime() const {
  auto yield_expiration = yield_expiration_.load();
  auto current_time = std::chrono::system_clock::now();
  if (yield_expiration > current_time)
    return std::chrono::duration_cast<std::chrono::milliseconds>(yield_expiration - current_time);
  else
    return 0ms;
}

}  // namespace core
}  // namespace minifi
}  // namespace nifi
}  // namespace apache
}  // namespace org
