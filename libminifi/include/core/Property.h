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
#ifndef LIBMINIFI_INCLUDE_CORE_PROPERTY_H_
#define LIBMINIFI_INCLUDE_CORE_PROPERTY_H_

#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>
#include <string_view>

#include "CachedValueValidator.h"
#include "core/Core.h"
#include "PropertyValidation.h"
#include "PropertyValue.h"
#include "utils/StringUtils.h"
#include "utils/TimeUtil.h"
#include "utils/gsl.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace core {

class PropertyBuilder;

class Property {
 public:
  /*!
   * Create a new property
   * Pay special attention to when value is "true" or "false"
   * as they will get coerced to the bool true and false, causing
   * further overwrites to inherit the bool validator.
   */
  Property(std::string name, std::string description, std::string value, bool is_required, std::string valid_regex, std::vector<std::string> dependent_properties,
           std::vector<std::pair<std::string, std::string>> exclusive_of_properties)
      : name_(std::move(name)),
        description_(std::move(description)),
        is_required_(is_required),
        valid_regex_(std::move(valid_regex)),
        dependent_properties_(std::move(dependent_properties)),
        exclusive_of_properties_(std::move(exclusive_of_properties)),
        is_collection_(false),
        supports_el_(false),
        is_transient_(false) {
    default_value_ = coerceDefaultValue(value);
  }

  Property(const std::string name, const std::string description, std::string value)
      : name_(name),
        description_(description),
        is_required_(false),
        is_collection_(false),
        supports_el_(false),
        is_transient_(false) {
    default_value_ = coerceDefaultValue(value);
  }

  Property(const std::string name, const std::string description)
      : name_(name),
        description_(description),
        is_required_(false),
        is_collection_(true),
        supports_el_(false),
        is_transient_(false) {}

  Property(Property &&other) = default;

  Property(const Property &other) = default;

  Property()
      : name_(""),
        description_(""),
        is_required_(false),
        is_collection_(false),
        supports_el_(false),
        is_transient_(false) {}

  virtual ~Property() = default;

  void setTransient() {
    is_transient_ = true;
  }

  bool isTransient() const {
    return is_transient_;
  }
  std::string getName() const;
  std::string getDisplayName() const;
  std::vector<std::string> getAllowedTypes() const;
  std::string getDescription() const;
  std::shared_ptr<PropertyValidator> getValidator() const;
  const PropertyValue &getValue() const;
  bool getRequired() const;
  bool supportsExpressionLanguage() const;
  std::string getValidRegex() const;
  std::vector<std::string> getDependentProperties() const;
  std::vector<std::pair<std::string, std::string>> getExclusiveOfProperties() const;
  std::vector<std::string> getValues();

  const PropertyValue &getDefaultValue() const {
    return default_value_;
  }

  template<typename T = std::string>
  void setValue(const T &value) {
    if (!is_collection_) {
      values_.clear();
      values_.push_back(default_value_);
    } else {
      values_.push_back(default_value_);
    }
    PropertyValue& vn = values_.back();
    vn.setValidator(validator_);
    vn = value;
    ValidationResult result = vn.validate(name_);
    if (!result.valid()) {
      throw utils::internal::InvalidValueException(name_ + " value validation failed");
    }
  }

  void setValue(PropertyValue &newValue) {
    if (!is_collection_) {
      values_.clear();
      values_.push_back(newValue);
    } else {
      values_.push_back(newValue);
    }
    PropertyValue& vn = values_.back();
    vn.setValidator(validator_);
    ValidationResult result = vn.validate(name_);
    if (!result.valid()) {
      throw utils::internal::InvalidValueException(name_ + " value validation failed");
    }
  }
  void setSupportsExpressionLanguage(bool supportEl);

  std::vector<PropertyValue> getAllowedValues() const {
    return allowed_values_;
  }

  void addAllowedValue(const PropertyValue &value) {
    allowed_values_.push_back(value);
  }

  void clearAllowedValues() {
    allowed_values_.clear();
  }

  /**
   * Add value to the collection of values.
   */
  void addValue(const std::string &value);
  Property &operator=(const Property &other) = default;
  Property &operator=(Property &&other) = default;
// Compare
  bool operator <(const Property & right) const;


  static bool StringToDateTime(const std::string& input, int64_t& output) {
    int64_t temp = utils::timeutils::parseDateTimeStr(input);
    if (temp == -1) {
      return false;
    }
    output = temp;
    return true;
  }

