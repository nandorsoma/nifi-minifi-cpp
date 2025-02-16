/**
 * @file TailFile.cpp
 * TailFile class implementation
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

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <unordered_map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "range/v3/action/sort.hpp"
#include "range/v3/range/conversion.hpp"
#include "range/v3/view/transform.hpp"

#include "FlowFileRecord.h"
#include "io/CRCStream.h"
#include "utils/file/FileUtils.h"
#include "utils/file/PathUtils.h"
#include "utils/TimeUtil.h"
#include "utils/StringUtils.h"
#include "utils/ProcessorConfigUtils.h"
#include "TextFragmentUtils.h"
#include "TailFile.h"
#include "core/ProcessContext.h"
#include "core/ProcessSession.h"
#include "core/Resource.h"
#include "utils/RegexUtils.h"


namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace processors {

const core::Property TailFile::FileName(
    core::PropertyBuilder::createProperty("File to Tail")
        ->withDescription("Fully-qualified filename of the file that should be tailed when using single file mode, or a file regex when using multifile mode")
        ->isRequired(true)
        ->build());

const core::Property TailFile::StateFile(
    core::PropertyBuilder::createProperty("State File")
        ->withDescription("DEPRECATED. Only use it for state migration from the legacy state file.")
        ->isRequired(false)
        ->withDefaultValue<std::string>("TailFileState")
        ->build());

const core::Property TailFile::Delimiter(
    core::PropertyBuilder::createProperty("Input Delimiter")
        ->withDescription("Specifies the character that should be used for delimiting the data being tailed"
         "from the incoming file. If none is specified, data will be ingested as it becomes available.")
        ->isRequired(false)
        ->withDefaultValue<std::string>("\\n")
        ->build());

const core::Property TailFile::TailMode(
    core::PropertyBuilder::createProperty("tail-mode", "Tailing Mode")
        ->withDescription("Specifies the tail file mode. In 'Single file' mode only a single file will be watched. "
        "In 'Multiple file' mode a regex may be used. Note that in multiple file mode we will still continue to watch for rollover on the initial set of watched files. "
        "The Regex used to locate multiple files will be run during the schedule phrase. Note that if rotated files are matched by the regex, those files will be tailed.")->isRequired(true)
        ->withAllowableValue<std::string>("Single file")->withAllowableValue("Multiple file")->withDefaultValue("Single file")
        ->build());

const core::Property TailFile::BaseDirectory(
    core::PropertyBuilder::createProperty("tail-base-directory", "Base Directory")
        ->withDescription("Base directory used to look for files to tail. This property is required when using Multiple file mode. "
                          "Can contain expression language placeholders if Attribute Provider Service is set.")
        ->isRequired(false)
        ->supportsExpressionLanguage(true)
        ->build());

const core::Property TailFile::RecursiveLookup(
    core::PropertyBuilder::createProperty("Recursive lookup")
        ->withDescription("When using Multiple file mode, this property determines whether files are tailed in "
        "child directories of the Base Directory or not.")
        ->isRequired(false)
        ->withDefaultValue<bool>(false)
        ->build());

const core::Property TailFile::LookupFrequency(
    core::PropertyBuilder::createProperty("Lookup frequency")
        ->withDescription("When using Multiple file mode, this property specifies the minimum duration "
        "the processor will wait between looking for new files to tail in the Base Directory.")
        ->isRequired(false)
        ->withDefaultValue<core::TimePeriodValue>("10 min")
        ->build());

const core::Property TailFile::RollingFilenamePattern(
    core::PropertyBuilder::createProperty("Rolling Filename Pattern")
        ->withDescription("If the file to tail \"rolls over\" as would be the case with log files, this filename pattern will be used to "
        "identify files that have rolled over so MiNiFi can read the remaining of the rolled-over file and then continue with the new log file. "
        "This pattern supports the wildcard characters * and ?, it also supports the notation ${filename} to specify a pattern based on the name of the file "
        "(without extension), and will assume that the files that have rolled over live in the same directory as the file being tailed.")
        ->isRequired(false)
        ->withDefaultValue<std::string>("${filename}.*")
        ->build());

const core::Property TailFile::InitialStartPosition(
    core::PropertyBuilder::createProperty("Initial Start Position")
        ->withDescription("When the Processor first begins to tail data, this property specifies where the Processor should begin reading data. "
                          "Once data has been ingested from a file, the Processor will continue from the last point from which it has received data.\n"
                          "Beginning of Time: Start with the oldest data that matches the Rolling Filename Pattern and then begin reading from the File to Tail.\n"
                          "Beginning of File: Start with the beginning of the File to Tail. Do not ingest any data that has already been rolled over.\n"
                          "Current Time: Start with the data at the end of the File to Tail. Do not ingest any data that has already been rolled over or "
                          "any data in the File to Tail that has already been written.")
        ->isRequired(true)
        ->withDefaultValue(toString(InitialStartPositions::BEGINNING_OF_FILE))
        ->withAllowableValues(InitialStartPositions::values())
        ->build());

const core::Property TailFile::AttributeProviderService(
    core::PropertyBuilder::createProperty("Attribute Provider Service")
        ->withDescription("Provides a list of key-value pair records which can be used in the Base Directory property using Expression Language. "
                          "Requires Multiple file mode.")
        ->asType<minifi::controllers::AttributeProviderService>()
        ->build());

const core::Relationship TailFile::Success("success", "All files are routed to success");

const char *TailFile::CURRENT_STR = "CURRENT.";
const char *TailFile::POSITION_STR = "POSITION.";

namespace {
template<typename Container, typename Key>
bool containsKey(const Container &container, const Key &key) {
  return container.find(key) != container.end();
}

template <typename Container, typename Key>
int64_t readOptionalInt64(const Container &container, const Key &key) {
  const auto it = container.find(key);
  if (it != container.end()) {
    return std::stoll(it->second);
  } else {
    return 0;
  }
}

template <typename Container, typename Key>
uint64_t readOptionalUint64(const Container &container, const Key &key) {
  const auto it = container.find(key);
  if (it != container.end()) {
    return std::stoull(it->second);
  } else {
    return 0;
  }
}

// the delimiter is the first character of the input, allowing some escape sequences
std::string parseDelimiter(const std::string &input) {
  if (input.empty()) return "";
  if (input[0] != '\\') return std::string{ input[0] };
  if (input.size() == std::size_t{1}) return "\\";
  switch (input[1]) {
    case 'r': return "\r";
    case 't': return "\t";
    case 'n': return "\n";
    default: return std::string{ input[1] };
  }
}

std::map<std::string, TailState> update_keys_in_legacy_states(const std::map<std::string, TailState> &legacy_tail_states) {
  std::map<std::string, TailState> new_tail_states;
  for (const auto &key_value_pair : legacy_tail_states) {
    const TailState &state = key_value_pair.second;
    std::string full_file_name = utils::file::concat_path(state.path_, state.file_name_);
    new_tail_states.emplace(full_file_name, state);
  }
  return new_tail_states;
}

void openFile(const std::string &file_name, uint64_t offset, std::ifstream &input_stream, const std::shared_ptr<core::logging::Logger> &logger) {
  logger->log_debug("Opening %s", file_name);
  input_stream.open(file_name.c_str(), std::fstream::in | std::fstream::binary);
  if (!input_stream.is_open() || !input_stream.good()) {
    input_stream.close();
    throw Exception(FILE_OPERATION_EXCEPTION, "Could not open file: " + file_name);
  }
  if (offset != 0U) {
    input_stream.seekg(offset, std::ifstream::beg);
    if (!input_stream.good()) {
      logger->log_error("Seeking to %" PRIu64 " failed for file %s (does file/filesystem support seeking?)", offset, file_name);
      throw Exception(FILE_OPERATION_EXCEPTION, "Could not seek file " + file_name + " to offset " + std::to_string(offset));
    }
  }
}

constexpr std::size_t BUFFER_SIZE = 4096;

class FileReaderCallback {
 public:
  FileReaderCallback(const std::string &file_name,
                     uint64_t offset,
                     char input_delimiter,
                     uint64_t checksum)
    : input_delimiter_(input_delimiter),
      checksum_(checksum) {
    openFile(file_name, offset, input_stream_, logger_);
  }

  int64_t operator()(const std::shared_ptr<io::BaseStream>& output_stream) {
    io::CRCStream<io::BaseStream> crc_stream{gsl::make_not_null(output_stream.get()), checksum_};

    uint64_t num_bytes_written = 0;
    bool found_delimiter = false;

    while (hasMoreToRead() && !found_delimiter) {
      if (begin_ == end_) {
        input_stream_.read(reinterpret_cast<char *>(buffer_.data()), gsl::narrow<std::streamsize>(buffer_.size()));

        const auto num_bytes_read = input_stream_.gcount();
        logger_->log_trace("Read %jd bytes of input", std::intmax_t{num_bytes_read});

        begin_ = buffer_.data();
        end_ = begin_ + num_bytes_read;
      }

      char *delimiter_pos = std::find(begin_, end_, input_delimiter_);
      found_delimiter = (delimiter_pos != end_);

      const auto zlen = gsl::narrow<size_t>(std::distance(begin_, delimiter_pos)) + (found_delimiter ? 1 : 0);
      crc_stream.write(reinterpret_cast<uint8_t*>(begin_), zlen);
      num_bytes_written += zlen;
      begin_ += zlen;
    }

    if (found_delimiter) {
      checksum_ = crc_stream.getCRC();
    } else {
      latest_flow_file_ends_with_delimiter_ = false;
    }

    return num_bytes_written;
  }

  uint64_t checksum() const {
    return checksum_;
  }

  bool hasMoreToRead() const {
    return begin_ != end_ || input_stream_.good();
  }

  bool useLatestFlowFile() const {
    return latest_flow_file_ends_with_delimiter_;
  }

 private:
  char input_delimiter_;
  uint64_t checksum_;
  std::ifstream input_stream_;
  std::shared_ptr<core::logging::Logger> logger_ = core::logging::LoggerFactory<TailFile>::getLogger();

  std::array<char, BUFFER_SIZE> buffer_;
  char *begin_ = buffer_.data();
  char *end_ = buffer_.data();

  bool latest_flow_file_ends_with_delimiter_ = true;
};

class WholeFileReaderCallback {
 public:
  WholeFileReaderCallback(const std::string &file_name,
                          uint64_t offset,
                          uint64_t checksum)
    : checksum_(checksum) {
    openFile(file_name, offset, input_stream_, logger_);
  }

  uint64_t checksum() const {
    return checksum_;
  }

  int64_t operator()(const std::shared_ptr<io::BaseStream>& output_stream) {
    std::array<char, BUFFER_SIZE> buffer;

    io::CRCStream<io::BaseStream> crc_stream{gsl::make_not_null(output_stream.get()), checksum_};

    uint64_t num_bytes_written = 0;

    while (input_stream_.good()) {
      input_stream_.read(buffer.data(), buffer.size());

      const auto num_bytes_read = input_stream_.gcount();
      logger_->log_trace("Read %jd bytes of input", std::intmax_t{num_bytes_read});

      const int len = gsl::narrow<int>(num_bytes_read);

      crc_stream.write(reinterpret_cast<uint8_t*>(buffer.data()), len);
      num_bytes_written += len;
    }

    checksum_ = crc_stream.getCRC();

    return num_bytes_written;
  }

 private:
  uint64_t checksum_;
  std::ifstream input_stream_;
  std::shared_ptr<core::logging::Logger> logger_ = core::logging::LoggerFactory<TailFile>::getLogger();
};
}  // namespace

void TailFile::initialize() {
  setSupportedProperties({
    FileName,
    StateFile,
    Delimiter,
    TailMode,
    BaseDirectory,
    RecursiveLookup,
    LookupFrequency,
    RollingFilenamePattern,
    InitialStartPosition,
    AttributeProviderService});
  setSupportedRelationships({Success});
}

void TailFile::onSchedule(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSessionFactory>& /*sessionFactory*/) {
  gsl_Expects(context);

  tail_states_.clear();

  state_manager_ = context->getStateManager();
  if (state_manager_ == nullptr) {
    throw Exception(PROCESSOR_EXCEPTION, "Failed to get StateManager");
  }

  std::string value;

  if (context->getProperty(Delimiter.getName(), value)) {
    delimiter_ = parseDelimiter(value);
  }

  context->getProperty(FileName.getName(), file_to_tail_);

  std::string mode;
  context->getProperty(TailMode.getName(), mode);

  if (mode == "Multiple file") {
    tail_mode_ = Mode::MULTIPLE;

    parseAttributeProviderServiceProperty(*context);

    if (!context->getProperty(BaseDirectory.getName(), base_dir_)) {
      throw minifi::Exception(ExceptionType::PROCESSOR_EXCEPTION, "Base directory is required for multiple tail mode.");
    }

    if (!attribute_provider_service_ && !utils::file::is_directory(base_dir_)) {
      throw minifi::Exception(ExceptionType::PROCESSOR_EXCEPTION, "Base directory does not exist or is not a directory");
    }

    context->getProperty(RecursiveLookup.getName(), recursive_lookup_);

    // NOTE:
    //   context->getProperty(LookupFrequency.getName(), lookup_frequency_);
    // is incorrect, as std::chrono::milliseconds::rep is unspecified, and may not be supported by getProperty()
    // (e.g. in clang/libc++, this underlying type is long long)
    int64_t lookup_frequency;
    if (context->getProperty(LookupFrequency.getName(), lookup_frequency)) {
      lookup_frequency_ = std::chrono::milliseconds{lookup_frequency};
    }

    recoverState(context);

    doMultifileLookup(*context);

  } else {
    tail_mode_ = Mode::SINGLE;

    std::string path, file_name;
    if (utils::file::getFileNameAndPath(file_to_tail_, path, file_name)) {
      // NOTE: position and checksum will be updated in recoverState() if there is a persisted state for this file
      tail_states_.emplace(file_to_tail_, TailState{path, file_name});
    } else {
      throw minifi::Exception(ExceptionType::PROCESSOR_EXCEPTION, "File to tail must be a fully qualified file");
    }

    recoverState(context);
  }

  std::string rolling_filename_pattern_glob;
  context->getProperty(RollingFilenamePattern.getName(), rolling_filename_pattern_glob);
  rolling_filename_pattern_ = utils::file::globToRegex(rolling_filename_pattern_glob);
  initial_start_position_ = InitialStartPositions{utils::parsePropertyWithAllowableValuesOrThrow(*context, InitialStartPosition.getName(), InitialStartPositions::values())};
}

