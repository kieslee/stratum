// Copyright 2019 Google LLC
// Copyright 2019 Dell EMC
// Copyright 2019-present Open Networking Foundation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "stratum/hal/lib/phal/phaldb_service.h"

#include <functional>
#include <sstream>  // IWYU pragma: keep
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "gflags/gflags.h"
#include "google/protobuf/any.pb.h"
#include "google/rpc/code.pb.h"
#include "google/rpc/status.pb.h"
#include "stratum/glue/gtl/cleanup.h"
#include "stratum/glue/gtl/map_util.h"
#include "stratum/glue/logging.h"
#include "stratum/glue/status/status.h"
#include "stratum/glue/status/status_macros.h"
#include "stratum/glue/status/statusor.h"
#include "stratum/hal/lib/common/utils.h"
#include "stratum/hal/lib/phal/attribute_database_interface.h"
#include "stratum/hal/lib/phal/managed_attribute.h"
#include "stratum/lib/channel/channel.h"
#include "stratum/lib/constants.h"
#include "stratum/lib/macros.h"
#include "stratum/lib/utils.h"
#include "stratum/public/lib/error.h"

DEFINE_string(local_phaldb_url, stratum::kPhalDbServiceUrl,
              "URL for server to listen to for external calls from CLIs, etc.");

DECLARE_bool(warmboot);
// DECLARE_string(persistent_config_dir);
DECLARE_int32(grpc_keepalive_time_ms);
DECLARE_int32(grpc_keepalive_timeout_ms);
DECLARE_int32(grpc_keepalive_min_ping_interval);
DECLARE_int32(grpc_keepalive_permit);
DECLARE_uint32(grpc_max_recv_msg_size);
DECLARE_uint32(grpc_max_send_msg_size);

