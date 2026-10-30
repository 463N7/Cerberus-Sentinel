#pragma once
#define ESP_RETURN_ON_ERROR(call, tag, ...) do { (void)(tag); esp_err_t err_ = (call); if (err_ != ESP_OK) return err_; } while (0)