void TailFile::parseAttributeProviderServiceProperty(core::ProcessContext& context) {
  const auto attribute_provider_service_name = context.getProperty(AttributeProviderService);
  if (!attribute_provider_service_name || attribute_provider_service_name->empty()) {
    return;
  }

  std::shared_ptr<core::controller::ControllerService> controller_service = context.getControllerService(*attribute_provider_service_name);
  if (!controller_service) {
    throw minifi::Exception{ExceptionType::PROCESS_SCHEDULE_EXCEPTION, utils::StringUtils::join_pack("Controller service '", *attribute_provider_service_name, "' not found")};
  }

  // we drop ownership of the service here -- in the long term, getControllerService() should return a non-owning pointer or optional reference
  attribute_provider_service_ = dynamic_cast<minifi::controllers::AttributeProviderService*>(controller_service.get());
  if (!attribute_provider_service_) {
    throw minifi::Exception{ExceptionType::PROCESS_SCHEDULE_EXCEPTION, utils::StringUtils::join_pack("Controller service '", *attribute_provider_service_name, "' is not an AttributeProviderService")};
  }
}

void TailFile::parseStateFileLine(char *buf, std::map<std::string, TailState> &state) const {
  char *line = buf;

  logger_->log_trace("Received line %s", buf);

  while ((line[0] == ' ') || (line[0] == '\t'))
    ++line;

  char first = line[0];
  if ((first == '\0') || (first == '#') || (first == '\r') || (first == '\n') || (first == '=')) {
    return;
  }

  char *equal = strchr(line, '=');
  if (equal == nullptr) {
    return;
  }

  equal[0] = '\0';
  std::string key = line;

  equal++;
  while ((equal[0] == ' ') || (equal[0] == '\t'))
    ++equal;

  first = equal[0];
  if ((first == '\0') || (first == '\r') || (first == '\n')) {
    return;
  }

  std::string value = equal;
  key = utils::StringUtils::trimRight(key);
  value = utils::StringUtils::trimRight(value);

  if (key == "FILENAME") {
    std::string fileLocation, fileName;
    if (utils::file::getFileNameAndPath(value, fileLocation, fileName)) {
      logger_->log_debug("State migration received path %s, file %s", fileLocation, fileName);
      state.emplace(fileName, TailState{fileLocation, fileName});
    } else {
      state.emplace(value, TailState{fileLocation, value});
    }
  }
  if (key == "POSITION") {
    // for backwards compatibility
    if (tail_states_.size() != std::size_t{1}) {
      throw minifi::Exception(ExceptionType::PROCESSOR_EXCEPTION, "Incompatible state file types");
    }
    const auto position = std::stoull(value);
    logger_->log_debug("Received position %llu", position);
    state.begin()->second.position_ = gsl::narrow<uint64_t>(position);
  }
  if (key.find(CURRENT_STR) == 0) {
    const auto file = key.substr(strlen(CURRENT_STR));
    std::string fileLocation, fileName;
    if (utils::file::getFileNameAndPath(value, fileLocation, fileName)) {
      state[file].path_ = fileLocation;
      state[file].file_name_ = fileName;
    } else {
      throw minifi::Exception(ExceptionType::PROCESSOR_EXCEPTION, "State file contains an invalid file name");
    }
  }

  if (key.find(POSITION_STR) == 0) {
    const auto file = key.substr(strlen(POSITION_STR));
    state[file].position_ = std::stoull(value);
  }
}

