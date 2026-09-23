#include "command_sender.h"

#include "libuvc/libuvc.h"
#include "log.h"

namespace tv {

CameraCommands::Sender makeCameraSender(uvc_device_handle* devh) {
  return [devh](uint16_t value) {
    // UVC SET_CUR on the camera terminal's CT_ZOOM_ABSOLUTE_CONTROL (docs/PROTOCOL.md).
    const uvc_error_t result = uvc_set_zoom_abs(devh, value);
    if (result != UVC_SUCCESS) LOGW("zoom write 0x%04x failed: %s", value, uvc_strerror(result));
    return result == UVC_SUCCESS;
  };
}

}  // namespace tv
