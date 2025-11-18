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

#include <glob.h>
#include <sys/stat.h>
#include <zlib.h>

#include <cstring>
#include <sstream>

#include "file_utils.h"

namespace observability {

namespace metrics::common {
const int THOUSANDS_OF_MAGNITUDE = 1000;
const int MILLION_OF_MAGNITUDE = 1000000;
const mode_t LOG_FILE_PERMISSION = 0400;
const size_t BUFFER_SIZE = 32 * 1024uL;

inline std::string StrErr(int errNum)
{
    char errBuf[256];
    errBuf[0] = '\0';
    return strerror_r(errNum, errBuf, sizeof errBuf);
}

void Glob(const std::string &pathPattern, std::vector<std::string> &paths)
{
    glob_t result;

    int ret = glob(pathPattern.c_str(), GLOB_TILDE | 1, nullptr, &result);
    switch (ret) {
        case 0:
            break;
        case GLOB_NOMATCH:
            globfree(&result);
            std::cout << "failed to glob files, reason: no match." << std::endl;
            return;
        case GLOB_NOSPACE:
            globfree(&result);
            std::cout << "failed to glob files, reason: out of memory." << std::endl;
            return;
        default:
            globfree(&result);
            std::cout << "failed to glob files, pattern: " << pathPattern << ", errno:" <<
                ret << ", errmsg:" << StrErr(ret) << std::endl;
            return;
    }

    for (size_t i = 0; i < result.gl_pathc; ++i) {
        (void)paths.emplace_back(result.gl_pathv[i]);
    }

    globfree(&result);
    return;
}

void Read(FILE *f, uint8_t *buf, size_t *pSize)
{
    size_t numReads = 0;
    size_t size = *pSize;

    do {
        static_assert(std::is_unsigned<decltype(numReads)>::value);
        numReads = fread_unlocked(buf, 1, size, f);
    } while (numReads == 0 && ferror(f) == EINTR);

    if (numReads < size) {
        if (feof(f)) {
            *pSize = numReads;
        } else {
            std::cout << "failed to reads, IOError occurred, errno: " << errno << std::endl;
        }
    }
    return;
}

int CompressFile(const std::string &src, const std::string &dest)
{
    FILE *file = fopen(src.c_str(), "r");
    if (file == nullptr) {
        std::cout << "failed to open file: " << src << std::endl;
        return -1;
    }
    gzFile gzf = gzopen(dest.c_str(), "w");
    if (gzf == nullptr) {
        std::cout << "failed to open gz file: " << dest << std::endl;
        (void)fclose(file);
        return -1;
    }

    size_t size = BUFFER_SIZE;
    uint8_t buf[size] = "\0";
    while (size != 0) {
        try {
            Read(file, buf, &size);
        } catch (const std::exception &e) {
            (void)gzclose(gzf);
            (void)fclose(file);
            std::cout << "failed to compress file, err: " << e.what() << std::endl;
            return -1;
        }
        if (size == 0) {
            break;
        }
        int n = gzwrite(gzf, buf, static_cast<unsigned int>(size));
        if (n <= 0) {
            int err;
            const char *errStr = gzerror(gzf, &err);
            std::cout << "failed to write gz file, errmsg:" << errStr << std::endl;
            (void)gzclose(gzf);
            (void)fclose(file);
            return err;
        }
    }
    (void)gzclose(gzf);
    (void)fclose(file);

    // Change mode to 0400, we only allow the read permission. And we
    // will never check the return even the chmod operation is failed.
    int rc = chmod(dest.c_str(), LOG_FILE_PERMISSION);
    if (rc != 0) {
        std::cout << "failed to chmod file " << dest << ", err:" << StrErr(errno) << std::endl;
    }

    return 0;
}

void DeleteFile(const std::string &filename)
{
    auto code = unlink(filename.c_str());
    if (code != 0) {
        std::cout << "code: " << code << ", failed to delete file " << filename << std::endl;
    }
}

void GetFileModifiedTime(const std::string &filename, int64_t &timestamp)
{
    struct stat statBuf {};
    if (stat(filename.c_str(), &statBuf) != 0) {
        std::cerr << "failed to access modify time from " << filename << std::endl;
        return;
    }

    auto secondBit = statBuf.st_mtim.tv_sec * MILLION_OF_MAGNITUDE;
    if (secondBit / MILLION_OF_MAGNITUDE != statBuf.st_mtim.tv_sec) {
        std::cerr << "invalid value tv_sec: " << statBuf.st_mtim.tv_sec <<
            "; tv_nsec: " << statBuf.st_mtim.tv_nsec << std::endl;
        return;
    }
    auto nsecBit = statBuf.st_mtim.tv_nsec / THOUSANDS_OF_MAGNITUDE;
    timestamp = INT64_MAX - secondBit < nsecBit ? 0 : secondBit + nsecBit;
}

}  // namespace metrics::common

}  // namespace observability
