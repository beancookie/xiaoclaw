#pragma once

#include <stddef.h>
#include "esp_err.h"

esp_err_t fatfs_write_atomic(const char *path, const void *data, size_t len);
esp_err_t fatfs_ensure_file(const char *path, const char *content);
