#ifndef PLATFORMS_NETWORKING_HERCULES_HAL_LIB_COMMON_CERTIFICATE_MANAGEMENT_SERVICE_H_
#define PLATFORMS_NETWORKING_HERCULES_HAL_LIB_COMMON_CERTIFICATE_MANAGEMENT_SERVICE_H_

#include <grpc++/grpc++.h>

#include <memory>

#include "platforms/networking/hercules/glue/gnoi/cert.grpc.pb.h"
#include "platforms/networking/hercules/hal/lib/common/common.pb.h"
#include "platforms/networking/hercules/hal/lib/common/error_buffer.h"
#include "platforms/networking/hercules/hal/lib/common/switch_interface.h"
#include "platforms/networking/hercules/lib/security/auth_policy_checker.h"
#include "absl/base/integral_types.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "util/task/status.h"

namespace google {
namespace hercules {
namespace hal {

// CertificateManagementService is an implementation of
// gnoi::certificate::CertificateManagement gRPC service and is in charge of
// providing all certificate related functionalities, such as cert rotation,
// cert install, etc.
class CertificateManagementService final
    : public ::gnoi::certificate::CertificateManagement::Service {
 public:
  // Input parameters:
  // mode: The mode of operation.
  // switch_interface: The pointer to the implementation of SwitchInterface for
  //     all the low-level platform-specific operations.
  // auth_policy_checker: for per RPC authorization policy checks.
  // error_buffer: pointer to an ErrorBuffer for logging all critical errors.
  CertificateManagementService(OperationMode mode,
                               SwitchInterface* switch_interface,
                               AuthPolicyChecker* auth_policy_checker,
                               ErrorBuffer* error_buffer);
  ~CertificateManagementService() override {}

  // Sets up the service in coldboot or warmboot mode.
  ::util::Status Setup(bool warmboot);

  // Tears down the class. Called in both warmboot or coldboot mode.
  ::util::Status Teardown();

  // Please see //openconfig/gnoi/cert/cert.proto for the
  // documentation of the RPCs.
  ::grpc::Status Rotate(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter<
          ::gnoi::certificate::RotateCertificateResponse,
          ::gnoi::certificate::RotateCertificateRequest>* stream) override;

  ::grpc::Status Install(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter<
          ::gnoi::certificate::InstallCertificateResponse,
          ::gnoi::certificate::InstallCertificateRequest>* stream) override;

  ::grpc::Status GetCertificates(
      ::grpc::ServerContext* context,
      const ::gnoi::certificate::GetCertificatesRequest* req,
      ::gnoi::certificate::GetCertificatesResponse* resp) override;

  ::grpc::Status RevokeCertificates(
      ::grpc::ServerContext* context,
      const ::gnoi::certificate::RevokeCertificatesRequest* req,
      ::gnoi::certificate::RevokeCertificatesResponse* resp) override;

  ::grpc::Status CanGenerateCSR(
      ::grpc::ServerContext* context,
      const ::gnoi::certificate::CanGenerateCSRRequest* req,
      ::gnoi::certificate::CanGenerateCSRResponse* resp) override;

  // CertificateManagementService is neither copyable nor movable.
  CertificateManagementService(const CertificateManagementService&) = delete;
  CertificateManagementService& operator=(const CertificateManagementService&) =
      delete;

 private:
  // Determines the mode of operation:
  // - OPERATION_MODE_STANDALONE: when Hercules stack runs independently and
  // therefore needs to do all the SDK initialization itself.
  // - OPERATION_MODE_COUPLED: when Hercules stack runs as part of Sandcastle
  // stack, coupled with the rest of stack processes.
  // - OPERATION_MODE_SIM: when Hercules stack runs in simulation mode.
  // Note that this variable is set upon initialization and is never changed
  // afterwards.
  const OperationMode mode_;

  // Pointer to SwitchInterface implementation, which encapsulates all the
  // switch capabilities. Not owned by this class.
  SwitchInterface* switch_interface_;

  // Pointer to AuthPolicyChecker. Not owned by this class.
  AuthPolicyChecker* auth_policy_checker_;

  // Pointer to ErrorBuffer to save any critical errors we encounter. Not owned
  // by this class.
  ErrorBuffer* error_buffer_;
};

}  // namespace hal
}  // namespace hercules
}  // namespace google

#endif  // PLATFORMS_NETWORKING_HERCULES_HAL_LIB_COMMON_CERTIFICATE_MANAGEMENT_SERVICE_H_
