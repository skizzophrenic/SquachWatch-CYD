# Patches the (already retry-patched) OTA tool's main.c so that, after the
# verify phase reads the version back, it asks the extension to say hello
# over CustomRpc and reports the answer: the only way to tell our image from
# the stock one, which carries the same version number.
import sys
p = sys.argv[1]
s = open(p).read()
a = 'static const char *TAG = "tab5-c6-ota";'
assert s.count(a) == 1
s = s.replace(a, a + '''
#include "esp_hosted_misc.h"
#include "freertos/event_groups.h"
static EventGroupHandle_t s_hello_evt;
static char s_hello_tag[17];
static uint8_t s_hello_on = 0xFF;
static void on_hello(uint32_t id, const uint8_t *d, size_t n, void *ctx)
{
    if (n >= 17) { memcpy(s_hello_tag, d, 16); s_hello_tag[16] = 0; s_hello_on = d[16]; }
    xEventGroupSetBits(s_hello_evt, 1);
}
/* Ask the SquachWatch extension (if this image has it) to say hello. */
static bool squach_hello(void)
{
    s_hello_evt = xEventGroupCreate();
    esp_hosted_register_custom_callback(0x5181, on_hello, NULL);
    const esp_err_t e = esp_hosted_send_custom_data(0x5103, (const uint8_t *)"?", 1);   /* the host refuses an empty payload */
    if (e != ESP_OK) { ESP_LOGW(TAG, "[WARN] hello could not be sent: %s", esp_err_to_name(e)); return false; }
    if (xEventGroupWaitBits(s_hello_evt, 1, pdTRUE, pdTRUE, pdMS_TO_TICKS(4000)) & 1) {
        ESP_LOGI(TAG, "[PASS] extension answered: \\"%s\\", sniffing=%u", s_hello_tag, (unsigned)s_hello_on);
        { nvs_handle_t h; if (nvs_open("squach", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, "tries", 0); nvs_commit(h); nvs_close(h); } }
        return true;
    }
    ESP_LOGW(TAG, "[WARN] no hello within 4 s: this image has no SquachWatch extension (stock)");
    return false;
}''')
# After any successful version read in the verify phase and in the
# nothing-to-do path, say hello.
a = '''            ESP_LOGI(TAG, "[PASS] The C6 already runs the version of the host, nothing to do");
            halt();'''
assert s.count(a) == 1
s = s.replace(a, '''            /* Same version as the host: stock or ours? Only the hello tells. */
            if (squach_hello()) {
                ESP_LOGI(TAG, "[PASS] The C6 already runs our image, nothing to do");
                halt();
            }
            ESP_LOGW(TAG, "[DIAG] Same version but no extension: writing our image");
            {
                nvs_handle_t h; uint8_t tries = 0;
                if (nvs_open("squach", NVS_READWRITE, &h) == ESP_OK) {
                    nvs_get_u8(h, "tries", &tries);
                    if (tries >= 2) { ESP_LOGE(TAG, "[FAIL] Written twice already with no hello back; stopping so this never loops"); nvs_close(h); halt(); }
                    nvs_set_u8(h, "tries", tries + 1); nvs_commit(h); nvs_close(h);
                }
            }''')
open(p, 'w').write(s)
print('patched')
