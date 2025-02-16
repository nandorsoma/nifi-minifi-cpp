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


#include <type_traits> //NOLINT
#include <sys/stat.h> //NOLINT
#include <chrono> //NOLINT
#include <thread> //NOLINT
#undef NDEBUG
#include <cassert>
#include <string>
#include <utility>
#include <memory>
#include <vector>
#include <fstream>
#include "core/repository/VolatileContentRepository.h"
#include "unit/ProvenanceTestHelper.h"
#include "FlowController.h"
#include "processors/GetFile.h"
#include "core/Core.h"
#include "core/FlowFile.h"
#include "core/Processor.h"
#include "core/controller/ControllerServiceNode.h"
#include "core/controller/ControllerServiceProvider.h"
#include "processors/ExecuteProcess.h"
#include "core/ProcessContext.h"
#include "core/ProcessSession.h"
#include "core/ProcessorNode.h"
#include "TestBase.h"
#include "Catch.h"

int main(int /*argc*/, char ** /*argv*/) {
#ifndef WIN32
  TestController testController;
  std::shared_ptr<core::Processor> processor = std::make_shared<org::apache::nifi::minifi::processors::ExecuteProcess>("executeProcess");
  processor->setMaxConcurrentTasks(1);

  std::shared_ptr<core::Repository> test_repo =
      std::make_shared<TestRepository>();
  std::shared_ptr<core::ContentRepository> content_repo = std::make_shared<core::repository::VolatileContentRepository>();
  std::shared_ptr<TestRepository> repo =
      std::static_pointer_cast<TestRepository>(test_repo);
  std::shared_ptr<minifi::FlowController> controller =
      std::make_shared<TestFlowController>(test_repo, test_repo, content_repo);

  utils::Identifier processoruuid = processor->getUUID();
  assert(processoruuid);
  auto connection = std::make_unique<minifi::Connection>(test_repo, content_repo, "executeProcessConnection");
  connection->addRelationship(core::Relationship("success", "description"));

  // link the connections so that we can test results at the end for this
  connection->setSource(processor.get());
  connection->setDestination(processor.get());

  connection->setSourceUUID(processoruuid);
  connection->setDestinationUUID(processoruuid);

  processor->addConnection(connection.get());
  assert(processor->getName() == "executeProcess");

  std::shared_ptr<core::FlowFile> record;
  processor->setScheduledState(core::ScheduledState::RUNNING);

  processor->initialize();

  std::atomic<bool> is_ready(false);

  std::vector<std::thread> processor_workers;

  auto node2 = std::make_shared<core::ProcessorNode>(processor.get());
  auto contextset = std::make_shared<core::ProcessContext>(node2, nullptr, test_repo, test_repo);
  core::ProcessSessionFactory factory(contextset);
  processor->onSchedule(contextset.get(), &factory);

  for (int i = 0; i < 1; i++) {
    processor_workers.push_back(std::thread([processor, test_repo, &is_ready]() {
      auto node = std::make_shared<core::ProcessorNode>(processor.get());
      auto context = std::make_shared<core::ProcessContext>(node, nullptr, test_repo, test_repo);
      context->setProperty(org::apache::nifi::minifi::processors::ExecuteProcess::Command, "sleep 0.5");
      auto session = std::make_shared<core::ProcessSession>(context);
      while (!is_ready.load(std::memory_order_relaxed)) {
      }
      processor->onTrigger(context.get(), session.get());
    }));
  }

  is_ready.store(true, std::memory_order_relaxed);

  std::for_each(processor_workers.begin(), processor_workers.end(), [](std::thread &t) {
    t.join();
  });

  auto execp = std::static_pointer_cast<org::apache::nifi::minifi::processors::ExecuteProcess>(processor);
#endif
}
