// SPDX-License-Identifier: Apache-2.0
/**
 * Copyright (C) 2021 Jihoon Lee <jhoon.it.lee@samsung.com>
 *
 * @file   common_properties.cpp
 * @date   14 May 2021
 * @brief  This file contains implementation of common properties widely used
 * across layers
 * @see	   https://github.com/nnstreamer/nntrainer
 * @author Jihoon Lee <jhoon.it.lee@samsung.com>
 * @bug    No known bugs except for NYI items
 */
#include <base_properties.h>
#include <common_properties.h>

#include <nntrainer_error.h>
#include <nntrainer_log.h>
#include <stdexcept>
#include <tensor_dim.h>

#include <regex>
#include <sstream>
#include <utility>
#include <vector>

namespace nntrainer {
namespace props {

Name::Name() : nntrainer::Property<std::string>() {}

Name::Name(const std::string &value) { set(value); }

void Name::set(const std::string &value) {
  auto to_lower = [](const std::string &str) {
    std::string ret = str;
    std::transform(ret.begin(), ret.end(), ret.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ret;
  };
  nntrainer::Property<std::string>::set(to_lower(value));
}

bool Name::isValid(const std::string &v) const {
  static std::regex allowed("[a-zA-Z0-9][-_./a-zA-Z0-9]*");
  return !v.empty() && std::regex_match(v, allowed);
}

Normalization::Normalization(bool value) { set(value); }

Standardization::Standardization(bool value) { set(value); }

bool DropOutRate::isValid(const float &v) const {
  if (v < 0.0)
    return false;
  else
    return true;
}

void RandomTranslate::set(const float &value) {
  Property<float>::set(std::abs(value));
}

bool FilePath::isValid(const std::string &v) const {
  std::ifstream file(v, std::ios::binary | std::ios::ate);
  return file.good();
}

void FilePath::set(const std::string &v) {
  Property<std::string>::set(v);
  std::ifstream file(v, std::ios::binary | std::ios::ate);
  cached_pos_size = file.tellg();
}

std::ifstream::pos_type FilePath::file_size() { return cached_pos_size; }

ReturnSequences::ReturnSequences(bool value) { set(value); }

bool NumClass::isValid(const unsigned int &v) const { return v > 0; }

InputConnection::InputConnection() : nntrainer::Property<Connection>() {}
InputConnection::InputConnection(const Connection &value) :
  nntrainer::Property<Connection>(value) {} /**< default value if any */

Connection::Connection(const std::string &layer_name, unsigned int idx) :
  index(idx),
  name(layer_name) {}

Connection::Connection(const Connection &rhs) = default;
Connection &Connection::operator=(const Connection &rhs) = default;
Connection::Connection(Connection &&rhs) noexcept = default;
Connection &Connection::operator=(Connection &&rhs) noexcept = default;

bool Connection::operator==(const Connection &rhs) const noexcept {
  return index == rhs.index and name == rhs.name;
};

Epsilon::Epsilon(float value) { set(value); }

bool Epsilon::isValid(const float &value) const { return value > 0.0f; }

Momentum::Momentum(float value) { set(value); }

bool Momentum::isValid(const float &value) const {
  return value > 0.0f && value < 1.0f;
}

bool Axis::isValid(const unsigned int &value) const {
  return value < ml::train::TensorDim::MAXDIM;
}

bool SplitDimension::isValid(const unsigned int &value) const {
  return value > 0 && value < ml::train::TensorDim::MAXDIM;
}

PoolSize::PoolSize(unsigned int value) { set(value); }

Stride::Stride(unsigned int value) { set(value); }

/**
 * @brief unsigned integer property, internally used to parse padding values
 *
 */
class Padding_ : public nntrainer::Property<int> {
public:
  using prop_tag = int_prop_tag; /**< property type */
};

bool Padding2D::isValid(const std::string &v) const {

  /// case 1, 2: padding has string literal
  if (istrequal(v, "valid") || istrequal(v, "same")) {
    return true;
  }

  std::vector<props::Padding_> paddings;
  from_string(v, paddings);

  /// case 3, 4, 5: padding has a sequence of unsigned integer
  if (paddings.size() == 1 || paddings.size() == 2 || paddings.size() == 4) {
    /// check if every padding is non-negative integer
    for (const auto &padding : paddings) {
      if (padding.get() < 0) {
        return false;
      }
    }
    return true;
  }

  /// case else: false
  return false;
}

std::array<unsigned int, 4>
Padding2D::compute(const TensorDim &input, const TensorDim &kernel,
                   const std::array<unsigned int, 2> &strides) {
  auto &padding_repr = get(); /// padding representation

  if (istrequal(padding_repr, "valid")) {
    return {0, 0, 0, 0};
  }

  /// in the case of same padding, padding is distributed to each side if
  /// possible. otherwise pad_all_side / 2 is allocated to top | left and rest
  /// are assigned to the other side
  if (istrequal(padding_repr, "same")) {
    /// @note if we start to consider dilation, this calculation has to tuned
    /// accordingly.

    auto calculate_padding = [](unsigned input_, unsigned kernel_,
                                unsigned stride) {
      /// ceil(input / stride)
      auto out = (input_ + stride - 1) / stride;
      auto req_input = (out - 1) * stride + kernel_;
      return req_input >= input_ ? req_input - input_ : 0;
    };

    auto pad_horizontal =
      calculate_padding(input.width(), kernel.width(), strides[1]);
    auto pad_vertical =
      calculate_padding(input.height(), kernel.height(), strides[0]);

    auto pad_top = pad_vertical / 2;
    auto pad_left = pad_horizontal / 2;

    return {pad_top, pad_vertical - pad_top, pad_left,
            pad_horizontal - pad_left};
  }

  /// case 3, 4, 5: padding has a sequence of unsigned integer
  std::vector<props::Padding_> paddings_;
  from_string(padding_repr, paddings_);
  std::vector<unsigned int> paddings(paddings_.begin(), paddings_.end());

  switch (paddings.size()) {
  case 1:
    return {paddings[0], paddings[0], paddings[0], paddings[0]};
  case 2:
    return {paddings[0], paddings[0], paddings[1], paddings[1]};
  case 4:
    return {paddings[0], paddings[1], paddings[2], paddings[3]};
  default:
    throw std::logic_error("[padding] should not reach here");
  }

  throw std::logic_error("[padding] should not reach here");
}

bool Padding1D::isValid(const std::string &v) const {

  /// case 1, 2: padding has string literal
  if (istrequal(v, "valid") || istrequal(v, "same")) {
    return true;
  }

  std::vector<props::Padding_> paddings;
  from_string(v, paddings);

  /// case 3, 4: padding has a sequence of unsigned integer
  if (paddings.size() == 1 || paddings.size() == 2) {
    /// check if every padding is non-negative integer
    for (const auto &padding : paddings) {
      if (padding.get() < 0) {
        return false;
      }
    }
    return true;
  }

  /// case else: false
  return false;
}

std::array<unsigned int, 2> Padding1D::compute(const TensorDim &input,
                                               const TensorDim &kernel,
                                               const unsigned int &strides) {
  auto &padding_repr = get(); /// padding representation

  if (istrequal(padding_repr, "valid")) {
    return {0, 0};
  }

  // NYI
  return {0, 0};
}

WeightRegularizerConstant::WeightRegularizerConstant(float value) {
  set(value);
}

bool WeightRegularizerConstant::isValid(const float &value) const {
  return value >= 0.0f;
}

InputLayer::InputLayer() : Name() {}
InputLayer::InputLayer(const std::string &name) : Name(name) {}

OutputLayer::OutputLayer() : Name() {}
OutputLayer::OutputLayer(const std::string &name) : Name(name) {}

LabelLayer::LabelLayer() : Name() {}
LabelLayer::LabelLayer(const std::string &name) : Name(name) {}

HiddenStateActivation::HiddenStateActivation(ActivationTypeInfo::Enum value) {
  set(value);
};

RecurrentActivation::RecurrentActivation(ActivationTypeInfo::Enum value) {
  set(value);
};

WeightInitializer::WeightInitializer(Tensor::Initializer value) { set(value); }

BiasInitializer::BiasInitializer(Tensor::Initializer value) { set(value); }

BNPARAMS_MU_INIT::BNPARAMS_MU_INIT(Tensor::Initializer value) { set(value); }

BNPARAMS_VAR_INIT::BNPARAMS_VAR_INIT(Tensor::Initializer value) { set(value); }

BNPARAMS_GAMMA_INIT::BNPARAMS_GAMMA_INIT(Tensor::Initializer value) {
  set(value);
}

BNPARAMS_BETA_INIT::BNPARAMS_BETA_INIT(Tensor::Initializer value) {
  set(value);
}

WeightRegularizer::WeightRegularizer(nntrainer::WeightRegularizer value) {
  set(value);
}

bool WeightRegularizer::isValid(
  const nntrainer::WeightRegularizer &value) const {
  return value != nntrainer::WeightRegularizer::UNKNOWN;
}

FlipDirection::FlipDirection(FlipDirectionInfo::Enum value) { set(value); }

void GenericShape::set(const TensorDim &value) {
  TensorDim ret = value;
  ret.setDynDimFlag(0b1000);
  if (ret.batch() != 1) {
    ml_logw("Batch size set with dimension %u is ignored."
            "Use batchsize property for the model to update batchsize.",
            ret.batch());
    ret.batch(1);
  }
  Property<TensorDim>::set(ret);
}

} // namespace props

template <>
std::string
str_converter<props::connection_prop_tag, props::Connection>::to_string(
  const props::Connection &value) {
  std::stringstream ss;
  ss << value.getName().get() << '(' << value.getIndex() << ')';
  return ss.str();
}

template <>
props::Connection
str_converter<props::connection_prop_tag, props::Connection>::from_string(
  const std::string &value) {
  auto pos = value.find_first_of('(');
  auto idx = 0u;
  auto name_part = value.substr(0, pos);

  if (pos != std::string::npos) {
    NNTR_THROW_IF(value.back() != ')', std::invalid_argument)
      << "failed to parse connection invalid format: " << value;

    auto idx_part = value.substr(pos + 1, value.length() - 1);
    idx = str_converter<uint_prop_tag, unsigned>::from_string(idx_part);
  }

  return props::Connection(name_part, idx);
}

} // namespace nntrainer
