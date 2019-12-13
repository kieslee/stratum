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

#include "stratum/glue/status/status_test_util.h"

#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "stratum/glue/logging.h"

TEST(StatusTestUtil, ExpectOkSuccess) { EXPECT_OK(::util::Status::OK); }

TEST(StatusTestUtil, AssertOkSuccess) { ASSERT_OK(::util::Status::OK); }

TEST(StatusTestUtil, ExpectOkFailure) {
  ::util::Status error_status(::util::error::UNKNOWN, "error_status_message");
  EXPECT_NONFATAL_FAILURE(EXPECT_OK(error_status), "error_status_message");
}

TEST(StatusTestUtil, AssertOkFailure) {
  EXPECT_FATAL_FAILURE(
      ASSERT_OK(::util::Status(::util::error::UNKNOWN, "error_status_message")),
      "error_status_message");
}
