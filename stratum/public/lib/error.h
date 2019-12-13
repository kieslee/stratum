/*
 * Copyright 2018 Google LLC
 * Copyright 2018-present Open Networking Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef STRATUM_PUBLIC_LIB_ERROR_H_
#define STRATUM_PUBLIC_LIB_ERROR_H_

#include "stratum/glue/status/status_macros.h"
#include "stratum/public/proto/error.pb.h"

namespace stratum {

// StratumErrorSpace returns the singleton instance to be used through
// out the code.
const ::util::ErrorSpace* StratumErrorSpace();

}  // namespace stratum

// Allow using status_macros. For example:
// return MAKE_ERROR(ERR_UNKNOWN) << "test";
namespace util {
namespace status_macros {

template <>
class ErrorCodeOptions<::stratum::ErrorCode> : public BaseErrorCodeOptions {
 public:
  const ::util::ErrorSpace* GetErrorSpace() {
    return ::stratum::StratumErrorSpace();
  }
};

}  // namespace status_macros
}  // namespace util

#endif  // STRATUM_PUBLIC_LIB_ERROR_H_
