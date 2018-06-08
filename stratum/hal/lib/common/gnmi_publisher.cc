// Copyright 2018 Google LLC
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


#include "stratum/hal/lib/common/gnmi_publisher.h"

#include <list>
#include <string>

#include "stratum/hal/lib/common/channel_writer_wrapper.h"
#include "absl/synchronization/mutex.h"
#include "sandblaze/gnmi/gnmi.pb.h"
#include "util/gtl/map_util.h"

namespace stratum {
namespace hal {

GnmiPublisher::GnmiPublisher(SwitchInterface* switch_interface)
    : switch_interface_(CHECK_NOTNULL(switch_interface)),
      parse_tree_(CHECK_NOTNULL(switch_interface)),
      event_channel_(nullptr),
      on_config_pushed_(
          new EventHandlerRecord(on_config_pushed_func_, nullptr)) {
  Register<ConfigHasBeenPushedEvent>(EventHandlerRecordPtr(on_config_pushed_))
      .IgnoreError();
}

GnmiPublisher::~GnmiPublisher() {}

::util::Status GnmiPublisher::HandleChange(const GnmiEvent& event) {
  absl::WriterMutexLock l(&access_lock_);

  ::util::Status status = event.Process();
  if (status != ::util::OkStatus()) LOG(ERROR) << status;

  return ::util::OkStatus();
}

::util::Status GnmiPublisher::HandleEvent(
    const GnmiEvent& event, const std::weak_ptr<EventHandlerRecord>& h) {
  // In order to reference a weak pointer, first it has to be used to create a
  // shared pointer.
  if (std::shared_ptr<EventHandlerRecord> handler = h.lock()) {
    RETURN_IF_ERROR((*handler)(event));
  }
  return ::util::OkStatus();
}

::util::Status GnmiPublisher::HandlePoll(const SubscriptionHandle& handle) {
  ::util::Status status;
  if ((status = (*handle)(PollEvent())) != ::util::OkStatus()) {
    // Something went wrong.
    LOG(ERROR) << "Handler returned non-OK status: " << status;
  }
  return ::util::OkStatus();
}

::util::Status GnmiPublisher::SubscribePeriodic(const Frequency& freq,
                                                const ::gnmi::Path& path,
                                                GnmiSubscribeStream* stream,
                                                SubscriptionHandle* h) {
  auto status = Subscribe(&TreeNode::AllSubtreeLeavesSupportOnTimer,
                          &TreeNode::GetOnTimerHandler, path, stream, h);
  if (status != ::util::OkStatus()) {
    return status;
  }
  EventHandlerRecordPtr weak(*h);
  if (TimerDaemon::RequestPeriodicTimer(
          freq.delay_ms_, freq.period_ms_,
          [weak, this]() { return this->HandleEvent(TimerEvent(), weak); },
          (*h)->mutable_timer()) != ::util::OkStatus()) {
    return MAKE_ERROR(ERR_INTERNAL) << "Cannot start timer.";
  }
  // A handler has been successfully found and now it has to be registered in
  // the event handler list that handles timer events.
  return Register<TimerEvent>(weak);
}

::util::Status GnmiPublisher::SubscribePoll(const ::gnmi::Path& path,
                                            GnmiSubscribeStream* stream,
                                            SubscriptionHandle* h) {
  return Subscribe(&TreeNode::AllSubtreeLeavesSupportOnPoll,
                   &TreeNode::GetOnPollHandler, path, stream, h);
}

::util::Status GnmiPublisher::SubscribeOnChange(const ::gnmi::Path& path,
                                                GnmiSubscribeStream* stream,
                                                SubscriptionHandle* h) {
  auto status = Subscribe(&TreeNode::AllSubtreeLeavesSupportOnChange,
                          &TreeNode::GetOnChangeHandler, path, stream, h);
  if (status != ::util::OkStatus()) {
    return status;
  }
  // A handler has been successfully found and now it has to be registered in
  // all event handler lists that handle events of the type this handler is
  // prepared to handle.
  return parse_tree_.FindNodeOrNull(path)->DoOnChangeRegistration(
      EventHandlerRecordPtr(*h));
}

::util::Status GnmiPublisher::Subscribe(
    const SupportOnPtr& all_leaves_support_mode,
    const GetHandlerFunc& get_handler, const ::gnmi::Path& path,
    GnmiSubscribeStream* stream, SubscriptionHandle* h) {
  absl::WriterMutexLock l(&access_lock_);

  // Check input parameters.
  if (stream == nullptr) {
    return MAKE_ERROR(ERR_INVALID_PARAM) << "stream pointer is null!";
  }
  if (h == nullptr) {
    return MAKE_ERROR(ERR_INVALID_PARAM) << "handle pointer is null!";
  }
  if (path.elem_size() == 0) {
    return MAKE_ERROR(ERR_INVALID_PARAM) << "path is empty!";
  }
  // Map the input path to the supported one - walk the tree of known elements
  // element by element starting from the root and if the element is found the
  // move to the next one. If not found, return an error.
  const TreeNode* node = parse_tree_.FindNodeOrNull(path);
  if (node == nullptr) {
    // Ooops... This path is not supported.
    return MAKE_ERROR(ERR_INVALID_PARAM)
           << "The path (" << path.ShortDebugString() << ") is unsupported!";
  }
  if (!(node->*all_leaves_support_mode)()) {
    // Ooops... Not all leaves in this subtree support this mode!
    return MAKE_ERROR(ERR_INVALID_PARAM)
           << "Not all leaves on the path (" << path.ShortDebugString()
           << ") support this mode!";
  }
  // All good! Save the handler that handles this leaf.
  h->reset(new EventHandlerRecord((node->*get_handler)(), stream));
  return ::util::OkStatus();
}

::util::Status GnmiPublisher::UnSubscribe(EventHandlerRecord* h) {
  absl::WriterMutexLock l(&access_lock_);
  // TODO Add implementation.
  return ::util::OkStatus();
}

::util::Status
GnmiPublisher::UpdateSubscriptionWithTargetSpecificModeSpecification(
    const ::gnmi::Path& path, ::gnmi::Subscription* subscription) {
  absl::WriterMutexLock l(&access_lock_);
  // Check input parameters.
  if (subscription == nullptr) {
    return MAKE_ERROR(ERR_INVALID_PARAM) << "subscription pointer is null!";
  }
  if (path.elem_size() == 0) {
    return MAKE_ERROR(ERR_INVALID_PARAM) << "path is empty!";
  }
  // Map the input path to the supported one - walk the tree of known elements,
  // element by element, starting from the root. If the element is found, move
  // to the next one. If not found, return an error.
  const TreeNode* node = parse_tree_.FindNodeOrNull(path);
  if (node == nullptr) {
    // Ooops... This path is not supported.
    return MAKE_ERROR(ERR_INVALID_PARAM)
           << "The path (" << path.ShortDebugString() << ") is unsupported!";
  }
  return node->ApplyTargetDefinedModeToSubscription(subscription);
}

::util::Status GnmiPublisher::SendSyncResponse(GnmiSubscribeStream* stream) {
  // Notify the client that all nodes have been processed.
  if (stream == nullptr) {
    LOG(ERROR) << "Message cannot be sent as the stream pointer is null!";
    return MAKE_ERROR(ERR_INTERNAL) << "stream pointer is null!";
  }
  ::gnmi::SubscribeResponse resp;
  resp.set_sync_response(true);
  if (stream->Write(resp, ::grpc::WriteOptions()) == false) {
    return MAKE_ERROR(ERR_INTERNAL)
           << "Writing sync-response message to stream failed!";
  } else {
    VLOG(1) << "Sync-response message has been sent.";
  }
  return ::util::OkStatus();
}

void GnmiPublisher::ReadGnmiEvents(
    const std::unique_ptr<ChannelReader<GnmiEventPtr>>& reader) {
  do {
    GnmiEventPtr event_ptr;
    // Block on the next event message from the Channel.
    int code = reader->Read(&event_ptr, absl::InfiniteDuration()).error_code();
    // Exit if the Channel is closed.
    if (code == ERR_CANCELLED) break;
    // Read should never timeout.
    if (code == ERR_ENTRY_NOT_FOUND) {
      LOG(ERROR) << "Read with infinite timeout failed with ENTRY_NOT_FOUND.";
      continue;
    }
    // Handle received message.
    ::util::Status status = HandleChange(*event_ptr);
    if (status != ::util::OkStatus()) LOG(ERROR) << status;
  } while (true);
}

void* GnmiPublisher::ThreadReadGnmiEvents(void* arg) {
  CHECK_NOTNULL(arg);
  // Retrieve arguments.
  auto* args = reinterpret_cast<ReaderArgs<GnmiEventPtr>*>(arg);
  GnmiPublisher* manager = args->manager;
  std::unique_ptr<ChannelReader<GnmiEventPtr>> reader = std::move(args->reader);
  delete args;
  manager->ReadGnmiEvents(reader);
  return nullptr;
}

::util::Status GnmiPublisher::RegisterEventWriter() {
  absl::WriterMutexLock l(&access_lock_);
  // If we have not done that yet, create notification event Channel, register
  // it, and create Reader thread.
  if (event_channel_ == nullptr && switch_interface_ != nullptr) {
    event_channel_ = Channel<GnmiEventPtr>::Create(kMaxGnmiEventDepth);
    // Create and register writer to channel with the BcmSdkInterface.
    auto writer = std::make_shared<ChannelWriterWrapper<GnmiEventPtr>>(
        ChannelWriter<GnmiEventPtr>::Create(event_channel_));
    RETURN_IF_ERROR(switch_interface_->RegisterEventNotifyWriter(writer));
    // Create and hand-off Reader to new reader thread.
    pthread_t event_reader_tid;
    auto reader = ChannelReader<GnmiEventPtr>::Create(event_channel_);
    int ret =
        pthread_create(&event_reader_tid, nullptr, ThreadReadGnmiEvents,
                       new ReaderArgs<GnmiEventPtr>{this, std::move(reader)});
    if (ret != 0) {
      return MAKE_ERROR(ERR_INTERNAL)
             << "Failed to spawn gNMI event thread. Err: " << ret << ".";
    }
    // We don't care about the return value. The thread should exit following
    // the closing of the Channel in UnregisterEventWriter().
    ret = pthread_detach(event_reader_tid);
    if (ret != 0) {
      return MAKE_ERROR(ERR_INTERNAL)
             << "Failed to detach gNMI event thread. Err: " << ret << ".";
    }
  }

  return ::util::OkStatus();
}

::util::Status GnmiPublisher::UnregisterEventWriter() {
  absl::WriterMutexLock l(&access_lock_);
  ::util::Status status = ::util::OkStatus();
  // Unregister the Event Notify Channel from the SwitchInterface.
  if (event_channel_ != nullptr && switch_interface_ != nullptr) {
    APPEND_STATUS_IF_ERROR(status,
                           switch_interface_->UnregisterEventNotifyWriter());
    // Close Channel.
    if (!event_channel_ || !event_channel_->Close()) {
      APPEND_ERROR(status) << " Event Notify Channel is already closed.";
    }
    event_channel_ = nullptr;
    switch_interface_ = nullptr;
  }

  return status;
}

}  // namespace hal
}  // namespace stratum