bool TailFile::recoverState(const std::shared_ptr<core::ProcessContext>& context) {
  std::map<std::string, TailState> new_tail_states;
  bool state_load_success = getStateFromStateManager(new_tail_states) ||
                            getStateFromLegacyStateFile(context, new_tail_states);
  if (!state_load_success) {
    return false;
  }

  if (tail_mode_ == Mode::SINGLE) {
    if (tail_states_.size() == 1) {
      auto state_it = tail_states_.begin();
      const auto it = new_tail_states.find(state_it->first);
      if (it != new_tail_states.end()) {
        state_it->second = it->second;
      }
    } else {
      throw minifi::Exception(ExceptionType::PROCESSOR_EXCEPTION, "This should never happen: "
          "in Single file mode, internal state size should be 1, but it is " + std::to_string(tail_states_.size()));
    }
  } else {
    tail_states_ = std::move(new_tail_states);
  }

  logState();
  storeState();

  return true;
}

bool TailFile::getStateFromStateManager(std::map<std::string, TailState> &new_tail_states) const {
  std::unordered_map<std::string, std::string> state_map;
  if (state_manager_->get(state_map)) {
    for (size_t i = 0U;; ++i) {
      if (state_map.find("file." + std::to_string(i) + ".name") == state_map.end()) {
        break;
      }
      try {
        const std::string& current = state_map.at("file." + std::to_string(i) + ".current");
        uint64_t position = std::stoull(state_map.at("file." + std::to_string(i) + ".position"));
        uint64_t checksum = readOptionalUint64(state_map, "file." + std::to_string(i) + ".checksum");
        std::chrono::file_clock::time_point last_read_time{std::chrono::milliseconds{
            readOptionalInt64(state_map, "file." + std::to_string(i) + ".last_read_time")
        }};

        std::string fileLocation, fileName;
        if (utils::file::getFileNameAndPath(current, fileLocation, fileName)) {
          logger_->log_debug("Received path %s, file %s", fileLocation, fileName);
          new_tail_states.emplace(current, TailState{fileLocation, fileName, position, last_read_time, checksum});
        } else {
          new_tail_states.emplace(current, TailState{fileLocation, current, position, last_read_time, checksum});
        }
      } catch (...) {
        continue;
      }
    }
    for (const auto& s : tail_states_) {
      logger_->log_debug("TailState %s: %s, %s, %" PRIu64 ", %" PRIu64,
                         s.first, s.second.path_, s.second.file_name_, s.second.position_, s.second.checksum_);
    }
    return true;
  } else {
    logger_->log_info("Found no stored state");
  }
  return false;
}

