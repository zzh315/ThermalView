// The single piece of app code that writes a camera control (CLAUDE.md rule 1). Its only caller
// is the session, which wraps it in tv::CameraCommands so the allowlist and rate limit always
// apply first. A CTest/build check fails if any other file writes a control.
#pragma once

#include "tv/command_gate.h"

struct uvc_device_handle;

namespace tv {

CameraCommands::Sender makeCameraSender(uvc_device_handle* devh);

}  // namespace tv
