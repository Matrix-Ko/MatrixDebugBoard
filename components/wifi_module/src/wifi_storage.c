#include "wifi_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "WIFI_STORAGE";
static const char *NVS_NS = "wifi_cfg";

esp_err_t wifi_storage_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t wifi_storage_save(const char *ssid, const char *password) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "password", password);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Saved: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_storage_load(char *ssid, size_t ssid_len, char *password, size_t pwd_len) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_str(h, "ssid", ssid, &ssid_len);
    if (err == ESP_OK) err = nvs_get_str(h, "password", password, &pwd_len);
    nvs_close(h);
    return err;
}

bool wifi_storage_has_config(void) {
    nvs_handle_t h;
    char ssid[64] = {0};
    size_t len = sizeof(ssid);
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool has = (nvs_get_str(h, "ssid", ssid, &len) == ESP_OK && ssid[0] != '\0');
    nvs_close(h);
    return has;
}

esp_err_t wifi_storage_clear(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_erase_key(h, "ssid");
    nvs_erase_key(h, "password");
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t wifi_storage_save_ap(const char *ssid, const char *password) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "ap_ssid", ssid);
    nvs_set_str(h, "ap_pwd",  password);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "AP config saved: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_storage_load_ap(char *ssid, size_t ssid_len, char *password, size_t pwd_len) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return ESP_FAIL;
    size_t sl = ssid_len, pl = pwd_len;
    nvs_get_str(h, "ap_ssid", ssid,     &sl);
    nvs_get_str(h, "ap_pwd",  password, &pl);
    nvs_close(h);
    return ESP_OK;
}