bool TailFile::getStateFromLegacyStateFile(const std::shared_ptr<core::ProcessContext>& context,
                                           std::map<std::string, TailState> &new_tail_states) const {
  std::string state_file_name_property;
  context->getProperty(StateFile.getName(), state_file_name_property);
  std::string state_file = state_file_name_property + "." + getUUIDStr();

  std::ifstream file(state_file.c_str(), std::ifstream::in);
  if (!file.good()) {
    logger_->log_info("Legacy state file %s not found (this is OK)", state_file);
    return false;
  }

  std::map<std::string, TailState> legacy_tail_states;
  char buf[BUFFER_SIZE];
  for (file.getline(buf, BUFFER_SIZE); file.good(); file.getline(buf, BUFFER_SIZE)) {
    parseStateFileLine(buf, legacy_tail_states);
  }

  new_tail_states = update_keys_in_legacy_states(legacy_tail_states);
  return true;
}

void TailFile::logState() {
  logger_->log_info("State of the TailFile processor %s:", name_);
  for (const auto& [key, value] : tail_states_) {
    core::logging::LOG_INFO(logger_) << key << " => { " << value << " }";
  }
}

std::ostream& operator<<(std::ostream &os, const TailState &tail_state) {
  os << "name: " << tail_state.file_name_
      << ", position: " << tail_state.position_
      << ", checksum: " << tail_state.checksum_
      << ", last_read_time: " << tail_state.lastReadTimeInMilliseconds();
  return os;
}

