// Compile the actual ESP32 storage branch. Only /sd VFS routing and frame
// locking are adapted to the host; all writer logic/syscall checks are real.
#define SQW_SD_ESP32_HOST 1
#include "wardrive_sd_failure_test.cpp"
