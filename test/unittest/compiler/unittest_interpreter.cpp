// SPDX-License-Identifier: Apache-2.0
/**
 * Copyright (C) 2021 Jihoon Lee <jhoon.it.lee@samsung.com>
 *
 * @file unittest_interpreter.cpp
 * @date 02 April 2021
 * @brief interpreter test
 * @see	https://github.com/nnstreamer/nntrainer
 * @author Jihoon Lee <jhoon.it.lee@samsung.com>
 * @bug No known bugs except for NYI items
 */

#include <functional>
#include <gtest/gtest.h>
#include <memory>

#include <app_context.h>
#include <execution_mode.h>
#include <ini_interpreter.h>
#include <interpreter.h>
#include <layer.h>
#include <layer_node.h>
#include <network_graph.h>
#include <node_exporter.h>

#ifdef ENABLE_TFLITE_INTERPRETER
#include <tflite_interpreter.h>

#include <tensorflow/contrib/lite/interpreter.h>
#include <tensorflow/contrib/lite/kernels/register.h>
#include <tensorflow/contrib/lite/model.h>
#endif

#include <app_context.h>
#include <compiler_test_util.h>
#include <nntrainer_test_util.h>

using LayerRepresentation = std::pair<std::string, std::vector<std::string>>;

auto ini_interpreter = std::make_shared<nntrainer::IniGraphInterpreter>(
  nntrainer::AppContext::Global(), compilerPathResolver);

/**
 * @brief nntrainer Interpreter Test setup
 *
 * @note Proposing an evolutional path of current test
 * 1. A reference graph vs given paramter
 * 2. A reference graph vs list of models
 * 3. A reference graph vs (pick two models) a -> b -> a graph, b -> a -> b
 * graph
 */
class nntrainerInterpreterTest
  : public ::testing::TestWithParam<
      std::tuple<nntrainer::GraphRepresentation, const char *,
                 std::shared_ptr<nntrainer::GraphInterpreter>>> {

protected:
  virtual void SetUp() {
    auto params = GetParam();

    reference = std::get<0>(params);
    file_path = compilerPathResolver(std::get<1>(params));
    interpreter = std::move(std::get<2>(params));
  }

  nntrainer::GraphRepresentation reference;
  std::shared_ptr<nntrainer::GraphInterpreter> interpreter;
  std::string file_path;
};

/**
 * @brief Check two compiled graph is equal
 * @note later this will be more complicated (getting N graph and compare each
 * other)
 *
 */
TEST_P(nntrainerInterpreterTest, graphEqual) {
  std::cout << "testing " << file_path << '\n';

  // int status = reference->compile("");
  // EXPECT_EQ(status, ML_ERROR_NONE);
  auto g = interpreter->deserialize(file_path);

  /// @todo: change this to something like graph::finalize
  // status = g->compile("");
  // EXPECT_EQ(status, ML_ERROR_NONE);

  /// @todo: make a proper graph equal
  /// 1. having same number of nodes
  /// 2. layer name is identical (this is too strict though)
  /// 3. attributes of layer is identical
  graphEqual(g, reference);
}

/**
 * @brief graph serialize after deserialize, compare if they are the same
 *
 */
TEST_P(nntrainerInterpreterTest, graphSerializeAfterDeserialize) {
  auto g = interpreter->deserialize(file_path);

  auto out_file_path = file_path + ".out";

  /// @todo: change this to something like graph::finalize
  // int status = g->compile("");
  // EXPECT_EQ(status, ML_ERROR_NONE);
  interpreter->serialize(g, out_file_path);
  auto new_g = interpreter->deserialize(out_file_path);

  graphEqual(g, new_g);

  EXPECT_EQ(remove(out_file_path.c_str()), 0) << strerror(errno);
}

auto fc0 = LayerRepresentation("fully_connected",
                               {"name=fc0", "unit=2", "input_shape=1:1:100"});
auto fc1 = LayerRepresentation("fully_connected", {"name=fc1", "unit=2"});