bool TailFile::storeState() {
  std::unordered_map<std::string, std::string> state;
  size_t i = 0;
  for (const auto& tail_state : tail_states_) {
    state["file." + std::to_string(i) + ".current"] = tail_state.first;
    state["file." + std::to_string(i) + ".name"] = tail_state.second.file_name_;
    state["file." + std::to_string(i) + ".position"] = std::to_string(tail_state.second.position_);
    state["file." + std::to_string(i) + ".checksum"] = std::to_string(tail_state.second.checksum_);
    state["file." + std::to_string(i) + ".last_read_time"] = std::to_string(tail_state.second.lastReadTimeInMilliseconds());
    ++i;
  }
  if (!state_manager_->set(state)) {
    logger_->log_error("Failed to set state");
    return false;
  }
  return true;
}

std::string TailFile::parseRollingFilePattern(const TailState &state) const {
  std::size_t last_dot_position = state.file_name_.find_last_of('.');
  std::string base_name = state.file_name_.substr(0, last_dot_position);
  return utils::StringUtils::replaceOne(rolling_filename_pattern_, "${filename}", base_name);
}

std::vector<TailState> TailFile::findAllRotatedFiles(const TailState &state) const {
  logger_->log_debug("Searching for all files rolled over");

  std::string pattern = parseRollingFilePattern(state);

  std::vector<TailStateWithMtime> matched_files_with_mtime;
  auto collect_matching_files = [&](const std::string &path, const std::string &file_name) -> bool {
    utils::Regex pattern_regex(pattern);
    if (file_name != state.file_name_ && utils::regexMatch(file_name, pattern_regex)) {
      std::string full_file_name = path + utils::file::get_separator() + file_name;
      TailStateWithMtime::TimePoint mtime{utils::file::last_write_time_point(full_file_name)};
      logger_->log_debug("File %s with mtime %" PRId64 " matches rolling filename pattern %s, so we are reading it", file_name, int64_t{mtime.time_since_epoch().count()}, pattern);
      matched_files_with_mtime.emplace_back(TailState{path, file_name}, mtime);
    }
    return true;
  };

  utils::file::list_dir(state.path_, collect_matching_files, logger_, false);

  return sortAndSkipMainFilePrefix(state, matched_files_with_mtime);
}

