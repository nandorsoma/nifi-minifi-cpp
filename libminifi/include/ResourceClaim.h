/**
 * @file ResourceClaim.h
 * Resource Claim class declaration
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
#ifndef LIBMINIFI_INCLUDE_RESOURCECLAIM_H_
#define LIBMINIFI_INCLUDE_RESOURCECLAIM_H_

#include <string>
#include <vector>
#include <queue>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include "core/Core.h"
#include "core/StreamManager.h"
#include "properties/Configure.h"
#include "utils/Id.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {

// Default content directory
#define DEFAULT_CONTENT_DIRECTORY "./content_repository"

extern std::string default_directory_path;

extern void setDefaultDirectory(std::string);

// ResourceClaim Class
class ResourceClaim {
 public:
  // the type which uniquely represents the resource for the owning manager
  using Path = std::string;
  // Constructor
  /*!
   * Create a new resource claim
   */
  // explicit ResourceClaim(std::shared_ptr<core::StreamManager<ResourceClaim>> claim_manager, const std::string contentDirectory);

  explicit ResourceClaim(std::shared_ptr<core::StreamManager<ResourceClaim>> claim_manager);

  explicit ResourceClaim(const Path& path, std::shared_ptr<core::StreamManager<ResourceClaim>> claim_manager);

  // Destructor
  ~ResourceClaim();
  // increaseFlowFileRecordOwnedCount
  void increaseFlowFileRecordOwnedCount() {
    claim_manager_->incrementStreamCount(*this);
  }
  // decreaseFlowFileRecordOwenedCount
  void decreaseFlowFileRecordOwnedCount() {
    claim_manager_->decrementStreamCount(*this);
  }
  // getFlowFileRecordOwenedCount
  uint64_t getFlowFileRecordOwnedCount() {
    return claim_manager_->getStreamCount(*this);
  }
  // Get the content full path
  Path getContentFullPath() const {
    return _contentFullPath;
  }

  bool exists() {
    if (claim_manager_ == nullptr) {
      return false;
    }
    return claim_manager_->exists(*this);
  }

  friend std::ostream& operator<<(std::ostream& stream, const ResourceClaim& claim) {
    stream << claim._contentFullPath;
    return stream;
  }

  friend std::ostream& operator<<(std::ostream& stream, const std::shared_ptr<ResourceClaim>& claim) {
    stream << claim->_contentFullPath;
    return stream;
  }

 protected:
  // Full path to the content
  const Path _contentFullPath;

  std::shared_ptr<core::StreamManager<ResourceClaim>> claim_manager_;

 private:
  // Logger
  std::shared_ptr<core::logging::Logger> logger_;
  // Prevent default copy constructor and assignment operation
  // Only support pass by reference or pointer
  ResourceClaim(const ResourceClaim &parent);
  ResourceClaim &operator=(const ResourceClaim &parent);

  static utils::NonRepeatingStringGenerator non_repeating_string_generator_;
};

}  // namespace minifi
}  // namespace nifi
}  // namespace apache
}  // namespace org
#endif  // LIBMINIFI_INCLUDE_RESOURCECLAIM_H_