auto flatten = LayerRepresentation("flatten", {"name=flat"});

#ifdef ENABLE_TFLITE_INTERPRETER

/**
 * TODO: update tflite interpreter after the change of semantics that tensors
 * are different between input and output of a layer but the underlying data
 * is same. Once the interpreter is updated, this test can be enabled.
 */
TEST(nntrainerInterpreterTflite, simple_fc) {

  nntrainer::TfliteInterpreter interpreter;

  auto fc0_zeroed = LayerRepresentation(
    "fully_connected", {"name=fc0", "unit=2", "input_shape=1:1:1",
                        "bias_initializer=ones", "weight_initializer=ones"});

  auto fc1_zeroed = LayerRepresentation(
    "fully_connected", {"name=fc1", "unit=2", "bias_initializer=ones",
                        "weight_initializer=ones", "input_layers=fc0"});

  auto g = makeGraph({fc0_zeroed, fc1_zeroed});

  nntrainer::NetworkGraph ng;

  for (auto &node : g) {
    ng.addLayer(node);
  }
  EXPECT_EQ(ng.compile(""), ML_ERROR_NONE);
  EXPECT_EQ(ng.initialize(), ML_ERROR_NONE);

  ng.allocateTensors(nntrainer::ExecutionMode::INFERENCE);
  interpreter.serialize(g, "test.tflite");
  ng.deallocateTensors();

  tflite::ops::builtin::BuiltinOpResolver resolver;
  std::unique_ptr<tflite::Interpreter> tf_interpreter;
  std::unique_ptr<tflite::FlatBufferModel> model =
    tflite::FlatBufferModel::BuildFromFile("test.tflite");
  EXPECT_NE(model, nullptr);
  tflite::InterpreterBuilder(*model, resolver)(&tf_interpreter);
  EXPECT_NE(tf_interpreter, nullptr);

  EXPECT_EQ(tf_interpreter->AllocateTensors(), kTfLiteOk);

  nntrainer::Tensor in(nntrainer::TensorDim({1, 1, 1, 1}));
  in.setValue(2.0f);
  nntrainer::Tensor out(nntrainer::TensorDim({1, 1, 1, 2}));

  auto in_indices = tf_interpreter->inputs();
  for (size_t idx = 0; idx < in_indices.size(); idx++) {
    tf_interpreter->tensor(in_indices[idx])->data.raw =
      reinterpret_cast<char *>(in.getData());
  }

  auto out_indices = tf_interpreter->outputs();
  for (size_t idx = 0; idx < out_indices.size(); idx++) {
    tf_interpreter->tensor(out_indices[idx])->data.raw =
      reinterpret_cast<char *>(out.getData());
  }

  int status = tf_interpreter->Invoke();
  EXPECT_EQ(status, TfLiteStatus::kTfLiteOk);

  nntrainer::Tensor ans(nntrainer::TensorDim({1, 1, 1, 2}));
  ans.setValue(7.0f);

  EXPECT_EQ(out, ans);

  if (remove("test.tflite")) {
    std::cerr << "remove ini "
              << "test.tflite"
              << "failed, reason: " << strerror(errno);
  }
}
#endif
/**
 * @brief make ini test case from given parameter
 */
static std::tuple<nntrainer::GraphRepresentation, const char *,
                  std::shared_ptr<nntrainer::GraphInterpreter>>
mkTc(nntrainer::GraphRepresentation graph, const char *file,
     std::shared_ptr<nntrainer::GraphInterpreter> interpreter) {
  return std::make_tuple(graph, file, interpreter);
}

// clang-format off
INSTANTIATE_TEST_CASE_P(nntrainerAutoInterpreterTest, nntrainerInterpreterTest,
                        ::testing::Values(
  mkTc(makeGraph({fc0, flatten}), "simple_fc.ini", ini_interpreter),
  mkTc(makeGraph({fc0, flatten}), "simple_fc_backbone.ini", ini_interpreter)
));
// clang-format on
