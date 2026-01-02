#pragma once

#include "nebulastore/common/result.h"

// Helper to generate unique variable names
#define RESULT_CONCAT_IMPL(x, y) x##y
#define RESULT_CONCAT(x, y) RESULT_CONCAT_IMPL(x, y)
#define RESULT_UNIQUE_VAR RESULT_CONCAT(_result_, __LINE__)

// RETURN_ON_ERROR: Return error if result has error
#define RETURN_ON_ERROR(result)           \
    do {                                  \
        auto&& RESULT_UNIQUE_VAR = (result); \
        if (RESULT_UNIQUE_VAR.hasError()) \
            return RESULT_UNIQUE_VAR.error(); \
    } while (0)

// CO_RETURN_ON_ERROR: Coroutine version
#define CO_RETURN_ON_ERROR(result)        \
    do {                                  \
        auto&& RESULT_UNIQUE_VAR = (result); \
        if (RESULT_UNIQUE_VAR.hasError()) \
            co_return RESULT_UNIQUE_VAR.error(); \
    } while (0)

// ASSIGN_OR_RETURN: Assign value or return error
#define ASSIGN_OR_RETURN(var, result)     \
    auto&& RESULT_CONCAT(_tmp_, __LINE__) = (result); \
    if (RESULT_CONCAT(_tmp_, __LINE__).hasError()) \
        return RESULT_CONCAT(_tmp_, __LINE__).error(); \
    var = std::move(RESULT_CONCAT(_tmp_, __LINE__)).value()

// CO_ASSIGN_OR_RETURN: Coroutine version
#define CO_ASSIGN_OR_RETURN(var, result)  \
    auto&& RESULT_CONCAT(_co_tmp_, __LINE__) = (result); \
    if (RESULT_CONCAT(_co_tmp_, __LINE__).hasError()) \
        co_return RESULT_CONCAT(_co_tmp_, __LINE__).error(); \
    var = std::move(RESULT_CONCAT(_co_tmp_, __LINE__)).value()
