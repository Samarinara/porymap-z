/* Headless mGBA smoke test for a real Play ROM. See verify_playtest_build.py. */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static mColor pixels[240 * 160];

static void log_message(struct mLogger *logger, int category, enum mLogLevel level,
                        const char *format, va_list args) {
    (void)logger;
    (void)category;
    if (level & (mLOG_FATAL | mLOG_ERROR)) {
        vfprintf(stderr, format, args);
        fputc('\n', stderr);
    }
}

int main(int argc, char **argv) {
    if (argc != 10 && argc != 12) {
        fprintf(stderr, "Usage: %s ROM gMain gSaveBlock1Ptr CB1_Overworld CB2_Overworld map x y frames [partyCountAddress expectedCount]\n", argv[0]);
        return 2;
    }
    const uint32_t main_addr = strtoul(argv[2], NULL, 0);
    const uint32_t save_ptr_addr = strtoul(argv[3], NULL, 0);
    const uint32_t callback1 = strtoul(argv[4], NULL, 0) & ~1U;
    const uint32_t callback2 = strtoul(argv[5], NULL, 0) & ~1U;
    const unsigned expected_map = strtoul(argv[6], NULL, 0);
    const int expected_x = strtol(argv[7], NULL, 0);
    const int expected_y = strtol(argv[8], NULL, 0);
    const unsigned frames = strtoul(argv[9], NULL, 0);
    const uint32_t party_count_addr = argc == 12 ? strtoul(argv[10], NULL, 0) : 0;
    const int expected_party_count = argc == 12 ? strtol(argv[11], NULL, 0) : -1;
    struct mLogger logger = {.log = log_message, .filter = NULL};
    mLogSetDefaultLogger(&logger);
    struct mCore *core = mCoreFind(argv[1]);
    if (!core || !core->init(core)) {
        fprintf(stderr, "Could not initialize the mGBA core.\n");
        return 2;
    }
    mCoreInitConfig(core, "porymap-boot-check");
    mCoreLoadConfig(core);
    core->setVideoBuffer(core, pixels, 240);
    if (!mCoreLoadFile(core, argv[1])) {
        fprintf(stderr, "Could not load the demo ROM.\n");
        core->deinit(core);
        return 2;
    }
    core->reset(core);
    unsigned overworld_frames = 0;
    for (unsigned frame = 0; frame < frames; ++frame) {
        core->runFrame(core);
        const uint32_t cb1 = core->busRead32(core, main_addr) & ~1U;
        const uint32_t cb2 = core->busRead32(core, main_addr + 4) & ~1U;
        overworld_frames = cb1 == callback1 && cb2 == callback2 ? overworld_frames + 1 : 0;
    }
    const uint32_t save = core->busRead32(core, save_ptr_addr);
    // These leading fields are shared by the supported pret/expansion games.
    const int x = (int16_t)core->busRead16(core, save);
    const int y = (int16_t)core->busRead16(core, save + 2);
    const unsigned group = core->busRead8(core, save + 4);
    const unsigned number = core->busRead8(core, save + 5);
    unsigned nonblack = 0, colors = 0;
    mColor seen[64];
    for (unsigned i = 0; i < 240 * 160; ++i) {
        const mColor pixel = pixels[i] & 0xffffff;
        nonblack += pixel != 0;
        if (colors < 64) {
            unsigned j = 0;
            while (j < colors && seen[j] != pixel) ++j;
            if (j == colors) seen[colors++] = pixel;
        }
    }
    const int valid_save = save >= 0x02000000 && save < 0x02040000;
    const int party_count = party_count_addr ? core->busRead8(core, party_count_addr) : -1;
    const int passed = valid_save && overworld_frames >= 60 &&
                       ((group << 8) | number) == expected_map &&
                       x == expected_x && y == expected_y && nonblack > 1000 && colors >= 8 &&
                       (expected_party_count < 0 || party_count == expected_party_count);
    printf("{\"passed\":%s,\"frames\":%u,\"overworld_frames\":%u,"
           "\"map_group\":%u,\"map_number\":%u,\"x\":%d,\"y\":%d,"
           "\"nonblack_pixels\":%u,\"colors\":%u,\"party_count\":%d}\n",
           passed ? "true" : "false", frames, overworld_frames,
           group, number, x, y, nonblack, colors, party_count);
    if (!passed) fprintf(stderr, "Demo did not reach the requested visible overworld.\n");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    return passed ? 0 : 1;
}
