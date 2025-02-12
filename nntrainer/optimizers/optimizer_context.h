// SPDX-License-Identifier: Apache-2.0
/**
 * Copyright (C) 2021 Parichay Kapoor <pk.kapoor@samsung.com>
 *
 * @file   optimizer_context.h
 * @date   30 July 2021
 * @see    https://github.com/nnstreamer/nntrainer
 * @author Parichay Kapoor <pk.kapoor@samsung.com>
 * @bug    No known bugs except for NYI items
 * @brief  This is the optimizer context for each optimizer
 */

#ifndef __OPTIMIZER_CONTEXT_H__
#define __OPTIMIZER_CONTEXT_H__

#include <memory>
#include <vector>

#include <tensor.h>

namespace nntrainer {

class Weight;

/**
 * @class   Op Context class for all optimizers
 * @brief   Class for Optimizer context
 *
 * @details This provides for the optimizer execution.
 */
class RunOptimizerContext {
public:
  /**
   * @brief Construct a new Run Optimizer Context object
   *
   */
  RunOptimizerContext(Weight *w = nullptr, size_t iter = 0) :
    weight(w),
    iteration(iter) {}

  /**
   * @brief Get the Weight tensor object
   *
   * @return Tensor& Reference to the weight tensor
   */
  Tensor &getWeight() const;

  /**
   * @brief Get the Weight Gradient tensor object
   *
   * @return Tensor& Reference to the weight grad tensor
   */
  Tensor &getGradient() const;

  /**
   * @brief Get the optimizer variable associated to this weight
   *
   * @param idx Identifier of the associated weight
   * @return Tensor& Reference to the optimizer variable
   */
  Tensor &getOptimizerVariable(unsigned int idx) const;

  /**
   * @brief   Check if run context is set and is ready to use
   *
   * @return true if ready, else false
   */
  bool readyToUse() const { return weight != nullptr; }

  /**
   * @brief   Apply the gradient with the given learning rate
   *
   * @param lr learning rate
   */
  void applyGradient(double lr) const;

  /**
   * @brief   Get the current iteration value
   *
   * @return iteration value
   */
  size_t getIteration() const { return iteration; }

private:
  Weight *weight;   /**< weights for the optimizer */
  size_t iteration; /**< iteration number */
};

} // namespace nntrainer
#endif // __OPTIMIZER_CONTEXT_H__
