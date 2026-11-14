# Patches shadowlab's tab5-p4-c6-sdio-ota main.c (copied to ~/tab5-c6/ota54):
# the link often comes up only on a second connect (seen on the bench), and
# an image is never sent to a chip whose version could not be read.
import sys
p = sys.argv[1]
s = open(p).read()
a = '''    esp_hosted_coprocessor_fwver_t ver;
    if (read_version(&ver))
    {'''
b = '''    esp_hosted_coprocessor_fwver_t ver;
    /* SquachWatch: retry the link, and never write blind. */
    bool have_ver = read_version(&ver);
    for (int i = 0; i < 4 && !have_ver; i++)
    {
        ESP_LOGW(TAG, "[WARN] No version yet, reconnecting (%d)", i + 1);
        esp_hosted_connect_to_slave();
        vTaskDelay(pdMS_TO_TICKS(3000));
        have_ver = read_version(&ver);
    }
    if (!have_ver)
    {
        ESP_LOGE(TAG, "[FAIL] The C6 never reported a version; nothing is written");
        halt();
    }
    if (have_ver)
    {'''
assert s.count(a) == 1
s = s.replace(a, b)
a = '''    else
    {
        ESP_LOGW(TAG, "[WARN] Could not read the C6 version (old firmware?), updating anyway");
    }
'''
assert s.count(a) == 1
s = s.replace(a, '')
open(p, 'w').write(s)
print('patched')
