#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unistd.h>

extern "C" {
typedef struct host_api_v1 {
    unsigned int api_version;
    int sample_rate;
    int frames_per_block;
    unsigned char *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;
    void (*log)(const char *msg);
    int (*midi_send_internal)(const unsigned char *msg, int len);
    int (*midi_send_external)(const unsigned char *msg, int len);
} host_api_v1_t;

typedef struct plugin_api_v2 {
    unsigned int api_version;
    void *(*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const unsigned char *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, short *out_interleaved_lr, int frames);
} plugin_api_v2_t;

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);
}

static void check_string_param(plugin_api_v2_t *api, void *inst, const char *key, const char *expected) {
    char got[512];
    std::memset(got, 0, sizeof(got));
    if (api->get_param(inst, key, got, (int)sizeof(got)) < 0) {
        std::fprintf(stderr, "FAIL: get %s failed\n", key);
        std::exit(1);
    }
    if (std::strcmp(got, expected) != 0) {
        std::fprintf(stderr, "FAIL: %s expected '%s', got '%s'\n", key, expected, got);
        std::exit(1);
    }
}

static void check_float_param(plugin_api_v2_t *api, void *inst, const char *key, float expected) {
    char got[512];
    std::memset(got, 0, sizeof(got));
    if (api->get_param(inst, key, got, (int)sizeof(got)) < 0) {
        std::fprintf(stderr, "FAIL: get %s failed\n", key);
        std::exit(1);
    }
    float val = (float)std::atof(got);
    if (std::fabs(val - expected) > 1e-3f) {
        std::fprintf(stderr, "FAIL: %s expected %g, got %g (string: %s)\n", key, expected, val, got);
        std::exit(1);
    }
}

/* Minimal native .ablpreset: instrumentRack -> drumRack -> 2 drumCells.
 * Pad 1 (note 36) uses the ableton:/user-library/ scheme with a %20-encoded
 * space; pad 2 (note 37) uses a different volume to check dB mapping. */
static const char *kPresetJson =
"{\n"
"  \"kind\": \"instrumentRack\",\n"
"  \"name\": \"TestKit\",\n"
"  \"chains\": [ { \"devices\": [ {\n"
"    \"kind\": \"drumRack\",\n"
"    \"chains\": [\n"
"      { \"drumZoneSettings\": { \"receivingNote\": 36, \"chokeGroup\": null },\n"
"        \"devices\": [ { \"kind\": \"drumCell\",\n"
"          \"parameters\": { \"Volume\": -12.0, \"Pan\": 0.25, \"Voice_Transpose\": 3,\n"
"                           \"Voice_PlaybackStart\": 0.0, \"Voice_Envelope_Attack\": 0.0001,\n"
"                           \"Voice_Envelope_Decay\": 1.0 },\n"
"          \"deviceData\": { \"sampleUri\": \"ableton:/user-library/Samples/Preset%20Samples/Kick%20Test.wav\" } } ] },\n"
"      { \"drumZoneSettings\": { \"receivingNote\": 37, \"chokeGroup\": 2 },\n"
"        \"devices\": [ { \"kind\": \"drumCell\",\n"
"          \"parameters\": { \"Volume\": -6.0, \"Pan\": 0.0, \"Voice_Transpose\": 0,\n"
"                           \"Voice_Envelope_Decay\": 0.5 },\n"
"          \"deviceData\": { \"sampleUri\": \"ableton:/user-library/Samples/Snare%20Test.wav\" } } ] }\n"
"    ]\n"
"  } ] } ]\n"
"}\n";

int main() {
    const char *preset_path = "/tmp/mrdrums_test_preset.ablpreset";
    FILE *f = std::fopen(preset_path, "w");
    if (!f) { std::fprintf(stderr, "FAIL: cannot write fixture\n"); return 2; }
    std::fputs(kPresetJson, f);
    std::fclose(f);

    plugin_api_v2_t *api = move_plugin_init_v2(NULL);
    if (!api) { std::fprintf(stderr, "FAIL: plugin api unavailable\n"); return 2; }

    void *inst = api->create_instance(".", "{}");
    if (!inst) { std::fprintf(stderr, "FAIL: create_instance failed\n"); return 2; }

    api->set_param(inst, "ui_preset_path", preset_path);

    char err[256];
    std::memset(err, 0, sizeof(err));
    if (api->get_error(inst, err, sizeof(err)) > 0) {
        std::fprintf(stderr, "FAIL: plugin error: %s\n", err);
        api->destroy_instance(inst);
        return 1;
    }

    /* ableton:/user-library/ resolves to /data/UserData/UserLibrary/, %20 -> space */
    check_string_param(api, inst, "p01_sample_path",
                       "/data/UserData/UserLibrary/Samples/Preset Samples/Kick Test.wav");
    check_string_param(api, inst, "p02_sample_path",
                       "/data/UserData/UserLibrary/Samples/Snare Test.wav");

    /* Volume reference is -12 dB == 1.0; -6 dB == 10^((-6+12)/20) ~= 1.9953 */
    check_float_param(api, inst, "p01_vol", 1.0f);
    check_float_param(api, inst, "p02_vol", 1.9953f);
    check_float_param(api, inst, "p01_pan", 0.25f);
    check_float_param(api, inst, "p01_tune", 3.0f);
    check_float_param(api, inst, "p01_attack_ms", 0.1f);   /* 0.0001s * 1000 */
    check_float_param(api, inst, "p01_decay_ms", 1000.0f); /* 1.0s * 1000 */
    check_float_param(api, inst, "p02_choke_group", 2.0f);

    /* The loaded preset path is remembered for browser scroll-restore. */
    check_string_param(api, inst, "ui_preset_path", preset_path);

    /* Roundtrip: a saved set restores the preset path AND the resolved pad
     * paths WITHOUT re-reading the preset file (proves local state wins). */
    char state[8192];
    std::memset(state, 0, sizeof(state));
    if (api->get_param(inst, "state", state, (int)sizeof(state)) < 0) {
        std::fprintf(stderr, "FAIL: get state failed\n");
        return 1;
    }
    api->destroy_instance(inst);

    void *inst2 = api->create_instance(".", state);
    if (!inst2) { std::fprintf(stderr, "FAIL: create_instance (restore) failed\n"); return 2; }
    check_string_param(api, inst2, "ui_preset_path", preset_path);
    check_string_param(api, inst2, "p01_sample_path",
                       "/data/UserData/UserLibrary/Samples/Preset Samples/Kick Test.wav");
    api->destroy_instance(inst2);

    std::printf("PASS: mrdrums .ablpreset parsing, URI resolution, and state roundtrip\n");
    return 0;
}
