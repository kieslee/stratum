// This is the main entry for HAL BCM module tests.
#include <stdlib.h>

#include "base/commandlineflags.h"
#include "platforms/networking/hercules/glue/init_google.h"
#include "platforms/networking/hercules/glue/logging.h"
#include "testing/base/public/gunit.h"

DEFINE_string(test_tmpdir, "", "Temp directory to be used for tests.");

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  InitGoogle(argv[0], &argc, &argv, true);

  bool tmpdir_created = false;
  if (FLAGS_test_tmpdir.empty()) {
    char tmpdir[] = "/tmp/hercules_hal_bcm_test.XXXXXX";
    CHECK(mkdtemp(tmpdir));
    FLAGS_test_tmpdir = tmpdir;
    tmpdir_created = true;
    LOG(INFO) << "Created FLAGS_test_tmpdir " << FLAGS_test_tmpdir;
  }

  int result = RUN_ALL_TESTS();

  if (tmpdir_created) {
    const std::string cleanup("rm -rf " + FLAGS_test_tmpdir);
    system(cleanup.c_str());
    LOG(INFO) << "Cleaned up FLAGS_test_tmpdir " << FLAGS_test_tmpdir;
  }

  return result;
}
