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

#ifndef STRATUM_HAL_LIB_PHAL_FIXED_STRINGSOURCE_H_
#define STRATUM_HAL_LIB_PHAL_FIXED_STRINGSOURCE_H_

#include <string>

#include "stratum/glue/status/status.h"
#include "stratum/glue/status/status_macros.h"
#include "stratum/glue/status/statusor.h"
#include "stratum/hal/lib/phal/stringsource_interface.h"
#include "stratum/lib/macros.h"

namespace stratum {
namespace hal {
namespace phal {

// A StringSource that produces a single fixed value.
class FixedStringSource : public StringSourceInterface {
 public:
  // Constructs a FixedStringSource that will always produce the given string.
  // If can_set == true, SetString will overwrite the stored fixed string.
  explicit FixedStringSource(const std::string& fixed_string)
      : fixed_string_(fixed_string) {}
  ::util::StatusOr<std::string> GetString() override { return fixed_string_; }
  ::util::Status SetString(const std::string& buffer) override {
    return MAKE_ERROR() << "Attempted to set a FixedStringSource.";
  }
  bool CanSet() override { return false; }

 private:
  std::string fixed_string_;
};

}  // namespace phal
}  // namespace hal
}  // namespace stratum

#endif  // STRATUM_HAL_LIB_PHAL_FIXED_STRINGSOURCE_H_
