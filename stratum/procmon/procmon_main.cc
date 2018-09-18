#include <grpc++/grpc++.h>
#include <memory>

#include "base/commandlineflags.h"
#include "platforms/networking/hercules/glue/init_google.h"
#include "platforms/networking/hercules/glue/logging.h"
#include "platforms/networking/hercules/lib/constants.h"
#include "platforms/networking/hercules/lib/macros.h"
#include "platforms/networking/hercules/lib/utils.h"
#include "platforms/networking/hercules/procmon/procmon.h"
#include "platforms/networking/hercules/procmon/procmon.pb.h"
#include "platforms/networking/hercules/procmon/procmon_service_impl.h"
#include "util/task/status.h"

DEFINE_string(procmon_config_file, "",
              "Path to Procmon configuration proto file.");
DEFINE_string(procmon_service_addr, ::google::hercules::kProcmonServiceUrl,
              "Url of the procmon service to listen to.");

namespace google {
namespace hercules {
namespace procmon {

::util::Status Main(int argc, char**argv) {
  InitGoogle(argv[0], &argc, &argv, true);
  InitHerculesLogging();

  // Read the procmon config.
  CHECK_RETURN_IF_FALSE(!FLAGS_procmon_config_file.empty())
      << "Flag procmon_config_file must be specified.";
  ProcmonConfig config;
  RETURN_IF_ERROR(ReadProtoFromTextFile(FLAGS_procmon_config_file, &config));

  // Create and start the procmon gRPC service.
  ProcmonServiceImpl procmon_service_impl;
  ::grpc::ServerBuilder builder;
  builder.AddListeningPort(FLAGS_procmon_service_addr,
                           ::grpc::InsecureServerCredentials());
  builder.RegisterService(&procmon_service_impl);
  std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  LOG(INFO) << "Procmon gRPC service started.";

  // Start Procmon class instance and run it.
  Procmon procmon(std::make_shared<ProcessHandler>());
  ::util::Status status = procmon.Run(config);  // blocking
  server->Wait();
  if (status.ok()) {
    return MAKE_ERROR(ERR_INTERNAL)
           << "Procmon::Run should never return with an ok status.";
  }

  return status;
}

}  // namespace procmon
}  // namespace hercules
}  // namespace google

int main(int argc, char** argv) {
  ::util::Status ret = google::hercules::procmon::Main(argc, argv);
  return ret.ok() ? 0 : 1;
}
