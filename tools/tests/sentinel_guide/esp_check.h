#pragma once
#define ESP_RETURN_ON_ERROR(expression, tag, message, ...) do { \
    (void)(tag); const esp_err_t err_ = (expression); \
    if (err_ != ESP_OK) return err_; \
} while (0)
