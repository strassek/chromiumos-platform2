// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "ml/machine_learning_service_impl.h"

#include <memory>
#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/macros.h>
#include <base/run_loop.h>
#include <base/message_loop/message_loop.h>
#include <brillo/message_loops/base_message_loop.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/bindings/interface_request.h>

#include "ml/mojom/graph_executor.mojom.h"
#include "ml/mojom/machine_learning_service.mojom.h"
#include "ml/mojom/model.mojom.h"
#include "ml/tensor_view.h"
#include "ml/test_utils.h"
#include "mojo/edk/embedder/embedder.h"

namespace ml {

namespace {

using ::chromeos::machine_learning::mojom::BuiltinModelId;
using ::chromeos::machine_learning::mojom::BuiltinModelSpec;
using ::chromeos::machine_learning::mojom::BuiltinModelSpecPtr;
using ::chromeos::machine_learning::mojom::CreateGraphExecutorResult;
using ::chromeos::machine_learning::mojom::ExecuteResult;
using ::chromeos::machine_learning::mojom::GraphExecutorPtr;
using ::chromeos::machine_learning::mojom::LoadModelResult;
using ::chromeos::machine_learning::mojom::MachineLearningServicePtr;
using ::chromeos::machine_learning::mojom::ModelPtr;
using ::chromeos::machine_learning::mojom::ModelRequest;
using ::chromeos::machine_learning::mojom::TensorPtr;

const int kSmartDim20190521InputSize = 592;
// TODO(pmalani): Need a better way to determine where model files are stored.
constexpr char kModelDirForFuzzer[] = "/usr/libexec/fuzzers/";

}  // namespace

class MachineLearningServiceImplForTesting : public MachineLearningServiceImpl {
 public:
  // Pass a dummy callback and use the testing model directory.
  explicit MachineLearningServiceImplForTesting(
      mojo::ScopedMessagePipeHandle pipe)
      : MachineLearningServiceImpl(
            std::move(pipe), base::Closure(), std::string(kModelDirForFuzzer)) {
  }
};

class MLServiceFuzzer {
 public:
  MLServiceFuzzer() = default;
  ~MLServiceFuzzer() = default;

  void SetUp() {
    ml_service_impl_ = std::make_unique<MachineLearningServiceImplForTesting>(
        mojo::MakeRequest(&ml_service_).PassMessagePipe());

    // Set up model spec.
    BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
    spec->id = BuiltinModelId::SMART_DIM_20190521;

    // Load model.
    bool model_callback_done = false;
    ml_service_->LoadBuiltinModel(
        std::move(spec), mojo::MakeRequest(&model_),
        base::Bind(
            [](bool* model_callback_done, const LoadModelResult result) {
              CHECK_EQ(result, LoadModelResult::OK);
              *model_callback_done = true;
            },
            &model_callback_done));
    base::RunLoop().RunUntilIdle();
    CHECK(model_callback_done);
    CHECK(model_.is_bound());

    // Get graph executor.
    bool ge_callback_done = false;
    model_->CreateGraphExecutor(
        mojo::MakeRequest(&graph_executor_),
        base::Bind(
            [](bool* ge_callback_done, const CreateGraphExecutorResult result) {
              CHECK_EQ(result, CreateGraphExecutorResult::OK);
              *ge_callback_done = true;
            },
            &ge_callback_done));
    base::RunLoop().RunUntilIdle();
    CHECK(ge_callback_done);
    CHECK(graph_executor_.is_bound());
  }

  void PerformInference(const uint8_t* data, size_t size) {
    // Construct input.
    std::unordered_map<std::string, TensorPtr> inputs;

    // Create input vector
    FuzzedDataProvider data_provider(data, size);
    std::vector<double> input_vec;
    for (int i = 0; i < kSmartDim20190521InputSize; i++)
      input_vec.push_back(
          data_provider.ConsumeFloatingPointInRange<double>(0, 1));

    inputs.emplace(
        "input", NewTensor<double>({1, kSmartDim20190521InputSize}, input_vec));
    std::vector<std::string> outputs({"output"});

    // Perform inference.
    bool infer_callback_done = false;
    graph_executor_->Execute(
        std::move(inputs), std::move(outputs),
        base::Bind(
            [](bool* infer_callback_done, const ExecuteResult result,
               base::Optional<std::vector<TensorPtr>> outputs) {
              // Basic inference checks.
              CHECK_EQ(result, ExecuteResult::OK);
              CHECK(outputs.has_value());
              CHECK_EQ(outputs->size(), 1);

              const TensorView<double> out_tensor((*outputs)[0]);
              CHECK(out_tensor.IsValidType());
              CHECK(out_tensor.IsValidFormat());
              *infer_callback_done = true;
            },
            &infer_callback_done));
    base::RunLoop().RunUntilIdle();
    CHECK(infer_callback_done);
  }

 private:
  MachineLearningServicePtr ml_service_;
  std::unique_ptr<MachineLearningServiceImplForTesting> ml_service_impl_;
  ModelPtr model_;
  GraphExecutorPtr graph_executor_;

  DISALLOW_COPY_AND_ASSIGN(MLServiceFuzzer);
};

}  // namespace ml

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  base::AtExitManager at_exit_manager;

  // Mock main task runner
  base::MessageLoopForIO message_loop;
  brillo::BaseMessageLoop brillo_loop(&message_loop);
  brillo_loop.SetAsCurrent();

  mojo::edk::Init();
  mojo::edk::InitIPCSupport(base::ThreadTaskRunnerHandle::Get());

  ml::MLServiceFuzzer fuzzer;
  fuzzer.SetUp();
  fuzzer.PerformInference(data, size);

  return 0;
}