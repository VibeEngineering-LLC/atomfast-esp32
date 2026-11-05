#pragma once
#include <stdio.h>
#define ESP_LOGI(tag, fmt, ...) do { if (0) printf("%s " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGW(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
