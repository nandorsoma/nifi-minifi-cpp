/**
 * @file AzureBlobStorageProcessorBase.cpp
 * AzureBlobStorageProcessorBase class implementation
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

#include "AzureBlobStorageProcessorBase.h"

#include "core/ProcessContext.h"

namespace org::apache::nifi::minifi::azure::processors {

const core::Property AzureBlobStorageProcessorBase::ContainerName(
  core::PropertyBuilder::createProperty("Container Name")
    ->withDescription("Name of the Azure Storage container. In case of PutAzureBlobStorage processor, container can be created if it does not exist.")
    ->supportsExpressionLanguage(true)
    ->isRequired(true)
    ->build());
const core::Property AzureBlobStorageProcessorBase::StorageAccountName(
    core::PropertyBuilder::createProperty("Storage Account Name")
      ->withDescription("The storage account name.")
      ->supportsExpressionLanguage(true)
      ->build());
const core::Property AzureBlobStorageProcessorBase::StorageAccountKey(
    core::PropertyBuilder::createProperty("Storage Account Key")
      ->withDescription("The storage account key. This is an admin-like password providing access to every container in this account. "
                        "It is recommended one uses Shared Access Signature (SAS) token instead for fine-grained control with policies.")
      ->supportsExpressionLanguage(true)
      ->build());
const core::Property AzureBlobStorageProcessorBase::SASToken(
    core::PropertyBuilder::createProperty("SAS Token")
      ->withDescription("Shared Access Signature token. Specify either SAS Token (recommended) or Storage Account Key together with Storage Account Name if Managed Identity is not used.")
      ->supportsExpressionLanguage(true)
      ->build());
const core::Property AzureBlobStorageProcessorBase::CommonStorageAccountEndpointSuffix(
    core::PropertyBuilder::createProperty("Common Storage Account Endpoint Suffix")
      ->withDescription("Storage accounts in public Azure always use a common FQDN suffix. Override this endpoint suffix with a "
                        "different suffix in certain circumstances (like Azure Stack or non-public Azure regions). ")
      ->supportsExpressionLanguage(true)
      ->build());
const core::Property AzureBlobStorageProcessorBase::ConnectionString(
  core::PropertyBuilder::createProperty("Connection String")
    ->withDescription("Connection string used to connect to Azure Storage service. This overrides all other set credential properties if Managed Identity is not used.")
    ->supportsExpressionLanguage(true)
    ->build());
const core::Property AzureBlobStorageProcessorBase::UseManagedIdentityCredentials(
  core::PropertyBuilder::createProperty("Use Managed Identity Credentials")
    ->withDescription("If true Managed Identity credentials will be used together with the Storage Account Name for authentication.")
    ->isRequired(true)
    ->withDefaultValue<bool>(false)
    ->build());

void AzureBlobStorageProcessorBase::onSchedule(const std::shared_ptr<core::ProcessContext>& context, const std::shared_ptr<core::ProcessSessionFactory>& /*sessionFactory*/) {
  gsl_Expects(context);
  std::string value;
  if (!context->getProperty(ContainerName.getName(), value) || value.empty()) {
    throw Exception(PROCESS_SCHEDULE_EXCEPTION, "Container Name property missing or invalid");
  }

  if (context->getProperty(AzureStorageCredentialsService.getName(), value) && !value.empty()) {
    logger_->log_info("Getting Azure Storage credentials from controller service with name: '%s'", value);
    return;
  }

  if (!context->getProperty(UseManagedIdentityCredentials.getName(), use_managed_identity_credentials_)) {
    throw Exception(PROCESS_SCHEDULE_EXCEPTION, "Use Managed Identity Credentials is invalid.");
  }

  if (use_managed_identity_credentials_) {
    logger_->log_info("Using Managed Identity for authentication");
    return;
  }

  if (context->getProperty(ConnectionString.getName(), value) && !value.empty()) {
    logger_->log_info("Using connectionstring directly for Azure Storage authentication");
    return;
  }

  if (!context->getProperty(StorageAccountName.getName(), value) || value.empty()) {
    throw Exception(PROCESS_SCHEDULE_EXCEPTION, "Storage Account Name property missing or invalid");
  }

  if (context->getProperty(StorageAccountKey.getName(), value) && !value.empty()) {
    logger_->log_info("Using storage account name and key for authentication");
    return;
  }

  if (!context->getProperty(SASToken.getName(), value) || value.empty()) {
    throw Exception(PROCESS_SCHEDULE_EXCEPTION, "Neither Storage Account Key nor SAS Token property was set.");
  }

  logger_->log_info("Using storage account name and SAS token for authentication");
}

storage::AzureStorageCredentials AzureBlobStorageProcessorBase::getAzureCredentialsFromProperties(
    core::ProcessContext &context, const std::shared_ptr<core::FlowFile> &flow_file) const {
  storage::AzureStorageCredentials credentials;
  std::string value;
  if (context.getProperty(StorageAccountName, value, flow_file)) {
    credentials.setStorageAccountName(value);
  }
  if (context.getProperty(StorageAccountKey, value, flow_file)) {
    credentials.setStorageAccountKey(value);
  }
  if (context.getProperty(SASToken, value, flow_file)) {
    credentials.setSasToken(value);
  }
  if (context.getProperty(CommonStorageAccountEndpointSuffix, value, flow_file)) {
    credentials.setEndpontSuffix(value);
  }
  if (context.getProperty(ConnectionString, value, flow_file)) {
    credentials.setConnectionString(value);
  }
  credentials.setUseManagedIdentityCredentials(use_managed_identity_credentials_);
  return credentials;
}

bool AzureBlobStorageProcessorBase::setCommonStorageParameters(
    storage::AzureBlobStorageParameters& params,
    core::ProcessContext &context,
    const std::shared_ptr<core::FlowFile> &flow_file) {
  auto credentials = getCredentials(context, flow_file);
  if (!credentials) {
    return false;
  }

  params.credentials = *credentials;

  if (!context.getProperty(ContainerName, params.container_name, flow_file) || params.container_name.empty()) {
    logger_->log_error("Container Name is invalid or empty!");
    return false;
  }

  return true;
}

std::optional<storage::AzureStorageCredentials> AzureBlobStorageProcessorBase::getCredentials(
    core::ProcessContext &context,
    const std::shared_ptr<core::FlowFile> &flow_file) const {
  auto [result, controller_service_creds] = getCredentialsFromControllerService(context);
  if (controller_service_creds) {
    if (controller_service_creds->isValid()) {
      logger_->log_debug("Azure credentials read from credentials controller service!");
      return controller_service_creds;
    } else {
      logger_->log_error("Azure credentials controller service is set with invalid credential parameters!");
      return std::nullopt;
    }
  } else if (result == GetCredentialsFromControllerResult::CONTROLLER_NAME_INVALID) {
    logger_->log_error("Azure credentials controller service name is invalid!");
    return std::nullopt;
  }

  logger_->log_debug("No valid Azure credentials are set in credentials controller service, checking properties...");

  auto property_creds = getAzureCredentialsFromProperties(context, flow_file);
  if (property_creds.isValid()) {
    logger_->log_debug("Azure credentials read from properties!");
    return property_creds;
  }

  logger_->log_error("No valid Azure credentials are set in credentials controller service nor in properties!");
  return std::nullopt;
}

}  // namespace org::apache::nifi::minifi::azure::processors
