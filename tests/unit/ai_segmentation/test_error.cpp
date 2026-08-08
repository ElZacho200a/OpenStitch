// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/error.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace openstitch::ai_segmentation;

TEST_CASE("every AiErrorCode round-trips through its protocol name") {
    const AiErrorCode codes[] = {
        AiErrorCode::WorkerNotConfigured,   AiErrorCode::WorkerStartFailed,
        AiErrorCode::PythonNotFound,        AiErrorCode::WslNotFound,
        AiErrorCode::VenvNotFound,          AiErrorCode::Sam2NotInstalled,
        AiErrorCode::ModelNotInstalled,     AiErrorCode::ConfigNotFound,
        AiErrorCode::CheckpointNotFound,    AiErrorCode::ModelCheckpointMismatch,
        AiErrorCode::CudaUnavailable,       AiErrorCode::CudaOutOfMemory,
        AiErrorCode::ImageLoadFailed,       AiErrorCode::InferenceFailed,
        AiErrorCode::InvalidWorkerResponse, AiErrorCode::WorkerCrashed,
        AiErrorCode::Cancelled,
    };
    for (const AiErrorCode code : codes) {
        const std::string name = ai_error_code_name(code);
        CHECK(ai_error_code_from_name(name) == code);
        CHECK_FALSE(default_message(code).empty());
    }
}

TEST_CASE("known error code names match the protocol spec exactly") {
    CHECK(ai_error_code_name(AiErrorCode::ModelCheckpointMismatch) == "MODEL_CHECKPOINT_MISMATCH");
    CHECK(ai_error_code_name(AiErrorCode::CudaOutOfMemory) == "CUDA_OUT_OF_MEMORY");
    CHECK(ai_error_code_name(AiErrorCode::WslNotFound) == "WSL_NOT_FOUND");
}

TEST_CASE("unknown name falls back to InvalidWorkerResponse") {
    CHECK(ai_error_code_from_name("NOT_A_REAL_CODE") == AiErrorCode::InvalidWorkerResponse);
}