  static bool StringToPermissions(const std::string& input, uint32_t& output) {
    uint32_t temp = 0U;
    if (input.size() == 9U) {
      /* Probably rwxrwxrwx formatted */
      for (size_t i = 0; i < 3; i++) {
        if (input[i * 3] == 'r') {
          temp |= 04 << ((2 - i) * 3);
        } else if (input[i * 3] != '-') {
          return false;
        }
        if (input[i * 3 + 1] == 'w') {
          temp |= 02 << ((2 - i) * 3);
        } else if (input[i * 3 + 1] != '-') {
          return false;
        }
        if (input[i * 3 + 2] == 'x') {
          temp |= 01 << ((2 - i) * 3);
        } else if (input[i * 3 + 2] != '-') {
          return false;
        }
      }
    } else {
      /* Probably octal */
      try {
        size_t pos = 0U;
        temp = std::stoul(input, &pos, 8);
        if (pos != input.size()) {
          return false;
        }
        if ((temp & ~static_cast<uint32_t>(0777)) != 0U) {
          return false;
        }
      } catch (...) {
        return false;
      }
    }
    output = temp;
    return true;
  }

  // Convert String to Integer
  template<typename T>
  static bool StringToInt(std::string input, T &output) {
    return DataSizeValue::StringToInt<T>(input, output);
  }

  static bool StringToInt(std::string input, int64_t &output) {
    return StringToInt<int64_t>(input, output);
  }

// Convert String to Integer
  static bool StringToInt(std::string input, uint64_t &output) {
    return StringToInt<uint64_t>(input, output);
  }

  static bool StringToInt(std::string input, int32_t &output) {
    return StringToInt<int32_t>(input, output);
  }

// Convert String to Integer
  static bool StringToInt(std::string input, uint32_t &output) {
    return StringToInt<uint32_t>(input, output);
  }

 protected:
  /**
   * Coerce default values at construction.
   */
  PropertyValue coerceDefaultValue(const std::string &value) {
    PropertyValue ret;
    if (value == "false" || value == "true") {
      bool val;
      std::istringstream(value) >> std::boolalpha >> val;
      ret = val;
      validator_ = StandardValidators::getValidator(ret.getValue());
    } else {
      ret = value;
      validator_ = StandardValidators::get().VALID_VALIDATOR;
    }
    return ret;
  }

  std::string name_;
  std::string description_;
  bool is_required_;
  std::string valid_regex_;
  std::vector<std::string> dependent_properties_;
  std::vector<std::pair<std::string, std::string>> exclusive_of_properties_;
  bool is_collection_;
  PropertyValue default_value_;
  std::vector<PropertyValue> values_;
  gsl::not_null<std::shared_ptr<PropertyValidator>> validator_{StandardValidators::get().VALID_VALIDATOR};
  std::string display_name_;
  std::vector<PropertyValue> allowed_values_;
  // types represents the allowable types for this property
  // these types should be the canonical name.
  std::vector<std::string> types_;
  bool supports_el_;
  bool is_transient_;

 private:
  friend class PropertyBuilder;
};

template<typename T>
class ConstrainedProperty;

class PropertyBuilder : public std::enable_shared_from_this<PropertyBuilder> {
 public:
  static std::shared_ptr<PropertyBuilder> createProperty(const std::string &name) {
    std::shared_ptr<PropertyBuilder> builder = std::unique_ptr<PropertyBuilder>(new PropertyBuilder());
    builder->prop.name_ = name;
    return builder;
  }

  static std::shared_ptr<PropertyBuilder> createProperty(const std::string &name, const std::string &displayName) {
    std::shared_ptr<PropertyBuilder> builder = std::unique_ptr<PropertyBuilder>(new PropertyBuilder());
    builder->prop.name_ = name;
    builder->prop.display_name_ = displayName;
    return builder;
  }

  std::shared_ptr<PropertyBuilder> withDescription(const std::string &description) {
    prop.description_ = description;
    return shared_from_this();
  }

  std::shared_ptr<PropertyBuilder> isRequired(bool required) {
    prop.is_required_ = required;
    return shared_from_this();
  }

  std::shared_ptr<PropertyBuilder> supportsExpressionLanguage(bool sel) {
    prop.supports_el_ = sel;
    return shared_from_this();
  }

  template<typename T>
  std::shared_ptr<PropertyBuilder> withDefaultValue(const T &df, const std::shared_ptr<PropertyValidator> &validator = nullptr) {
    prop.default_value_ = df;

    if (validator != nullptr) {
      prop.default_value_.setValidator(gsl::make_not_null(validator));
      prop.validator_ = gsl::make_not_null(validator);
    } else {
      prop.validator_ = StandardValidators::getValidator(prop.default_value_.getValue());
      prop.default_value_.setValidator(prop.validator_);
    }
    // inspect the type and add a validator to this.
    // there may be cases in which the validator is typed differently
    // and a validator can be added for this.
    return shared_from_this();
  }

