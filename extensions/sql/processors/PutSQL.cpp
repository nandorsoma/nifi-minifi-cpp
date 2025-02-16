/**
 * @file PutSQL.cpp
 * PutSQL class declaration
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

#include "PutSQL.h"

#include <string>

#include "io/BufferStream.h"
#include "core/ProcessContext.h"
#include "core/ProcessSession.h"
#include "core/Resource.h"
#include "Exception.h"

namespace org::apache::nifi::minifi::processors {

const std::string PutSQL::ProcessorName("PutSQL");

const core::Property PutSQL::SQLStatement(
  core::PropertyBuilder::createProperty("SQL Statement")
  ->isRequired(false)
  ->withDescription(
      "The SQL statement to execute. The statement can be empty, a constant value, or built from attributes using Expression Language. "
      "If this property is specified, it will be used regardless of the content of incoming flowfiles. If this property is empty, the content of "
      "the incoming flow file is expected to contain a valid SQL statement, to be issued by the processor to the database.")
  ->supportsExpressionLanguage(true)->build());

const core::Relationship PutSQL::Success("success", "Database is successfully updated.");

PutSQL::PutSQL(const std::string& name, const utils::Identifier& uuid)
  : SQLProcessor(name, uuid, core::logging::LoggerFactory<PutSQL>::getLogger()) {
}

void PutSQL::initialize() {
  //! Set the supported properties
  setSupportedProperties({ DBControllerService, SQLStatement });

  //! Set the supported relationships
  setSupportedRelationships({ Success });
}

void PutSQL::processOnSchedule(core::ProcessContext& /*context*/) {}

void PutSQL::processOnTrigger(core::ProcessContext& context, core::ProcessSession& session) {
  auto flow_file = session.get();
  if (!flow_file) {
    context.yield();
    return;
  }
  session.transfer(flow_file, Success);

  std::string sql_statement;
  if (!context.getProperty(SQLStatement, sql_statement, flow_file)) {
    logger_->log_debug("Using the contents of the flow file as the SQL statement");
    sql_statement = to_string(session.readBuffer(flow_file));
  }
  if (sql_statement.empty()) {
    throw Exception(PROCESSOR_EXCEPTION, "Empty SQL statement");
  }

  connection_->prepareStatement(sql_statement)->execute(collectArguments(flow_file));
}

REGISTER_RESOURCE(PutSQL, "PutSQL to execute SQL command via ODBC.");

}  // namespace org::apache::nifi::minifi::processors
