#pragma once
#include <cstdio>

// Map ESPHome log macros to printf so component code compiles without change.
// Suppress DEBUG-level messages (set to no-op) to keep test output clean.
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...)