namespace stratum {
namespace hal {
namespace phal {

PhalDbService::PhalDbService(AttributeDatabaseInterface* attribute_db_interface)
    : attribute_db_interface_(ABSL_DIE_IF_NULL(attribute_db_interface)) {}

PhalDbService::~PhalDbService() {}

::util::Status PhalDbService::Setup(bool warmboot) {
  return ::util::OkStatus();
}

::util::Status PhalDbService::Run() {
  // TODO(max)
  // All HAL external facing services listen to a list of secure external URLs
  // given by external_stratum_urls flag, as well as a local insecure URLs for
  // given by local_stratum_url flag. The insecure URLs is used by any local
  // stratum_stub binary running on the switch, since local connections cannot
  // support auth.
  {
    ::grpc::ServerBuilder builder;
    // builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS,
    //                             FLAGS_grpc_keepalive_time_ms);
    // builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS,
    //                             FLAGS_grpc_keepalive_timeout_ms);
    // builder.AddChannelArgument(
    //     GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS,
    //     FLAGS_grpc_keepalive_min_ping_interval);
    // builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS,
    //                             FLAGS_grpc_keepalive_permit);
    builder.AddListeningPort(FLAGS_local_phaldb_url,
                             ::grpc::InsecureServerCredentials());
    // if (FLAGS_grpc_max_recv_msg_size > 0) {
    //   builder.SetMaxReceiveMessageSize(FLAGS_grpc_max_recv_msg_size * 1024 *
    //                                    1024);
    //   builder.AddChannelArgument<int>(
    //       GRPC_ARG_MAX_METADATA_SIZE,
    //       FLAGS_grpc_max_recv_msg_size * 1024 * 1024);
    // }
    // if (FLAGS_grpc_max_send_msg_size) {
    //   builder.SetMaxSendMessageSize(FLAGS_grpc_max_send_msg_size * 1024 *
    //   1024);
    // }
    builder.RegisterService(this);
    external_server_ = builder.BuildAndStart();
    if (external_server_ == nullptr) {
      return MAKE_ERROR(ERR_INTERNAL)
             << "Failed to start PhalDb service. This is an "
             << "internal error.";
    }
    LOG(INFO) << "PhalDB service is listening to " << FLAGS_local_phaldb_url
              << "...";
  }
  return ::util::OkStatus();
}

::util::Status PhalDbService::Teardown() {
  external_server_->Shutdown(std::chrono::system_clock::now());
  external_server_->Wait();  // blocking until external_server_->Shutdown()
                             // is called. We dont wait on internal_service.
  {
    absl::MutexLock l(&subscriber_thread_lock_);
    // Close Subscriber Channels.
    for (const auto& pair : subscriber_channels_) {
      pair.second->Close();
    }
    subscriber_channels_.clear();
  }

  LOG(INFO) << "PhalDbService shutdown completed successfully.";
  return ::util::OkStatus();
}

namespace {

// Convert from ProtoBuf Path to PhalDB Path
::util::StatusOr<Path> ToPhalDBPath(PathQuery req_path) {
  // If no path entries return error
  if (req_path.entries_size() == 0) {
    RETURN_ERROR(ERR_INVALID_PARAM) << "No Path";
  }

  // Create Attribute DB Path
  Path path;
  for (const auto& ent : req_path.entries()) {
    PathEntry entry;
    entry.name = ent.name();
    entry.index = ent.index();
    entry.indexed = ent.indexed();
    entry.all = ent.all();
    entry.terminal_group = ent.terminal_group();
    path.push_back(entry);
  }

  return path;
}

::grpc::Status ToPhalGrpcStatus(const ::util::Status& status,
                                const std::vector<::util::Status>& details) {
  // We need to create a ::google::rpc::Status and populate it with all the
  // details, then convert it to ::grpc::Status.
  ::google::rpc::Status from;
  if (!status.ok()) {
    from.set_code(ToGoogleRpcCode(status.CanonicalCode()));
    from.set_message(status.error_message());
    // Add individual errors only when the top level error code is not OK.
    for (const auto& detail : details) {
      // Each individual detail is converted to another ::google::rpc::Status,
      // which is then serialized as one proto any in 'from' message above.
      Error error;
      if (!detail.ok()) {
        error.set_canonical_code(ToGoogleRpcCode(detail.CanonicalCode()));
        error.set_code(detail.error_code());
        error.set_message(detail.error_message());
      } else {
        error.set_code(::google::rpc::OK);
      }
      from.add_details()->PackFrom(error);
    }
  } else {
    from.set_code(::google::rpc::OK);
  }

  return ::grpc::Status(ToGrpcCode(from.code()), from.message(),
                        from.SerializeAsString());
}

}  // namespace

::util::Status PhalDbService::DoGet(::grpc::ServerContext* context,
                                    const GetRequest* req, GetResponse* resp) {
  ASSIGN_OR_RETURN(auto path, ToPhalDBPath(req->path()));
  std::vector<Path> paths = {path};
  auto adapter = absl::make_unique<Adapter>(attribute_db_interface_);
  ASSIGN_OR_RETURN(auto result, adapter->Get(paths));
  LOG(INFO) << "Phal Get result:" << result->ShortDebugString();
  *resp->mutable_phal_db() = *result;

  return ::util::OkStatus();
}

::grpc::Status PhalDbService::Get(::grpc::ServerContext* context,
                                  const GetRequest* req, GetResponse* resp) {
  return ToPhalGrpcStatus(DoGet(context, req, resp), {});
}

::util::Status PhalDbService::DoSet(::grpc::ServerContext* context,
                                    const SetRequest* req, SetResponse* resp) {
  if (!req->updates_size()) return ::util::OkStatus();  // Nothing to do.

  AttributeValueMap attribute_map;

  // Spin thru each update
  for (const auto& update : req->updates()) {
    ASSIGN_OR_RETURN(auto path, ToPhalDBPath(update.path()));

    // Create attribute path:val pair base on value type
    switch (update.value().value_case()) {
      case UpdateValue::kDoubleVal: {
        attribute_map[path] = update.value().double_val();
        break;
      }
      case UpdateValue::kFloatVal: {
        attribute_map[path] = update.value().float_val();
        break;
      }
      case UpdateValue::kInt32Val: {
        attribute_map[path] = update.value().int32_val();
        break;
      }
      case UpdateValue::kInt64Val: {
        attribute_map[path] = update.value().int64_val();
        break;
      }
      case UpdateValue::kUint32Val: {
        attribute_map[path] = update.value().uint32_val();
        break;
      }
      case UpdateValue::kUint64Val: {
        attribute_map[path] = update.value().uint64_val();
        break;
      }
      case UpdateValue::kBoolVal: {
        attribute_map[path] = update.value().bool_val();
        break;
      }
      case UpdateValue::kStringVal: {
        attribute_map[path] = update.value().string_val();
        break;
      }
      case UpdateValue::kBytesVal: {
        attribute_map[path] = update.value().bytes_val();
        break;
      }
      default: {
        return MAKE_ERROR(ERR_INVALID_PARAM) << "Unknown value type";
        break;
      }
    }
  }
  auto adapter = absl::make_unique<Adapter>(attribute_db_interface_);
  RETURN_IF_ERROR(adapter->Set(attribute_map));

  return ::util::OkStatus();
}

::grpc::Status PhalDbService::Set(::grpc::ServerContext* context,
                                  const SetRequest* req, SetResponse* resp) {
  return ToPhalGrpcStatus(DoSet(context, req, resp), {});
}

::util::Status PhalDbService::DoSubscribe(
    ::grpc::ServerContext* context, const SubscribeRequest* req,
    ::grpc::ServerWriter<SubscribeResponse>* stream) {
  ASSIGN_OR_RETURN(auto path, ToPhalDBPath(req->path()));
  // Create writer and reader channels
  std::shared_ptr<Channel<PhalDB>> channel = Channel<PhalDB>::Create(128);

  {
    // Lock subscriber channels
    absl::MutexLock l(&subscriber_thread_lock_);
    // Save channel to subscriber channel map
    subscriber_channels_[pthread_self()] = channel;
  }
  auto _ = gtl::MakeCleanup([this, &channel] {
    {
      absl::MutexLock l(&subscriber_thread_lock_);
      // Close the channel which will then cause the PhalDB writer
      // to close and exit
      channel->Close();
      subscriber_channels_.erase(pthread_self());
    }
  });

  auto writer = ChannelWriter<PhalDB>::Create(channel);
  auto reader = ChannelReader<PhalDB>::Create(channel);

  // Issue the subscribe
  auto adapter = absl::make_unique<Adapter>(attribute_db_interface_);
  RETURN_IF_ERROR(adapter->Subscribe({path}, std::move(writer),
                                     absl::Seconds(req->polling_interval())));

  // Loop around processing messages from the PhalDB writer
  // Note: if the client dies we'll only close the channel
  //       and thus cancel the PhalDB subscription once we
  //       get something from the PhalDB subscription (i.e.
  //       if the poll timer expires and something has changed).
  //       We could potentially put something in here to check
  //       the stream and channel for changes but for now this
  //       will do.
  while (true) {
    PhalDB phaldb_resp;
    int code =
        reader->Read(&phaldb_resp, absl::InfiniteDuration()).error_code();

    // Exit if the channel is closed
    if (code == ERR_CANCELLED) {
      return MAKE_ERROR(ERR_INTERNAL) << "PhalDB Subscribe closed the channel";
    }

    // Error if read timesout
    if (code == ERR_ENTRY_NOT_FOUND) {
      LOG(ERROR) << "Subscribe read with infinite timeout "
                 << "failed with ENTRY_NOT_FOUND.";
      continue;
    }

    // If we get nothing in message then close the channel
    // - this is also used to mock the PhalDB Subscribe
    if (phaldb_resp.ByteSizeLong() == 0) {
      return MAKE_ERROR(ERR_INTERNAL) << "Subscribe read returned zero bytes.";
    }

    // Send message to client
    SubscribeResponse resp;
    *resp.mutable_phal_db() = phaldb_resp;

    // If Write fails then break out of the loop
    CHECK_RETURN_IF_FALSE(stream->Write(resp))
        << "Subscribe stream write failed";
  }

  return ::util::OkStatus();
}

::grpc::Status PhalDbService::Subscribe(
    ::grpc::ServerContext* context, const SubscribeRequest* req,
    ::grpc::ServerWriter<SubscribeResponse>* stream) {
  return ToPhalGrpcStatus(DoSubscribe(context, req, stream), {});
}

}  // namespace phal
}  // namespace hal
}  // namespace stratum
