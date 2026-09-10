#ifdef SIMULATOR
#include "network/FirmwareFlasher.h"

namespace firmware_flash {

Result flashFromSdPath(const char* sdPath, ProgressCb progressCb, void* progressCtx, bool dryRun) {
  return Result::OK;
}

}  // namespace firmware_flash
#endif
