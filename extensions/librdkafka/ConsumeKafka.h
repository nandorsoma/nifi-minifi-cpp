/**
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
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "KafkaProcessorBase.h"
#include "core/logging/LoggerConfiguration.h"
#include "io/StreamPipe.h"
#include "rdkafka.h"
#include "rdkafka_utils.h"
#include "KafkaConnection.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {

class FlowFileRecord;

namespace processors {

class ConsumeKafka : public KafkaProcessorBase {
 public:
  EXTENSIONAPI static constexpr char const* ProcessorName = "ConsumeKafka";

  // Supported Properties
  EXTENSIONAPI static core::Property KafkaBrokers;
  EXTENSIONAPI static core::Property TopicNames;
  EXTENSIONAPI static core::Property TopicNameFormat;
  EXTENSIONAPI static core::Property HonorTransactions;
  EXTENSIONAPI static core::Property GroupID;
  EXTENSIONAPI static core::Property OffsetReset;
  EXTENSIONAPI static core::Property KeyAttributeEncoding;
  EXTENSIONAPI static core::Property MessageDemarcator;
  EXTENSIONAPI static core::Property MessageHeaderEncoding;
  EXTENSIONAPI static core::Property HeadersToAddAsAttributes;
  EXTENSIONAPI static core::Property DuplicateHeaderHandling;
  EXTENSIONAPI static core::Property MaxPollRecords;
  EXTENSIONAPI static core::Property MaxPollTime;
  EXTENSIONAPI static core::Property SessionTimeout;

  // Supported Relationships
  EXTENSIONAPI static const core::Relationship Success;

  // Security Protocol allowable values
  static constexpr char const* SECURITY_PROTOCOL_PLAINTEXT = "plaintext";
  static constexpr char const* SECURITY_PROTOCOL_SSL = "ssl";

  // Topic Name Format allowable values
  static constexpr char const* TOPIC_FORMAT_NAMES = "Names";
  static constexpr char const* TOPIC_FORMAT_PATTERNS = "Patterns";

  // Offset Reset allowable values
  static constexpr char const* OFFSET_RESET_EARLIEST = "earliest";
  static constexpr char const* OFFSET_RESET_LATEST = "latest";
  static constexpr char const* OFFSET_RESET_NONE = "none";

  // Key Attribute Encoding allowable values
  static constexpr char const* KEY_ATTR_ENCODING_UTF_8 = "UTF-8";
  static constexpr char const* KEY_ATTR_ENCODING_HEX = "Hex";

  // Message Header Encoding allowable values
  static constexpr char const* MSG_HEADER_ENCODING_UTF_8 = "UTF-8";
  static constexpr char const* MSG_HEADER_ENCODING_HEX = "Hex";

  // Duplicate Header Handling allowable values
  static constexpr char const* MSG_HEADER_KEEP_FIRST = "Keep First";
  static constexpr char const* MSG_HEADER_KEEP_LATEST = "Keep Latest";
  static constexpr char const* MSG_HEADER_COMMA_SEPARATED_MERGE = "Comma-separated Merge";

  // Flowfile attributes written
  static constexpr char const* KAFKA_COUNT_ATTR = "kafka.count";  // Always 1 until we start supporting merging from batches
  static constexpr char const* KAFKA_MESSAGE_KEY_ATTR = "kafka.key";
  static constexpr char const* KAFKA_OFFSET_ATTR = "kafka.offset";
  static constexpr char const* KAFKA_PARTITION_ATTR = "kafka.partition";
  static constexpr char const* KAFKA_TOPIC_ATTR = "kafka.topic";

  static constexpr const std::size_t DEFAULT_MAX_POLL_RECORDS{ 10000 };
  static constexpr char const* DEFAULT_MAX_POLL_TIME = "4 seconds";
  static constexpr const std::size_t METADATA_COMMUNICATIONS_TIMEOUT_MS{ 60000 };

  explicit ConsumeKafka(const std::string& name, const utils::Identifier& uuid = utils::Identifier()) :
      KafkaProcessorBase(name, uuid, core::logging::LoggerFactory<ConsumeKafka>::getLogger()) {}

  ~ConsumeKafka() override = default;

 public:
  bool supportsDynamicProperties() override {
    return true;
  }
  /**
   * Function that's executed when the processor is scheduled.
   * @param context process context.
   * @param sessionFactory process session factory that is used when creating
   * ProcessSession objects.
   */
  void onSchedule(core::ProcessContext* context, core::ProcessSessionFactory* /* sessionFactory */) override;
  /**
   * Execution trigger for the RetryFlowFile Processor
   * @param context processor context
   * @param session processor session reference.
   */
  void onTrigger(core::ProcessContext* context, core::ProcessSession* session) override;

  // Initialize, overwrite by NiFi RetryFlowFile
  void initialize() override;

 private:
  void create_topic_partition_list();
  void extend_config_from_dynamic_properties(const core::ProcessContext& context);
  void configure_new_connection(core::ProcessContext& context);
  std::string extract_message(const rd_kafka_message_t& rkmessage) const;
  std::vector<std::unique_ptr<rd_kafka_message_t, utils::rd_kafka_message_deleter>> poll_kafka_messages();
  utils::KafkaEncoding key_attr_encoding_attr_to_enum() const;
  utils::KafkaEncoding message_header_encoding_attr_to_enum() const;
  std::string resolve_duplicate_headers(const std::vector<std::string>& matching_headers) const;
  std::vector<std::string> get_matching_headers(const rd_kafka_message_t& message, const std::string& header_name) const;
  std::vector<std::pair<std::string, std::string>> get_flowfile_attributes_from_message_header(const rd_kafka_message_t& message) const;
  void add_kafka_attributes_to_flowfile(std::shared_ptr<FlowFileRecord>& flow_file, const rd_kafka_message_t& message) const;
  std::optional<std::vector<std::shared_ptr<FlowFileRecord>>> transform_pending_messages_into_flowfiles(core::ProcessSession& session) const;
  void process_pending_messages(core::ProcessSession& session);

 private:
  core::annotation::Input getInputRequirement() const override {
    return core::annotation::Input::INPUT_FORBIDDEN;
  }

  std::string kafka_brokers_;
  std::vector<std::string> topic_names_;
  std::string topic_name_format_;
  bool honor_transactions_;
  std::string group_id_;
  std::string offset_reset_;
  std::string key_attribute_encoding_;
  std::string message_demarcator_;
  std::string message_header_encoding_;
  std::string duplicate_header_handling_;
  std::vector<std::string> headers_to_add_as_attributes_;
  std::size_t max_poll_records_;
  std::chrono::milliseconds max_poll_time_milliseconds_;
  std::chrono::milliseconds session_timeout_milliseconds_;

  std::unique_ptr<rd_kafka_t, utils::rd_kafka_consumer_deleter> consumer_;
  std::unique_ptr<rd_kafka_conf_t, utils::rd_kafka_conf_deleter> conf_;
  std::unique_ptr<rd_kafka_topic_partition_list_t, utils::rd_kafka_topic_partition_list_deleter> kf_topic_partition_list_;

  // Intermediate container type for messages that have been processed, but are
  // not yet persisted (eg. in case of I/O error)
  std::vector<std::unique_ptr<rd_kafka_message_t, utils::rd_kafka_message_deleter>> pending_messages_;

  std::mutex do_not_call_on_trigger_concurrently_;
};

}  // namespace processors
}  // namespace minifi
}  // namespace nifi
}  // namespace apache
}  // namespace org
