#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "yoke_voice_io.h"

int main(void) {
    uint8_t wire[123];
    for (size_t i = 0; i < sizeof(wire); ++i)
        wire[i] = (uint8_t)i;
    wire[120] = 0xfa;
    wire[121] = 0xce;
    struct {
        uint32_t before;
        yoke_voice_record_frame_t frame;
        uint16_t after;
    } out = { .before = 0x12345678, .after = 0x4321 };
    assert(yoke_voice_record_frame_parse(wire, 122, &out.frame));
    assert(out.frame.size == 120);
    assert(!memcmp(out.frame.data, wire, 120));
    assert(out.before == 0x12345678 && out.after == 0x4321);
    assert(yoke_voice_record_frame_parse(wire, 80, &out.frame));
    assert(out.frame.size == 80);
    assert(!memcmp(out.frame.data, wire, 80));
    for (size_t i = 80; i < sizeof(out.frame.data); ++i)
        assert(out.frame.data[i] == 0);
    yoke_voice_record_frame_t saved = out.frame;
    for (size_t n = 0; n <= sizeof(wire); ++n) {
        if (n == 80 || n == 122)
            continue;
        assert(!yoke_voice_record_frame_parse(wire, n, &out.frame));
        assert(!memcmp(&out.frame, &saved, sizeof(saved)));
    }
    assert(!yoke_voice_record_frame_parse(NULL, 80, &out.frame));
    assert(!yoke_voice_record_frame_parse(wire, 80, NULL));
    puts("CI1302 packet profiles, RMS exclusion, boundaries and rejection: PASS");
}
