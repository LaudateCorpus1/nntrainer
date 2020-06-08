/**
 * Copyright (C) 2019 Samsung Electronics Co., Ltd. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *   http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *
 * @file	tensor.h
 * @date	04 December 2019
 * @brief	This is Tensor class for calculation
 * @see		https://github.com/nnstreamer/nntrainer
 * @author	Jijoong Moon <jijoong.moon@samsung.com>
 * @bug		No known bugs except for NYI items
 *
 */

#ifndef __TENSOR_H__
#define __TENSOR_H__
#ifdef __cplusplus

#ifdef USE_BLAS
extern "C" {
#include <cblas.h>
}
#endif

#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <tensor_dim.h>
#include <vector>

namespace nntrainer {

class LazyTensor;
/**
 * @class   Tensor Class for Calculation
 * @brief   Tensor Class for Calculation
 */
class Tensor {
public:
  /**
   * @brief     Constructor of Tensor
   */
  Tensor() : dim(){};

  /**
   * @brief     Constructor of Tensor with batch size one
   * @param[in] dim TensorDim
   */
  Tensor(TensorDim dim);

  /**
   * @brief     Constructor of Tensor with batch size one
   * @param[in] heihgt Height of Tensor
   * @param[in] width Width of Tensor
   */
  Tensor(int height, int width);

  /**
   * @brief     Constructor of Tensor
   * @param[in] channel Channel of Tensor
   * @param[in] heihgt Height of Tensor
   * @param[in] width Width of Tensor
   */
  Tensor(int channel, int height, int width);

  /**
   * @brief     Constructor of Tensor
   * @param[in] batch Batch of Tensor
   * @param[in] channel Channel of Tensor
   * @param[in] heihgt Height of Tensor
   * @param[in] width Width of Tensor
   */
  Tensor(int batch, int channel, int height, int width);

  /**
   * @brief   Constructor of Tensor
   * @param[in] d data for the Tensor with batch size one
   */
  Tensor(std::vector<std::vector<float>> const &d);

  /**
   * @brief     Constructor of Tensor
   * @param[in] d data for the Tensor
   */
  Tensor(std::vector<std::vector<std::vector<float>>> const &d);

  /**
   * @brief     Constructor of Tensor
   * @param[in] d data for the Tensor
   */
  Tensor(std::vector<std::vector<std::vector<std::vector<float>>>> const &d);

  /**
   * @brief     return value at specific location
   * @param[in] batch batch location
   * @param[in] c channel location
   * @param[in] h height location
   * @param[in] w width location
   */
  float getValue(unsigned int batch, unsigned int c, unsigned int h,
                 unsigned int w);

  /**
   * @brief     Multiply value element by element
   * @param[in] value multiplier
   * @retval    Calculated Tensor
   */
  Tensor multiply(float const &value);

  /**
   * @brief     Divide value element by element
   * @param[in] value Divisor
   * @retval    Calculated Tensor
   */
  Tensor divide(float const &value);

  /**
   * @brief Add Tensor Element by Element without mem copy
   * @param[in] m Tensor to be added
   * #retval #ML_ERROR_NONE  Successful
   * #retval #ML_ERROR_INVALID_PARAMETER Invalid Parameter
   */
  int add_i(Tensor const &m);

  /**
   * @brief     Add Tensor Element by Element
   * @param[in] m Tensor to be added
   * @retval    Calculated Tensor
   */
  Tensor add(Tensor const &m) const;

  /**
   * @brief Add Tensor Element immediately to target tensor without mem copy
   * @param[in] value value to be added
   * #retval #ML_ERROR_NONE  Successful
   * #retval #ML_ERROR_INVALID_PARAMETER Invalid Parameter
   */
  int add_i(float const &value);

  /**
   * @brief     Add value Element by Element
   * @param[in] value value to be added
   * @retval    Calculated Tensor
   */
  Tensor add(float const &value);

  /**
   * @brief     Substract Tensor Element by Element
   * @param[in] m Tensor to be added
   * @retval    Calculated Tensor
   */
  Tensor subtract(Tensor const &m) const;

  /**
   * @brief     subtract value Element by Element
   * @param[in] value value to be added
   * @retval    Calculated Tensor
   */
  Tensor subtract(float const &value);

  /**
   * @brief     Multiply Tensor Element by Element ( Not the MxM )
   * @param[in] m Tensor to be multiplied
   * @retval    Calculated Tensor
   */
  Tensor multiply(Tensor const &m) const;

