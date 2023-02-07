#pragma once

#include <memory>
#include <esp_wifi.h>

std::string const static NETIF_NAME = "WIFI";

class Wifi {
  public:
    enum class WorkMode {
        Station,
        AccessPoint,
        DoubleDuty
    };

    static void Create(WorkMode wm) noexcept { _this = std::shared_ptr<Wifi>(new Wifi{ wm }); }

    static std::shared_ptr<Wifi> Get() noexcept
    {
        if (not _this) {
            ESP_LOGE("WIFI", "Get invoked with initialized Wifi! Call \"Run\" first!");
            std::terminate();
        }

        return _this;
    }

  protected:
    static bool IsOurNetif(const char *prefix, esp_netif_t *netif) noexcept
    {
        return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
    }

    static void OnDisconnect(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) noexcept
    {
        EspLogger::Log("wifi", "Wi-Fi disconnected, trying to reconnect...");

        esp_err_t err = esp_wifi_connect();
        if (err == ESP_ERR_WIFI_NOT_STARTED) {
            return;
        }

        ESP_ERROR_CHECK(err);
    }

    static void OnGotIp(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        if (!IsOurNetif(NETIF_NAME.c_str(), event->esp_netif)) {
            ESP_LOGW( NETIF_NAME.c_str(), "Got IPv4 from another interface \"%s\": ignored", esp_netif_get_desc(event->esp_netif));
            return;
        }

        ESP_LOGI( NETIF_NAME.c_str(),
                 "Got IPv4 event: Interface \"%s\" address: " IPSTR,
                 esp_netif_get_desc(event->esp_netif),
                 IP2STR(&event->ip_info.ip));
        memcpy(&s_ip_addr, &event->ip_info.ip, sizeof(s_ip_addr));
//        xSemaphoreGive(s_semph_get_ip_addrs);
    }

  private:
    Wifi(WorkMode wm)
      : workMode{ wm }
    {
        esp_netif_init();

        ESP_ERROR_CHECK(esp_event_loop_create_default());

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
        char                       *desc;
        asprintf(&desc, "%s: %s", NETIF_NAME.c_str(), esp_netif_config.if_desc);
        esp_netif_config.if_desc    = desc;
        esp_netif_config.route_prio = 128;
        esp_netif_t *netif          = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
        free(desc);

        esp_wifi_set_default_wifi_sta_handlers();

        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        wifi_config_t sta_config = {};
        strcpy((char *)sta_config.sta.ssid, "gktgdu53479");
        strcpy((char *)sta_config.sta.password, "x5r1a2jAsd099876");
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
    }

    static std::shared_ptr<Wifi> _this;
    static esp_ip4_addr_t        s_ip_addr;

    WorkMode workMode;
};