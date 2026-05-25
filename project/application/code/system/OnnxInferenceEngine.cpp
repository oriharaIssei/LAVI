#include "OnnxInferenceEngine.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <thread>

struct OnnxInferenceEngine::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "lavi"};
    std::unique_ptr<Ort::Session> session;

    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    std::vector<const char*> inputNamePtrs;   // session.Run 用 (inputNames を指す)
    std::vector<const char*> outputNamePtrs;

    std::string lastError;
};

OnnxInferenceEngine::OnnxInferenceEngine()
    : impl_(std::make_unique<Impl>()) {}

OnnxInferenceEngine::~OnnxInferenceEngine() = default;

bool OnnxInferenceEngine::LoadModel(const std::wstring& modelPath, bool useGpu) {
    UnloadModel();
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(
            (std::max)(1, static_cast<int>(std::thread::hardware_concurrency())));
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        if (useGpu) {
            try {
                OrtCUDAProviderOptions cudaOpts{};
                opts.AppendExecutionProvider_CUDA(cudaOpts);
            } catch (const Ort::Exception& e) {
                // CUDA EP が使えない場合は CPU で続行
                impl_->lastError = std::string("CUDA EP unavailable, falling back to CPU: ") + e.what();
            }
        }

        impl_->session = std::make_unique<Ort::Session>(impl_->env, modelPath.c_str(), opts);

        Ort::AllocatorWithDefaultOptions allocator;

        const size_t inCount = impl_->session->GetInputCount();
        impl_->inputNames.reserve(inCount);
        for (size_t i = 0; i < inCount; ++i) {
            auto name = impl_->session->GetInputNameAllocated(i, allocator);
            impl_->inputNames.emplace_back(name.get());
        }

        const size_t outCount = impl_->session->GetOutputCount();
        impl_->outputNames.reserve(outCount);
        for (size_t i = 0; i < outCount; ++i) {
            auto name = impl_->session->GetOutputNameAllocated(i, allocator);
            impl_->outputNames.emplace_back(name.get());
        }

        // session.Run に渡す const char* 配列を作る (文字列の実体は inputNames が保持)
        impl_->inputNamePtrs.clear();
        for (const auto& n : impl_->inputNames) impl_->inputNamePtrs.push_back(n.c_str());
        impl_->outputNamePtrs.clear();
        for (const auto& n : impl_->outputNames) impl_->outputNamePtrs.push_back(n.c_str());

        return true;
    } catch (const Ort::Exception& e) {
        impl_->lastError = e.what();
        impl_->session.reset();
        return false;
    }
}

void OnnxInferenceEngine::UnloadModel() {
    impl_->session.reset();
    impl_->inputNames.clear();
    impl_->outputNames.clear();
    impl_->inputNamePtrs.clear();
    impl_->outputNamePtrs.clear();
}

bool OnnxInferenceEngine::IsModelLoaded() const {
    return impl_->session != nullptr;
}

bool OnnxInferenceEngine::Run(const OnnxTensor& input, OnnxTensor& output) {
    std::vector<OnnxTensor> outs;
    if (!Run(std::vector<OnnxTensor>{input}, outs) || outs.empty()) {
        return false;
    }
    output = std::move(outs.front());
    return true;
}

bool OnnxInferenceEngine::Run(const std::vector<OnnxTensor>& inputs, std::vector<OnnxTensor>& outputs) {
    if (!impl_->session) {
        impl_->lastError = "model not loaded";
        return false;
    }
    if (inputs.size() != impl_->inputNames.size()) {
        impl_->lastError = "input count mismatch";
        return false;
    }

    try {
        Ort::MemoryInfo memInfo =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // CreateTensor はバッファを借用するだけなので、Run 完了まで生存させる必要がある。
        std::vector<std::vector<float>> inputBuffers(inputs.size());
        std::vector<Ort::Value> inputTensors;
        inputTensors.reserve(inputs.size());

        for (size_t i = 0; i < inputs.size(); ++i) {
            inputBuffers[i] = inputs[i].data;
            inputTensors.push_back(Ort::Value::CreateTensor<float>(
                memInfo,
                inputBuffers[i].data(),
                inputBuffers[i].size(),
                inputs[i].shape.data(),
                inputs[i].shape.size()));
        }

        auto outValues = impl_->session->Run(
            Ort::RunOptions{nullptr},
            impl_->inputNamePtrs.data(),
            inputTensors.data(),
            inputTensors.size(),
            impl_->outputNamePtrs.data(),
            impl_->outputNamePtrs.size());

        outputs.clear();
        outputs.reserve(outValues.size());
        for (auto& v : outValues) {
            auto info = v.GetTensorTypeAndShapeInfo();
            OnnxTensor t;
            t.shape = info.GetShape();
            const size_t count = info.GetElementCount();
            const float* p = v.GetTensorData<float>();
            t.data.assign(p, p + count);
            outputs.push_back(std::move(t));
        }
        return true;
    } catch (const Ort::Exception& e) {
        impl_->lastError = e.what();
        return false;
    }
}

bool OnnxInferenceEngine::Run(const std::vector<OnnxTensorInt64>& inputs, std::vector<OnnxTensor>& outputs) {
    if (!impl_->session) {
        impl_->lastError = "model not loaded";
        return false;
    }
    if (inputs.size() != impl_->inputNames.size()) {
        impl_->lastError = "input count mismatch";
        return false;
    }

    try {
        Ort::MemoryInfo memInfo =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::vector<std::vector<int64_t>> inputBuffers(inputs.size());
        std::vector<Ort::Value> inputTensors;
        inputTensors.reserve(inputs.size());

        for (size_t i = 0; i < inputs.size(); ++i) {
            inputBuffers[i] = inputs[i].data;
            inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(
                memInfo,
                inputBuffers[i].data(),
                inputBuffers[i].size(),
                inputs[i].shape.data(),
                inputs[i].shape.size()));
        }

        auto outValues = impl_->session->Run(
            Ort::RunOptions{nullptr},
            impl_->inputNamePtrs.data(),
            inputTensors.data(),
            inputTensors.size(),
            impl_->outputNamePtrs.data(),
            impl_->outputNamePtrs.size());

        outputs.clear();
        outputs.reserve(outValues.size());
        for (auto& v : outValues) {
            auto info = v.GetTensorTypeAndShapeInfo();
            OnnxTensor t;
            t.shape = info.GetShape();
            const size_t count = info.GetElementCount();
            const float* p = v.GetTensorData<float>();
            t.data.assign(p, p + count);
            outputs.push_back(std::move(t));
        }
        return true;
    } catch (const Ort::Exception& e) {
        impl_->lastError = e.what();
        return false;
    }
}

const std::vector<std::string>& OnnxInferenceEngine::InputNames() const {
    return impl_->inputNames;
}

const std::vector<std::string>& OnnxInferenceEngine::OutputNames() const {
    return impl_->outputNames;
}

const std::string& OnnxInferenceEngine::LastError() const {
    return impl_->lastError;
}