std::vector<TailState> TailFile::findRotatedFilesAfterLastReadTime(const TailState &state) const {
  logger_->log_debug("Searching for files rolled over after last read time: %" PRId64, state.lastReadTimeInMilliseconds());

  std::string pattern = parseRollingFilePattern(state);

  std::vector<TailStateWithMtime> matched_files_with_mtime;
  auto collect_matching_files = [&](const std::string &path, const std::string &file_name) -> bool {
    utils::Regex pattern_regex(pattern);
    if (file_name != state.file_name_ && utils::regexMatch(file_name, pattern_regex)) {
      std::string full_file_name = path + utils::file::get_separator() + file_name;
      TailStateWithMtime::TimePoint mtime{utils::file::last_write_time_point(full_file_name)};
      logger_->log_debug("File %s with mtime %" PRId64 " matches rolling filename pattern %s", file_name, int64_t{mtime.time_since_epoch().count()}, pattern);
      if (mtime >= std::chrono::time_point_cast<std::chrono::seconds>(state.last_read_time_)) {
        logger_->log_debug("File %s has mtime >= last read time, so we are going to read it", file_name);
        matched_files_with_mtime.emplace_back(TailState{path, file_name}, mtime);
      }
    }
    return true;
  };

  utils::file::list_dir(state.path_, collect_matching_files, logger_, false);

  return sortAndSkipMainFilePrefix(state, matched_files_with_mtime);
}

std::vector<TailState> TailFile::sortAndSkipMainFilePrefix(const TailState &state, std::vector<TailStateWithMtime>& matched_files_with_mtime) const {
  const auto first_by_mtime_then_by_name = [](const auto& left, const auto& right) {
    return std::tie(left.mtime_, left.tail_state_.file_name_) <
           std::tie(right.mtime_, right.tail_state_.file_name_);
  };
  matched_files_with_mtime |= ranges::actions::sort(first_by_mtime_then_by_name);

  if (!matched_files_with_mtime.empty() && state.position_ > 0) {
    TailState &first_rotated_file = matched_files_with_mtime[0].tail_state_;
    std::string full_file_name = first_rotated_file.fileNameWithPath();
    if (utils::file::file_size(full_file_name) >= state.position_) {
      uint64_t checksum = utils::file::computeChecksum(full_file_name, state.position_);
      if (checksum == state.checksum_) {
        first_rotated_file.position_ = state.position_;
        first_rotated_file.checksum_ = state.checksum_;
      }
    }
  }

  std::vector<TailState> matched_files;
  matched_files.reserve(matched_files_with_mtime.size());
  std::transform(matched_files_with_mtime.begin(), matched_files_with_mtime.end(), std::back_inserter(matched_files),
                 [](TailStateWithMtime &tail_state_with_mtime) { return std::move(tail_state_with_mtime.tail_state_); });
  return matched_files;
}

void TailFile::onTrigger(const std::shared_ptr<core::ProcessContext>& context, const std::shared_ptr<core::ProcessSession>& session) {
  gsl_Expects(context && session);

  if (tail_mode_ == Mode::MULTIPLE) {
    if (last_multifile_lookup_ + lookup_frequency_ < std::chrono::steady_clock::now()) {
      logger_->log_debug("Lookup frequency %" PRId64 " ms have elapsed, doing new multifile lookup", int64_t{lookup_frequency_.count()});
      doMultifileLookup(*context);
    } else {
      logger_->log_trace("Skipping multifile lookup");
    }
  }

  // iterate over file states. may modify them
  for (auto &state : tail_states_) {
    processFile(session, state.first, state.second);
  }

  if (!session->existsFlowFileInRelationship(Success)) {
    yield();
  }

  first_trigger_ = false;
}

