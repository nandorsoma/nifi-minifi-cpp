/**
 * @file FocusArchiveEntry.h
 * FocusArchiveEntry class declaration
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

#include "archive.h"

#include "ArchiveMetadata.h"
#include "FlowFileRecord.h"
#include "core/Processor.h"
#include "core/ProcessSession.h"
#include "core/Core.h"
#include "core/logging/LoggerConfiguration.h"
#include "utils/file/FileManager.h"
#include "utils/Export.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace processors {

//! FocusArchiveEntry Class
class FocusArchiveEntry : public core::Processor {
 public:
  //! Constructor
  /*!
   * Create a new processor
   */
  explicit FocusArchiveEntry(const std::string& name, const utils::Identifier& uuid = {})
  : core::Processor(name, uuid) {
  }
  //! Destructor
  ~FocusArchiveEntry()   override = default;
  //! Processor Name
  EXTENSIONAPI static constexpr char const* ProcessorName = "FocusArchiveEntry";
  //! Supported Properties
  EXTENSIONAPI static core::Property Path;
  //! Supported Relationships
  EXTENSIONAPI static core::Relationship Success;

  //! OnTrigger method, implemented by NiFi FocusArchiveEntry
  void onTrigger(core::ProcessContext *context,
      core::ProcessSession *session) override;
  //! Initialize, over write by NiFi FocusArchiveEntry
  void initialize() override;

  class ReadCallback {
   public:
    explicit ReadCallback(core::Processor*, utils::file::FileManager *file_man, ArchiveMetadata *archiveMetadata);
    int64_t operator()(const std::shared_ptr<io::BaseStream>& stream) const;
    bool isRunning() {return proc_->isRunning();}

   private:
    utils::file::FileManager *file_man_;
    core::Processor * const proc_;
    std::shared_ptr<core::logging::Logger> logger_ = core::logging::LoggerFactory<FocusArchiveEntry>::getLogger();
    ArchiveMetadata *_archiveMetadata;
    static int ok_cb(struct archive *, void* /*d*/) { return ARCHIVE_OK; }
    static la_ssize_t read_cb(struct archive * a, void *d, const void **buf);
  };

 private:
  //! Logger
  std::shared_ptr<core::logging::Logger> logger_ = core::logging::LoggerFactory<FocusArchiveEntry>::getLogger();
  static std::shared_ptr<utils::IdGenerator> id_generator_;
};

} /* namespace processors */
} /* namespace minifi */
} /* namespace nifi */
} /* namespace apache */
} /* namespace org */