  std::shared_ptr<PropertyBuilder> withType(const std::shared_ptr<PropertyValidator> &validator) {
    prop.validator_ = gsl::make_not_null(validator);
    prop.default_value_.setValidator(gsl::make_not_null(validator));
    return shared_from_this();
  }

  template<typename T>
  std::shared_ptr<ConstrainedProperty<T>> withAllowableValue(const T& df) {
    auto property = std::make_shared<ConstrainedProperty<T>>(shared_from_this());
    property->withAllowableValue(df);
    return property;
  }

  template<typename T>
  std::shared_ptr<ConstrainedProperty<T>> withAllowableValues(const std::set<T> &df) {
    auto property = std::make_shared<ConstrainedProperty<T>>(shared_from_this());
    property->withAllowableValues(df);
    return property;
  }

  template<typename T>
  std::shared_ptr<PropertyBuilder> withDefaultValue(const std::string &df) {
    prop.default_value_.operator=<T>(df);

    prop.validator_ = StandardValidators::getValidator(prop.default_value_.getValue());
    prop.default_value_.setValidator(prop.validator_);

    // inspect the type and add a validator to this.
    // there may be cases in which the validator is typed differently
    // and a validator can be added for this.
    return shared_from_this();
  }

  template<typename T>
  std::shared_ptr<PropertyBuilder> asType() {
    prop.types_.push_back(core::getClassName<T>());
    return shared_from_this();
  }

  std::shared_ptr<PropertyBuilder> withExclusiveProperty(const std::string &property, const std::string regex) {
    prop.exclusive_of_properties_.push_back({ property, regex });
    return shared_from_this();
  }

  Property &&build() {
    return std::move(prop);
  }

 private:
  Property prop;

  PropertyBuilder() = default;
};

template<typename T>
class ConstrainedProperty : public std::enable_shared_from_this<ConstrainedProperty<T>> {
 public:
  std::shared_ptr<ConstrainedProperty<T>> withDescription(const std::string &description) {
    builder_->withDescription(description);
    return this->shared_from_this();
  }

  std::shared_ptr<ConstrainedProperty<T>> isRequired(bool required) {
    builder_->isRequired(required);
    return this->shared_from_this();
  }

  std::shared_ptr<ConstrainedProperty<T>> supportsExpressionLanguage(bool sel) {
    builder_->supportsExpressionLanguage(sel);
    return this->shared_from_this();
  }

  std::shared_ptr<ConstrainedProperty<T>> withDefaultValue(const T &df, const std::shared_ptr<PropertyValidator> &validator = nullptr) {
    builder_->withDefaultValue(df, validator);
    return this->shared_from_this();
  }

  std::shared_ptr<ConstrainedProperty<T>> withAllowableValue(const T& df) {
    PropertyValue dn;
    dn = df;
    allowed_values_.emplace_back(dn);
    return this->shared_from_this();
  }

  std::shared_ptr<ConstrainedProperty<T>> withAllowableValues(const std::set<T>& defaultValues) {
    for (const auto &defaultValue : defaultValues) {
      PropertyValue dn;
      dn = defaultValue;
      allowed_values_.emplace_back(dn);
    }
    return this->shared_from_this();
  }

  template<typename J>
  std::shared_ptr<ConstrainedProperty<T>> asType() {
    builder_->asType<J>();
    return this->shared_from_this();
  }

  std::shared_ptr<ConstrainedProperty<T>> withExclusiveProperty(const std::string &property, const std::string regex) {
    builder_->withExclusiveProperty(property, regex);
    return this->shared_from_this();
  }

  Property &&build() {
    Property &&prop = builder_->build();
    for (const auto &value : allowed_values_) {
      prop.addAllowedValue(value);
    }
    return std::move(prop);
  }

  ConstrainedProperty(const std::shared_ptr<PropertyBuilder> &builder) // NOLINT
      : builder_(builder) {
  }

 protected:
  std::vector<PropertyValue> allowed_values_;
  std::shared_ptr<PropertyBuilder> builder_;

  friend class PropertyBuilder;
};

struct ConfigurationProperty {
  explicit ConfigurationProperty(std::string_view name,
      gsl::not_null<PropertyValidator*> validator = gsl::make_not_null(StandardValidators::get().VALID_VALIDATOR.get()))
    : name(name),
      validator(validator) {
  }

  std::string_view name;
  gsl::not_null<PropertyValidator*> validator;
};

}  // namespace core
}  // namespace minifi
}  // namespace nifi
}  // namespace apache
}  // namespace org

#endif  // LIBMINIFI_INCLUDE_CORE_PROPERTY_H_
