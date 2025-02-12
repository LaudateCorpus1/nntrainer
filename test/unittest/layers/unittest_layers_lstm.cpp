// SPDX-License-Identifier: Apache-2.0
/**
 * Copyright (C) 2021 Parichay Kapoor <pk.kapoor@samsung.com>
 *
 * @file unittest_layers_lstm.cpp
 * @date 8 July 2021
 * @brief LSTM Layer Test
 * @see	https://github.com/nnstreamer/nntrainer
 * @author Parichay Kapoor <pk.kapoor@samsung.com>
 * @bug No known bugs except for NYI items
 */
#include <tuple>

#include <gtest/gtest.h>

#include <layers_common_tests.h>
#include <lstm.h>

auto semantic_lstm =
  LayerSemanticsParamType(nntrainer::createLayer<nntrainer::LSTMLayer>,
                          nntrainer::LSTMLayer::type, {"unit=1"}, 0, false, 1);

INSTANTIATE_TEST_CASE_P(LSTM, LayerSemantics, ::testing::Values(semantic_lstm));

auto lstm_single_step = LayerGoldenTestParamType(
  nntrainer::createLayer<nntrainer::LSTMLayer>, {"unit=5"}, "3:1:1:7",
  "lstm_single_step.nnlayergolden", LayerGoldenTestParamOptions::DEFAULT);

auto lstm_multi_step = LayerGoldenTestParamType(
  nntrainer::createLayer<nntrainer::LSTMLayer>, {"unit=5"}, "3:1:4:7",
  "lstm_multi_step.nnlayergolden", LayerGoldenTestParamOptions::DEFAULT);

auto lstm_single_step_seq = LayerGoldenTestParamType(
  nntrainer::createLayer<nntrainer::LSTMLayer>,
  {"unit=5", "return_sequences=true"}, "3:1:1:7",
  "lstm_single_step_seq.nnlayergolden", LayerGoldenTestParamOptions::DEFAULT);

auto lstm_multi_step_seq = LayerGoldenTestParamType(
  nntrainer::createLayer<nntrainer::LSTMLayer>,
  {"unit=5", "return_sequences=true"}, "3:1:4:7",
  "lstm_multi_step_seq.nnlayergolden", LayerGoldenTestParamOptions::DEFAULT);

auto lstm_multi_step_seq_act_orig = LayerGoldenTestParamType(
  nntrainer::createLayer<nntrainer::LSTMLayer>,
  {"unit=5", "return_sequences=true", "hidden_state_activation=tanh",
   "recurrent_activation=sigmoid"},
  "3:1:4:7", "lstm_multi_step_seq.nnlayergolden",
  LayerGoldenTestParamOptions::DEFAULT);

auto lstm_multi_step_seq_act = LayerGoldenTestParamType(
  nntrainer::createLayer<nntrainer::LSTMLayer>,
  {"unit=5", "return_sequences=true", "hidden_state_activation=sigmoid",
   "recurrent_activation=tanh"},
  "3:1:4:7", "lstm_multi_step_seq_act.nnlayergolden",
  LayerGoldenTestParamOptions::DEFAULT);

INSTANTIATE_TEST_CASE_P(LSTM, LayerGoldenTest,
                        ::testing::Values(lstm_single_step, lstm_multi_step,
                                          lstm_single_step_seq,
                                          lstm_multi_step_seq,
                                          lstm_multi_step_seq_act_orig,
                                          lstm_multi_step_seq_act));
