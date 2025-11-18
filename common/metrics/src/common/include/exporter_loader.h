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

#ifndef METRICS_EXPORTER_LOADER_H
#define METRICS_EXPORTER_LOADER_H
#include <dlfcn.h>

#include <iostream>
namespace observability {
namespace metrics {

inline void *LoadHandleFromSoPath(const std::string &libPath, const int flag = RTLD_LAZY)
{
    void *handle = dlopen(libPath.c_str(), flag);
    if (!handle) {
        std::cerr << "Failed to load from" << dlerror() << std::endl;
        return nullptr;
    }
    return handle;
}

template <typename FuncType>
FuncType GetFuncFromHandle(void *handle, const std::string &funcName)
{
    if (handle == nullptr) {
        return nullptr;
    }
    FuncType func = (FuncType)(dlsym(handle, funcName.c_str()));
    if (!func) {
        std::cerr << "Failed to find func symbol:" << funcName << dlerror() << std::endl;
        dlclose(handle);
        return nullptr;
    }
    return func;
}

inline void CloseHandle(void *handle)
{
    dlclose(handle);
}

}  // namespace metrics
}  // namespace observability

#endif  // METRICS_EXPORTER_LOADER_H