bool TailFile::isOldFileInitiallyRead(TailState &state) const {
  // This is our initial processing and no stored state was found
  return first_trigger_ && state.last_read_time_ == std::chrono::file_clock::time_point{};
}

void TailFile::processFile(const std::shared_ptr<core::ProcessSession> &session,
                           const std::string &full_file_name,
                           TailState &state) {
  if (isOldFileInitiallyRead(state)) {
    if (initial_start_position_ == InitialStartPositions::BEGINNING_OF_TIME) {
      processAllRotatedFiles(session, state);
    } else if (initial_start_position_ == InitialStartPositions::CURRENT_TIME) {
      state.position_ = utils::file::file_size(full_file_name);
      state.last_read_time_ = std::chrono::file_clock::now();
      state.checksum_ = utils::file::computeChecksum(full_file_name, state.position_);
      storeState();
      return;
    }
  } else {
    uint64_t fsize = utils::file::file_size(full_file_name);
    if (fsize < state.position_) {
      processRotatedFilesAfterLastReadTime(session, state);
    } else if (fsize == state.position_) {
      logger_->log_trace("Skipping file %s as its size hasn't changed since last read", state.file_name_);
      return;
    }
  }

  processSingleFile(session, full_file_name, state);
  storeState();
}

void TailFile::processRotatedFilesAfterLastReadTime(const std::shared_ptr<core::ProcessSession> &session, TailState &state) {
  std::vector<TailState> rotated_file_states = findRotatedFilesAfterLastReadTime(state);
  processRotatedFiles(session, state, rotated_file_states);
}

void TailFile::processAllRotatedFiles(const std::shared_ptr<core::ProcessSession> &session, TailState &state) {
  std::vector<TailState> rotated_file_states = findAllRotatedFiles(state);
  processRotatedFiles(session, state, rotated_file_states);
}

void TailFile::processRotatedFiles(const std::shared_ptr<core::ProcessSession> &session, TailState &state, std::vector<TailState> &rotated_file_states) {
  for (TailState &file_state : rotated_file_states) {
    processSingleFile(session, file_state.fileNameWithPath(), file_state);
  }
  state.position_ = 0;
  state.checksum_ = 0;
}

void TailFile::processSingleFile(const std::shared_ptr<core::ProcessSession> &session,
                                 const std::string &full_file_name,
                                 TailState &state) {
  std::string fileName = state.file_name_;

  if (utils::file::file_size(full_file_name) == 0u) {
    logger_->log_warn("Unable to read file %s as it does not exist or has size zero", full_file_name);
    return;
  }
  logger_->log_debug("Tailing file %s from %" PRIu64, full_file_name, state.position_);

  std::size_t last_dot_position = fileName.find_last_of('.');
  std::string baseName = fileName.substr(0, last_dot_position);
  std::string extension = fileName.substr(last_dot_position + 1);

  if (!delimiter_.empty()) {
    char delim = delimiter_[0];
    logger_->log_trace("Looking for delimiter 0x%X", delim);

    std::size_t num_flow_files = 0;
    FileReaderCallback file_reader{full_file_name, state.position_, delim, state.checksum_};
    TailState state_copy{state};

    while (file_reader.hasMoreToRead()) {
      auto flow_file = session->create();
      session->write(flow_file, std::ref(file_reader));

      if (file_reader.useLatestFlowFile()) {
        updateFlowFileAttributes(full_file_name, state_copy, fileName, baseName, extension, flow_file);
        session->transfer(flow_file, Success);
        updateStateAttributes(state_copy, flow_file->getSize(), file_reader.checksum());

        ++num_flow_files;

      } else {
        session->remove(flow_file);
      }
    }

    state = state_copy;
    logger_->log_info("%zu flowfiles were received from TailFile input", num_flow_files);

  } else {
    WholeFileReaderCallback file_reader{full_file_name, state.position_, state.checksum_};
    auto flow_file = session->create();
    session->write(flow_file, std::ref(file_reader));

    updateFlowFileAttributes(full_file_name, state, fileName, baseName, extension, flow_file);
    session->transfer(flow_file, Success);
    updateStateAttributes(state, flow_file->getSize(), file_reader.checksum());
  }
}

