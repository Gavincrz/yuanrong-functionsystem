/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef METRICS_VALIDATE_H
#define METRICS_VALIDATE_H
#include <algorithm>

#include "common/include/constant.h"
namespace observability {
namespace metrics {
const uint8_t ASCII_UPPER_LIMIT = 127;

inline bool ValidateName(const std::string &name)
{
    if (name.size() > METRICS_NAME_MAX_SIZE) {
        return false;
    }
    // first char should be alpha
    if (!isalpha(name[0])) {
        return false;
    }
    // subsequent chars should be either of alphabets, digits, underscore, minus, dot
    return !std::any_of(std::next(name.begin()), name.end(),
                        [](char c) { return !isalnum(c) && c != '-' && c != '_' && c != '.'; });
}

inline bool ValidateUnit(const std::string &unit)
{
    if (unit.size() > METRICS_UNIT_MAX_SIZE) {
        return false;
    }
    // all should be ascii chars
    return !std::any_of(unit.begin(), unit.end(),
                        [](char c) { return static_cast<unsigned char>(c) > ASCII_UPPER_LIMIT; });
}

inline bool ValidateDescription(const std::string &description)
{
    if (description.size() > METRICS_DESCRIPTION_MAX_SIZE) {
        return false;
    }
    return true;
}

inline bool ValidateMetric(const std::string &name, const std::string &description, const std::string &unit)
{
    return ValidateName(name) && ValidateDescription(description) && ValidateUnit(unit);
}

}  // namespace metrics
}  // namespace observability

#endif  // METRICS_VALIDATE_H
