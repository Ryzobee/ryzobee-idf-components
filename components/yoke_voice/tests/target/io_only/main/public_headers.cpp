#include "yoke_voice.h"
#include "yoke_voice_io.h"
#include "yoke_voice_runtime.h"
#include "yoke_voice_player.h"
#include "yoke_voice_record.h"
#include "yoke_voice_network.h"
extern "C" void verify_cpp_linkage(void) {
    (void)yoke_voice_device_quiet(nullptr);
}