void TailFile::updateFlowFileAttributes(const std::string &full_file_name, const TailState &state,
                                        const std::string &fileName, const std::string &baseName,
                                        const std::string &extension,
                                        std::shared_ptr<core::FlowFile> &flow_file) const {
  logger_->log_info("TailFile %s for %" PRIu64 " bytes", fileName, flow_file->getSize());
  std::string logName = textfragmentutils::createFileName(baseName, extension, state.position_, flow_file->getSize());
  flow_file->setAttribute(core::SpecialFlowAttribute::PATH, state.path_);
  flow_file->addAttribute(core::SpecialFlowAttribute::ABSOLUTE_PATH, full_file_name);
  flow_file->setAttribute(core::SpecialFlowAttribute::FILENAME, logName);

  flow_file->setAttribute(textfragmentutils::BASE_NAME_ATTRIBUTE, baseName);
  flow_file->setAttribute(textfragmentutils::POST_NAME_ATTRIBUTE, extension);
  flow_file->setAttribute(textfragmentutils::OFFSET_ATTRIBUTE, std::to_string(state.position_));

  if (extra_attributes_.contains(state.path_)) {
    std::string prefix;
    if (attribute_provider_service_) {
      prefix = std::string(attribute_provider_service_->name()) + ".";
    }
    for (const auto& [key, value] : extra_attributes_.at(state.path_)) {
      flow_file->setAttribute(prefix + key, value);
    }
  }
}

void TailFile::updateStateAttributes(TailState &state, uint64_t size, uint64_t checksum) const {
  state.position_ += size;
  state.last_read_time_ = std::chrono::file_clock::now();
  state.checksum_ = checksum;
}

void TailFile::doMultifileLookup(core::ProcessContext& context) {
  checkForRemovedFiles();
  checkForNewFiles(context);
  last_multifile_lookup_ = std::chrono::steady_clock::now();
}

void TailFile::checkForRemovedFiles() {
  std::vector<std::string> file_names_to_remove;

  for (const auto &kv : tail_states_) {
    const std::string &full_file_name = kv.first;
    const TailState &state = kv.second;
    utils::Regex pattern_regex(file_to_tail_);
    if (utils::file::file_size(state.fileNameWithPath()) == 0u ||
        !utils::regexMatch(state.file_name_, pattern_regex)) {
      file_names_to_remove.push_back(full_file_name);
    }
  }

  for (const auto &full_file_name : file_names_to_remove) {
    tail_states_.erase(full_file_name);
  }
}

void TailFile::checkForNewFiles(core::ProcessContext& context) {
  auto add_new_files_callback = [&](const std::string &path, const std::string &file_name) -> bool {
    std::string full_file_name = path + utils::file::get_separator() + file_name;
    utils::Regex file_to_tail_regex(file_to_tail_);
    if (!containsKey(tail_states_, full_file_name) && utils::regexMatch(file_name, file_to_tail_regex)) {
      tail_states_.emplace(full_file_name, TailState{path, file_name});
    }
    return true;
  };

  if (!attribute_provider_service_) {
    utils::file::list_dir(base_dir_, add_new_files_callback, logger_, recursive_lookup_);
    return;
  }

  const auto attribute_maps = attribute_provider_service_->getAttributes();
  if (!attribute_maps) {
    logger_->log_error("Could not get attributes from the Attribute Provider Service");
    return;
  }

  for (const auto& attribute_map : *attribute_maps) {
    std::string base_dir = baseDirectoryFromAttributes(attribute_map, context);
    extra_attributes_[base_dir] = attribute_map;
    utils::file::list_dir(base_dir, add_new_files_callback, logger_, recursive_lookup_);
  }
}

std::string TailFile::baseDirectoryFromAttributes(const controllers::AttributeProviderService::AttributeMap& attribute_map, core::ProcessContext& context) const {
  auto flow_file = std::make_shared<FlowFileRecord>();
  for (const auto& [key, value] : attribute_map) {
    flow_file->setAttribute(key, value);
  }
  return context.getProperty(BaseDirectory, flow_file).value();
}

std::chrono::milliseconds TailFile::getLookupFrequency() const {
  return lookup_frequency_;
}

REGISTER_RESOURCE(TailFile, "\"Tails\" a file, or a list of files, ingesting data from the file as it is written to the file. The file is expected to be textual."
                  " Data is ingested only when a new line is encountered (carriage return or new-line character or combination). If the file to tail is periodically \"rolled over\","
                  " as is generally the case with log files, an optional Rolling Filename Pattern can be used to retrieve data from files that have rolled over, even if the rollover"
                  " occurred while NiFi was not running (provided that the data still exists upon restart of NiFi). It is generally advisable to set the Run Schedule to a few seconds,"
                  " rather than running with the default value of 0 secs, as this Processor will consume a lot of resources if scheduled very aggressively. At this time, this Processor"
                  " does not support ingesting files that have been compressed when 'rolled over'.");

}  // namespace processors
}  // namespace minifi
}  // namespace nifi
}  // namespace apache
}  // namespace org
