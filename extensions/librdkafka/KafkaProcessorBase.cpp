/**
 * @file KafkaProcessorBase.cpp
 * KafkaProcessorBase class implementation
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
#include "KafkaProcessorBase.h"

#include "rdkafka_utils.h"
#include "utils/ProcessorConfigUtils.h"
#include "controllers/SSLContextService.h"

namespace org::apache::nifi::minifi::processors {

const core::Property KafkaProcessorBase::SecurityProtocol(
        core::PropertyBuilder::createProperty("Security Protocol")
        ->withDescription("Protocol used to communicate with brokers. Corresponds to Kafka's 'security.protocol' property.")
        ->withDefaultValue<std::string>(toString(SecurityProtocolOption::PLAINTEXT))
        ->withAllowableValues<std::string>(SecurityProtocolOption::values())
        ->isRequired(true)
        ->build());
const core::Property KafkaProcessorBase::SSLContextService(
    core::PropertyBuilder::createProperty("SSL Context Service")
        ->withDescription("SSL Context Service Name")
        ->asType<minifi::controllers::SSLContextService>()
        ->build());
const core::Property KafkaProcessorBase::KerberosServiceName(
    core::PropertyBuilder::createProperty("Kerberos Service Name")
        ->withDescription("Kerberos Service Name")
        ->build());
const core::Property KafkaProcessorBase::KerberosPrincipal(
    core::PropertyBuilder::createProperty("Kerberos Principal")
        ->withDescription("Keberos Principal")
        ->build());
const core::Property KafkaProcessorBase::KerberosKeytabPath(
    core::PropertyBuilder::createProperty("Kerberos Keytab Path")
        ->withDescription("The path to the location on the local filesystem where the kerberos keytab is located. Read permission on the file is required.")
        ->build());
const core::Property KafkaProcessorBase::SASLMechanism(
        core::PropertyBuilder::createProperty("SASL Mechanism")
        ->withDescription("The SASL mechanism to use for authentication. Corresponds to Kafka's 'sasl.mechanism' property.")
        ->withDefaultValue<std::string>(toString(SASLMechanismOption::GSSAPI))
        ->withAllowableValues<std::string>(SASLMechanismOption::values())
        ->isRequired(true)
        ->build());
const core::Property KafkaProcessorBase::Username(
    core::PropertyBuilder::createProperty("Username")
        ->withDescription("The username when the SASL Mechanism is sasl_plaintext")
        ->build());
const core::Property KafkaProcessorBase::Password(
    core::PropertyBuilder::createProperty("Password")
        ->withDescription("The password for the given username when the SASL Mechanism is sasl_plaintext")
        ->build());

std::optional<utils::SSL_data> KafkaProcessorBase::getSslData(core::ProcessContext& context) const {
  std::string ssl_service_name;
  if (context.getProperty(SSLContextService.getName(), ssl_service_name) && !ssl_service_name.empty()) {
    std::shared_ptr<core::controller::ControllerService> service = context.getControllerService(ssl_service_name);
    if (service) {
      auto ssl_service = std::static_pointer_cast<minifi::controllers::SSLContextService>(service);
      utils::SSL_data ssl_data;
      ssl_data.ca_loc = ssl_service->getCACertificate();
      ssl_data.cert_loc = ssl_service->getCertificateFile();
      ssl_data.key_loc = ssl_service->getPrivateKeyFile();
      ssl_data.key_pw = ssl_service->getPassphrase();
      return ssl_data;
    } else {
      logger_->log_warn("SSL Context Service property is set to '%s', but the controller service could not be found.", ssl_service_name);
      return std::nullopt;
    }
  } else if (security_protocol_ == SecurityProtocolOption::SSL || security_protocol_ == SecurityProtocolOption::SASL_SSL) {
    logger_->log_warn("Security protocol is set to %s, but no valid SSL Context Service property is set.", security_protocol_.toString());
  }

  return std::nullopt;
}

void KafkaProcessorBase::setKafkaAuthenticationParameters(core::ProcessContext& context, gsl::not_null<rd_kafka_conf_t*> config) {
  security_protocol_ = utils::getRequiredPropertyOrThrow<SecurityProtocolOption>(context, SecurityProtocol.getName());
  utils::setKafkaConfigurationField(*config, "security.protocol", security_protocol_.toString());
  logger_->log_debug("Kafka security.protocol [%s]", security_protocol_.toString());
  if (security_protocol_ == SecurityProtocolOption::SSL || security_protocol_ == SecurityProtocolOption::SASL_SSL) {
    auto ssl_data = getSslData(context);
    if (ssl_data) {
      if (ssl_data->ca_loc.empty() && ssl_data->cert_loc.empty() && ssl_data->key_loc.empty() && ssl_data->key_pw.empty()) {
        logger_->log_warn("Security protocol is set to %s, but no valid security parameters are set in the properties or in the SSL Context Service.", security_protocol_.toString());
      } else {
        utils::setKafkaConfigurationField(*config, "ssl.ca.location", ssl_data->ca_loc);
        logger_->log_debug("Kafka ssl.ca.location [%s]", ssl_data->ca_loc);
        utils::setKafkaConfigurationField(*config, "ssl.certificate.location", ssl_data->cert_loc);
        logger_->log_debug("Kafka ssl.certificate.location [%s]", ssl_data->cert_loc);
        utils::setKafkaConfigurationField(*config, "ssl.key.location", ssl_data->key_loc);
        logger_->log_debug("Kafka ssl.key.location [%s]", ssl_data->key_loc);
        utils::setKafkaConfigurationField(*config, "ssl.key.password", ssl_data->key_pw);
        logger_->log_debug("Kafka ssl.key.password was set");
      }
    }
  }

  auto sasl_mechanism = utils::getRequiredPropertyOrThrow<SASLMechanismOption>(context, SASLMechanism.getName());
  utils::setKafkaConfigurationField(*config, "sasl.mechanism", sasl_mechanism.toString());
  logger_->log_debug("Kafka sasl.mechanism [%s]", sasl_mechanism.toString());

  auto setKafkaConfigIfNotEmpty = [this, &context, config](const std::string& property_name, const std::string& kafka_config_name, bool log_value = true) {
    std::string value;
    if (context.getProperty(property_name, value) && !value.empty()) {
      utils::setKafkaConfigurationField(*config, kafka_config_name, value);
      if (log_value) {
        logger_->log_debug("Kafka %s [%s]", kafka_config_name, value);
      } else {
        logger_->log_debug("Kafka %s was set", kafka_config_name);
      }
    }
  };

  setKafkaConfigIfNotEmpty(KerberosServiceName.getName(), "sasl.kerberos.service.name");
  setKafkaConfigIfNotEmpty(KerberosPrincipal.getName(), "sasl.kerberos.principal");
  setKafkaConfigIfNotEmpty(KerberosKeytabPath.getName(), "sasl.kerberos.keytab");
  setKafkaConfigIfNotEmpty(Username.getName(), "sasl.username");
  setKafkaConfigIfNotEmpty(Password.getName(), "sasl.password", false);
}

}  // namespace org::apache::nifi::minifi::processors
