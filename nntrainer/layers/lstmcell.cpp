// SPDX-License-Identifier: Apache-2.0
/**
 * Copyright (C) 2021 Parichay Kapoor <pk.kapoor@samsung.com>
 *
 * @file   lstmcell.cpp
 * @date   17 March 2021
 * @brief  This is LSTMCell Layer Class of Neural Network
 * @see    https://github.com/nnstreamer/nntrainer
 * @author Parichay Kapoor <pk.kapoor@samsung.com>
 * @bug    No known bugs except for NYI items
 *
 */

#include <cmath>
#include <layer_context.h>
#include <lstmcell.h>
#include <nntrainer_error.h>
#include <nntrainer_log.h>
#include <node_exporter.h>
#include <util_func.h>

namespace nntrainer {

static constexpr size_t SINGLE_INOUT_IDX = 0;

enum LSTMParams {
  weight_xh,
  weight_hh,
  bias_h,
  hidden_state,
  mem_cell,
  fgio,
  dropout_mask
};

LSTMCellLayer::LSTMCellLayer() :
  LayerImpl(),
  lstm_props(props::Unit(), props::HiddenStateActivation(),
             props::RecurrentActivation(), props::DropOutRate(),
             props::MaxTimestep(), props::Timestep()),
  wt_idx({0}),
  acti_func(ActivationType::ACT_NONE, true),
  recurrent_acti_func(ActivationType::ACT_NONE, true),
  epsilon(1e-3) {}

// - weight_xh ( input to hidden )
//  : [1, 1, input_size, unit (hidden_size) x NUM_GATE] -> f, g, i, o
// - weight_hh ( hidden to hidden )
//  : [1, 1, unit (hidden_size) , unit (hidden_size) x NUM_GATE] -> f, g, i, o
// - bias_h ( hidden bias )
//  : [1, 1, 1, unit (hidden_size) x NUM_GATE] -> f, g, i, o
void LSTMCellLayer::finalize(InitLayerContext &context) {
  auto &weight_regularizer =
    std::get<props::WeightRegularizer>(*layer_impl_props);
  auto &weight_regularizer_constant =
    std::get<props::WeightRegularizerConstant>(*layer_impl_props);
  auto &weight_initializer =
    std::get<props::WeightInitializer>(*layer_impl_props);
  auto &bias_initializer = std::get<props::BiasInitializer>(*layer_impl_props);

  NNTR_THROW_IF(std::get<props::Unit>(lstm_props).empty(),
                std::invalid_argument)
    << "unit property missing for lstm layer";
  auto unit = std::get<props::Unit>(lstm_props).get();
  auto &hidden_state_activation_type =
    std::get<props::HiddenStateActivation>(lstm_props);
  auto &recurrent_activation_type =
    std::get<props::RecurrentActivation>(lstm_props);
  float dropout_rate = std::get<props::DropOutRate>(lstm_props);

  if (context.getNumInputs() != 1)
    throw std::invalid_argument("LSTM layer takes only one input");
  if (std::get<props::MaxTimestep>(lstm_props).empty())
    throw std::invalid_argument(
      "Number of unroll steps must be provided to LSTM cells");
  if (std::get<props::Timestep>(lstm_props).empty())
    throw std::invalid_argument(
      "Current Timestep must be provided to LSTM cell");

  // input_dim = [ batch, 1, 1, feature_size ]
  TensorDim output_dim;
  const TensorDim &input_dim = context.getInputDimensions()[0];
  if (input_dim.height() != 1 && input_dim.channel() != 1)
    throw std::invalid_argument(
      "Input must be single time dimension for LSTMCell");
  // output_dim = [ batch, 1, 1, hidden_size (unit)]
  output_dim = input_dim;
  output_dim.width(unit);

  if (dropout_rate > epsilon) {
    wt_idx[LSTMParams::dropout_mask] = context.requestTensor(
      output_dim, "dropout_mask", Tensor::Initializer::NONE, false,
      TensorLifespan::ITERATION_LIFESPAN);
  }

  context.setOutputDimensions({output_dim});

  TensorDim bias_dim = TensorDim();
  bias_dim.setTensorDim(3, unit * NUM_GATE);

  TensorDim dim_xh = output_dim;
  dim_xh.height(input_dim.width());
  dim_xh.width(unit * NUM_GATE);
  dim_xh.batch(1);

  TensorDim dim_hh = output_dim;
  dim_hh.height(unit);
  dim_hh.width(unit * NUM_GATE);
  dim_hh.batch(1);

  // weight_initializer can be set seperately. weight_xh initializer,
  // weight_hh initializer kernel initializer & recurrent_initializer in keras
  // for now, it is set same way.
  wt_idx[LSTMParams::weight_xh] =
    context.requestWeight(dim_xh, weight_initializer, weight_regularizer,
                          weight_regularizer_constant, "weight_xh", true);
  wt_idx[LSTMParams::weight_hh] =
    context.requestWeight(dim_hh, weight_initializer, weight_regularizer,
                          weight_regularizer_constant, "weight_hh", true);
  wt_idx[LSTMParams::bias_h] = context.requestWeight(
    bias_dim, bias_initializer, WeightRegularizer::NONE, 1.0f, "bias_h", true);

  unsigned int max_timestep = std::get<props::MaxTimestep>(lstm_props);

  TensorDim d = input_dim;
  // d.height(d.batch());
  d.height(1);
  d.batch(max_timestep * d.batch());
  d.width(unit);

  /** hidden dim = [ UnrollLength, 1, Batch, Units ] */
  wt_idx[LSTMParams::hidden_state] =
    context.requestTensor(d, "hidden_state", Tensor::Initializer::NONE, true,
                          TensorLifespan::ITERATION_LIFESPAN);
  wt_idx[LSTMParams::mem_cell] =
    context.requestTensor(d, "mem_cell", Tensor::Initializer::NONE, true,
                          TensorLifespan::ITERATION_LIFESPAN);

  /**
   * TODO: make this independent of time dimension once recurrent realizer
   * supports requesting tensors which are not always shared
   *
   * TODO: reorder to ifgo for better performance. This will require change in
   * stored weights in the test
   */
  d.width(unit * NUM_GATE);
  wt_idx[LSTMParams::fgio] =
    context.requestTensor(d, "fgio", Tensor::Initializer::NONE, true,
                          TensorLifespan::ITERATION_LIFESPAN);

  if (hidden_state_activation_type.get() == ActivationType::ACT_NONE) {
    hidden_state_activation_type.set(ActivationType::ACT_TANH);
  }
  acti_func.setActiFunc(hidden_state_activation_type.get());

  if (recurrent_activation_type.get() == ActivationType::ACT_NONE) {
    recurrent_activation_type.set(ActivationType::ACT_SIGMOID);
  }
  recurrent_acti_func.setActiFunc(recurrent_activation_type.get());
}

void LSTMCellLayer::setProperty(const std::vector<std::string> &values) {
  auto remain_props = loadProperties(values, lstm_props);
  LayerImpl::setProperty(remain_props);
}

void LSTMCellLayer::exportTo(Exporter &exporter,
                             const ExportMethods &method) const {
  LayerImpl::exportTo(exporter, method);
  exporter.saveResult(lstm_props, method, this);
}

void LSTMCellLayer::forwarding(RunLayerContext &context, bool training) {
  auto unit = std::get<props::Unit>(lstm_props).get();
  float dropout_rate = std::get<props::DropOutRate>(lstm_props);

  Tensor &weight_xh = context.getWeight(wt_idx[LSTMParams::weight_xh]);
  Tensor &weight_hh = context.getWeight(wt_idx[LSTMParams::weight_hh]);
  Tensor &bias_h = context.getWeight(wt_idx[LSTMParams::bias_h]);

  Tensor &input_ = context.getInput(SINGLE_INOUT_IDX);
  Tensor &hidden_ = context.getTensor(wt_idx[LSTMParams::hidden_state]);
  Tensor &cell_ = context.getTensor(wt_idx[LSTMParams::mem_cell]);
  Tensor &fgio = context.getTensor(wt_idx[LSTMParams::fgio]);
  const TensorDim &input_dim = input_.getDim();
  unsigned int batch = input_dim.batch();

  unsigned int start_timestep = std::get<props::Timestep>(lstm_props);

  if (start_timestep == 0) {
    hidden_.setZero();
    cell_.setZero();
  }

  unsigned int max_timestep = std::get<props::MaxTimestep>(lstm_props);
  hidden_.reshape({max_timestep, 1, batch, hidden_.width()});
  cell_.reshape({max_timestep, 1, batch, cell_.width()});
  fgio.reshape({max_timestep, 1, batch, fgio.width()});

  /**
   * @note when the recurrent realization happens, different instances of lstm
   * will share the weights, hidden state, cell and fgio memory. However, they
   * do not share the input, output and derivatives memory. The input/output
   * will be contain a single timestep data only.
   */
  Tensor hs = hidden_.getBatchSlice(start_timestep, 1);
  Tensor cs = cell_.getBatchSlice(start_timestep, 1);
  Tensor fgio_t = fgio.getBatchSlice(start_timestep, 1);

  input_.dot(weight_xh, fgio_t);

  if (start_timestep > 0) {
    Tensor hs_prev = hidden_.getBatchSlice(start_timestep - 1, 1);
    hs_prev.dot(weight_hh, fgio_t, false, false, 1.0);
  }

  fgio_t.add_i(bias_h);
  Tensor hif = fgio_t.getSharedDataTensor({batch, unit * 2}, 0, false);
  Tensor hi = fgio_t.getSharedDataTensor({batch, unit}, 0, false);
  Tensor hf = fgio_t.getSharedDataTensor({batch, unit}, unit, false);
  Tensor hg = fgio_t.getSharedDataTensor({batch, unit}, unit * 2, false);
  Tensor ho = fgio_t.getSharedDataTensor({batch, unit}, unit * 3, false);

  recurrent_acti_func.run_fn(hif, hif);
  recurrent_acti_func.run_fn(ho, ho);
  acti_func.run_fn(hg, hg);

  if (start_timestep > 0) {
    Tensor cs_prev = cell_.getBatchSlice(start_timestep - 1, 1);
    hf.multiply_strided(cs_prev, cs);
  }
  hg.multiply_strided(hi, cs, 1.0);

  acti_func.run_fn(cs, hs);
  hs.multiply_i_strided(ho);

  if (dropout_rate > epsilon && training) {
    Tensor &mask_ = context.getTensor(wt_idx[LSTMParams::dropout_mask]);
    hs.dropout_mask(dropout_rate);
    hs.multiply_i(mask_);
  }

  Tensor &output = context.getOutput(SINGLE_INOUT_IDX);
  std::copy(hs.getData(), hs.getData() + hs.size(), output.getData());
}

void LSTMCellLayer::calcDerivative(RunLayerContext &context) {
  Tensor &derivative_ = context.getTensorGrad(wt_idx[LSTMParams::fgio]);
  Tensor &weight = context.getWeight(wt_idx[LSTMParams::weight_xh]);
  Tensor &ret_ = context.getOutgoingDerivative(SINGLE_INOUT_IDX);

  unsigned int max_timestep = std::get<props::MaxTimestep>(lstm_props);
  derivative_.reshape({max_timestep, 1, ret_.batch(), derivative_.width()});

  /** get the timestep values */
  unsigned int start_timestep = std::get<props::Timestep>(lstm_props);

  Tensor deriv_t = derivative_.getBatchSlice(start_timestep, 1);
  deriv_t.dot(weight, ret_, false, true);
}

void LSTMCellLayer::calcGradient(RunLayerContext &context) {
  auto unit = std::get<props::Unit>(lstm_props).get();
  float dropout_rate = std::get<props::DropOutRate>(lstm_props);

  Tensor &djdw_x = context.getWeightGrad(wt_idx[LSTMParams::weight_xh]);
  Tensor &djdw_h = context.getWeightGrad(wt_idx[LSTMParams::weight_hh]);
  Tensor &djdb_h = context.getWeightGrad(wt_idx[LSTMParams::bias_h]);
  Tensor &weight_hh = context.getWeight(wt_idx[LSTMParams::weight_hh]);

  Tensor &derivative_ = context.getTensorGrad(wt_idx[LSTMParams::hidden_state]);
  /**
   * TODO: hidden_ is only used from the previous timestep. Once it is supported
   * as input, no need to cache the hidden_ itself
   */
  Tensor &hidden_ = context.getTensor(wt_idx[LSTMParams::hidden_state]);
  Tensor &incoming_deriv = context.getIncomingDerivative(SINGLE_INOUT_IDX);
  Tensor &input_ = context.getInput(SINGLE_INOUT_IDX);
  Tensor &m_cell_ = context.getTensor(wt_idx[LSTMParams::mem_cell]);
  Tensor &dm_cell_ = context.getTensorGrad(wt_idx[LSTMParams::mem_cell]);
  Tensor &fgio = context.getTensor(wt_idx[LSTMParams::fgio]);
  Tensor &d_fgio = context.getTensorGrad(wt_idx[LSTMParams::fgio]);
  const TensorDim &input_dim = input_.getDim();
  unsigned int batch = input_dim.batch();

  /** get the timestep values */
  unsigned int max_timestep = std::get<props::MaxTimestep>(lstm_props);
  unsigned int start_timestep = std::get<props::Timestep>(lstm_props);

  derivative_.reshape({max_timestep, 1, batch, derivative_.width()});
  hidden_.reshape({max_timestep, 1, batch, hidden_.width()});
  m_cell_.reshape({max_timestep, 1, batch, m_cell_.width()});
  dm_cell_.reshape({max_timestep, 1, batch, dm_cell_.width()});
  fgio.reshape({max_timestep, 1, batch, fgio.width()});
  d_fgio.reshape({max_timestep, 1, batch, d_fgio.width()});

  if (start_timestep + 1 == max_timestep) {
    djdw_x.setZero();
    djdw_h.setZero();
    djdb_h.setZero();
  }

  Tensor dh = derivative_.getBatchSlice(start_timestep, 1);
  dh.reshape(incoming_deriv.getDim());
  if (start_timestep + 1 == max_timestep) {
    dh.copyData(incoming_deriv);
  } else {
    dh.add_i(incoming_deriv);
  }
  dh = derivative_.getBatchSlice(start_timestep, 1);

  if (dropout_rate > epsilon) {
    derivative_.multiply_i(context.getTensor(wt_idx[LSTMParams::dropout_mask]));
  }

  Tensor dc = dm_cell_.getBatchSlice(start_timestep, 1);
  Tensor xs = input_;
  Tensor hs_t = hidden_.getBatchSlice(start_timestep, 1);
  Tensor cs = m_cell_.getBatchSlice(start_timestep, 1);

  Tensor dfgio_t = d_fgio.getBatchSlice(start_timestep, 1);
  Tensor fgio_t = fgio.getBatchSlice(start_timestep, 1);

  Tensor dhif = dfgio_t.getSharedDataTensor({batch, unit * 2}, 0, false);
  Tensor dhi = dfgio_t.getSharedDataTensor({batch, unit}, 0, false);
  Tensor dhf = dfgio_t.getSharedDataTensor({batch, unit}, unit, false);
  Tensor dhg = dfgio_t.getSharedDataTensor({batch, unit}, unit * 2, false);
  Tensor dho = dfgio_t.getSharedDataTensor({batch, unit}, unit * 3, false);

  Tensor hif = fgio_t.getSharedDataTensor({batch, unit * 2}, 0, false);
  Tensor hi = fgio_t.getSharedDataTensor({batch, unit}, 0, false);
  Tensor hf = fgio_t.getSharedDataTensor({batch, unit}, unit, false);
  Tensor hg = fgio_t.getSharedDataTensor({batch, unit}, unit * 2, false);
  Tensor ho = fgio_t.getSharedDataTensor({batch, unit}, unit * 3, false);

  acti_func.run_fn(cs, cs);
  cs.multiply_strided(dh, dho);

  if (start_timestep + 1 == max_timestep) {
    acti_func.run_prime_fn(cs, dc, dh);
    dc.multiply_i_strided(ho);
  } else {
    /// @todo optimize this by updating run_prime_fn to accumulate or make
    /// it inplace somehow
    Tensor dc_temp(dc.getDim());
    acti_func.run_prime_fn(cs, dc_temp, dh);
    dc_temp.multiply_strided(ho, dc, 1.0);
  }

  if (start_timestep > 0) {
    Tensor dc_nx = dm_cell_.getBatchSlice(start_timestep - 1, 1);
    dc.multiply_strided(hf, dc_nx);
    Tensor cs_prev = m_cell_.getBatchSlice(start_timestep - 1, 1);
    dc.multiply_strided(cs_prev, dhf);
  } else {
    dhf.setZero();
  }

  dc.multiply_strided(hg, dhi);
  dc.multiply_strided(hi, dhg);

  recurrent_acti_func.run_prime_fn(ho, dho, dho);
  recurrent_acti_func.run_prime_fn(hif, dhif, dhif);
  acti_func.run_prime_fn(hg, dhg, dhg);
  dfgio_t.sum(2, djdb_h, 1.0, 1.0);

  xs.dot(dfgio_t, djdw_x, true, false, 1.0f);
  if (start_timestep != 0) {
    Tensor hs_prev = hidden_.getBatchSlice(start_timestep - 1, 1);
    hs_prev.dot(dfgio_t, djdw_h, true, false, 1.0f);
    Tensor dh_nx = derivative_.getBatchSlice(start_timestep - 1, 1);
    dfgio_t.dot(weight_hh, dh_nx, false, true, 1.0f);
  }
}

void LSTMCellLayer::setBatch(RunLayerContext &context, unsigned int batch) {
  unsigned int max_timestep = std::get<props::MaxTimestep>(lstm_props);
  context.updateTensor(wt_idx[LSTMParams::hidden_state], batch * max_timestep);
  context.updateTensor(wt_idx[LSTMParams::mem_cell], batch * max_timestep);
  context.updateTensor(wt_idx[LSTMParams::fgio], batch * max_timestep);
  context.updateTensor(wt_idx[LSTMParams::dropout_mask], batch);
}

} // namespace nntrainer