  /**
   * @brief     Divide Tensor Element by Element
   * @param[in] m Divisor Tensor
   * @retval    Calculated Tensor
   */
  Tensor divide(Tensor const &m) const;

  /**
   * @brief     Dot Product of Tensor ( equal MxM )
   * @param[in] m Tensor
   * @retval    Calculated Tensor
   */
  Tensor dot(Tensor const &m) const;

  /**
   * @brief     Transpose Tensor
   * @param[in] direction to transpose ex) 0:2:1
   * @retval    Calculated Tensor
   */
  Tensor transpose(std::string direction) const;

  /**
   * @brief     sum all the Tensor elements according to the batch
   * @retval    Calculated Tensor(batch, 1, 1)
   */
  Tensor sum() const;

  /**
   * @brief     sum all the Tensor elements according to the axis
   *            0 : batch direction
   *            1 : channel direction
   *            2 : channel direction
   *            3 : channel direction
   * @retval    Calculated Tensor
   */
  Tensor sum(int axis) const;

  /**
   * @brief     Averaging the Tensor elements according to the batch
   * @retval    Calculated Tensor(1, height, width)
   */
  Tensor average() const;

  /**
   * @brief     Anchor a starting point to defer following evaluation
   * @retval    LazyTensor class that can be used with run();
   */
  LazyTensor chain() const;

  /**
   * @brief     Softmax the Tensor elements
   * @retval    Calculated Tensor
   */
  Tensor softmax() const;

  /**
   * @brief     l2norm the Tensor elements
   * @retval    Calculated l2norm
   */
  float l2norm() const;

  /**
   * @brief     Normalize the Tensor elements
   * @retval    Calculated Tensor
   */
  Tensor normalization() const;

  /**
   * @brief     Standardize the Tensor elements
   * @retval    Calculated Tensor
   */
  Tensor standardization() const;

  /**
   * @brief     Fill the Tensor elements with zero
   */
  void setZero();

  /**
   * @brief     Reduce Rank ( Tensor to Vector )
   * @retval    Saved vector
   */
  std::vector<float> mat2vec();

  /**
   * @brief     Apply function element by element
   * @param[in] *function function pointer applied
   * @retval    Tensor
   */
  Tensor apply(float (*function)(float)) const;

  /**
   * @brief     Apply function to Tensor
   * @param[in] *function function pointer applied
   * @retval    Tensor
   */
  Tensor apply(Tensor (*function)(Tensor)) const;

  /**
   * @brief     Print element
   * @param[in] out out stream
   * @retval    Tensor
   */
  void print(std::ostream &out) const;

  /**
   * @brief     Get Width of Tensor
   * @retval    int Width
   */
  int getWidth() const { return dim.width(); };

  /**
   * @brief     Get Channel of Tensor
   * @retval    int Channel
   */
  int getChannel() const { return dim.channel(); };

  /**
   * @brief     Get Height of Tensor
   * @retval    int Height
   */
  int getHeight() const { return dim.height(); };

  /**
   * @brief     Get Batch of Tensor
   * @retval    int Batch
   */
  int getBatch() const { return dim.batch(); };

  /**
   * @brief     Set the elelemnt value
   * @param[in] batch batch location
   * @param[in] c channel location
   * @param[in] i height location
   * @param[in] j width location
   * @param[in] value value to be stored
   */
  void setValue(unsigned int batch, unsigned int c, unsigned int i,
                unsigned int j, float value);

  /**
   * @brief     Copy the Tensor
   * @param[in] from Tensor to be Copyed
   * @retval    Matix
   */
  Tensor &copy(Tensor const &from);

  /**
   * @brief     Save the Tensor into file
   * @param[in] file output file stream
   */
  void save(std::ofstream &file);

  /**
   * @brief     Read the Tensor from file
   * @param[in] file input file stream
   */
  void read(std::ifstream &file);

  /**
   * @brief     return argument index which value is max
   * @retval    int argument index
   */
  int argmax();

  /**
   * @brief     return Tensor Dim
   * @retval    TensorDim
   */
  TensorDim getDim() { return dim; }

  /**
   * @brief     return Data pointer of Tensor
   * @retval    float pointer
   */
  float *getData() { return data.data(); }

private:
  /**< handle the data as a std::vector type */
  std::vector<float> data;
  TensorDim dim;
};

/**
 * @brief   Overriding output stream
 */
std::ostream &operator<<(std::ostream &out, Tensor const &m);

} /* namespace nntrainer */

#endif /* __cplusplus */
#endif /* __TENSOR_H__ */
