#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "esp_eth.h"
#include "protocol_examples_common.h"

#include <esp_http_server.h>
#include "mbedtls/md5.h"

#include "main.h"

const file_res_type_t res_types[_RES_TYPE_MAX] =
    {
        [_RES_TYPE_JS] {
            .uri_type = "application/javascript; charset=UTF-8",
            .file_ext = ".js"
        },
        [_RES_TYPE_JSON] {
            .uri_type = "application/json",
            .file_ext = ".json"
        },
        [_RES_TYPE_HTML] {
            .uri_type = "text/html; charset=UTF-8",
            .file_ext = ".html"
        },
        [_RES_TYPE_CSS] {
            .uri_type = "text/css; charset=UTF-8",
            .file_ext = ".css"
        },
        [_RES_TYPE_ICO] {
            .uri_type = "image/x-icon",
            .file_ext = ".ico"
        },
        [_RES_TYPE_FONT_WOFF2] {
            .uri_type = "font/woff2",
            .file_ext = ".woff2"
        },
        [_RES_TYPE_PDF] {
            .uri_type = "application/pdf",
            .file_ext = ".pdf"
        }};

static_cache_t static_files_cache[_STATIC_MAX_FILE] =
    {
        [_STATIC_INDEX_HTML] {
            .filename = "index.html.gz",
            .uri = "index.html",
            .resptype = _RES_TYPE_HTML,
            .gzipped = true,
            .buf = NULL,
            .len = 10930
        },
        [_STATIC_INDEX_CSS] {
            .filename = "index.css.gz",
            .uri = "index.css",
            .resptype = _RES_TYPE_CSS,
            .gzipped = true,
            .buf = NULL,
            .len = 38099
        },
        [_STATIC_INDEX_JS] {
            .filename = "index.js.gz",
            .uri = "index.js",
            .resptype = _RES_TYPE_JS,
            .gzipped = true,
            .buf = NULL,
            .len = 75723
        }};

static const char *TAG = "example";

char atoc(uint8_t value, bool capital)
{
    if (value < 10)
    {
        return (char)(value + 48);
    }
    else
    {
        if (capital)
            return (char)(value + 55);
        else
            return (char)(value + 87);
    }
}

char *array2hex(const uint8_t *hex, char *target, uint8_t len, bool capital)
{
    int i, hop = 0;
    for (i = 0; i < len; i++)
    {
        target[hop] = atoc((hex[i] & 0xf0) >> 4, capital);
        target[hop + 1] = atoc((hex[i] & 0x0f), capital);
        hop += 2;
    }
    target[hop] = '\0';
    return (target);
}

esp_err_t http_get_hdr_value(httpd_req_t *req, char *key, char **value)
{
    size_t value_len = httpd_req_get_hdr_value_len(req, key);
    if (value_len > 1)
    {
        *value = pvPortMalloc(value_len + 1);
        httpd_req_get_hdr_value_str(req, key, (char *)*value, value_len + 1);

        ESP_LOGD(TAG, "Request has key: '%s' - value: '%s'", key, *value);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t get_static_file(static_file_t static_file, httpd_req_t *req)
{
    esp_err_t res;
    char *buf = NULL;
    if (http_get_hdr_value(req, "Accept-Encoding", &buf) == ESP_OK)
    {
        if (strstr(buf, "gzip"))
        {
            uint32_t start_time = esp_log_timestamp();
            ESP_LOGI(TAG, "Get static file: %s", static_files_cache[static_file].filename);
            if (static_files_cache[static_file].buf)
            {
                if (static_files_cache[static_file].gzipped)
                    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
                httpd_resp_set_type(req, res_types[static_files_cache[static_file].resptype].uri_type);
                httpd_resp_set_status(req, HTTPD_200);
                unsigned char md5[16];
                char md5hex[34];
                mbedtls_md5_ret((const unsigned char *)static_files_cache[static_file].buf, static_files_cache[static_file].len, md5);
                httpd_resp_set_hdr(req, "MD5HASH", array2hex((const uint8_t *)&md5, (char *)&md5hex, 16, false));
                res = httpd_resp_send(req, static_files_cache[static_file].buf, static_files_cache[static_file].len);
                uint32_t end_time = esp_log_timestamp();
                if (res == ESP_OK)
                    ESP_LOGI(TAG, "Sending file '%s' took %u ms", static_files_cache[static_file].filename, end_time - start_time);
                else
                    ESP_LOGI(TAG, "Sending file '%s' failed", static_files_cache[static_file].filename);
                return res;
            }
            res = httpd_resp_send_404(req);
            return res;
        }
    }
    res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server supports only gzipped responds. Try another browser");
    return res;
}

esp_err_t get_file_handler(httpd_req_t *req)
{
    esp_err_t res;
    uint32_t start_time = esp_log_timestamp();
    ESP_LOGI(TAG, "GET %s", req->uri);

    for (int i = 0; i < _STATIC_MAX_FILE; i++)
    {
        if (strncasecmp(req->uri + 1, static_files_cache[i].uri, strlen(static_files_cache[i].uri)) == 0)
        {
            res = get_static_file(i, req);
            uint32_t end_time = esp_log_timestamp();
            ESP_LOGI(TAG, "Processing request '%s' took %u ms", static_files_cache[i].filename, end_time - start_time);
            return res;
        }
    }
    res = httpd_resp_send_404(req);
    return res;
}

static const httpd_uri_t get_file_uri = {
    .uri = "/*",
    .method = HTTP_GET,
    .handler = get_file_handler,
    .user_ctx = NULL};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_open_sockets = 6;
    config.max_uri_handlers = 4;
    config.max_resp_headers = 8;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &get_file_uri);

        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server)
    {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}

static void connect_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server == NULL)
    {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}

void make_http_cache(void)
{
    unsigned char md5[16];
    char tgt[34];
    ESP_LOGI(TAG, "Caching HTTP static files...");
    for (int i = 0; i < _STATIC_MAX_FILE; i++)
    {

        static_files_cache[i].buf = pvPortMalloc(static_files_cache[i].len);
        for (int j = 0; j < static_files_cache[j].len; j++)
            static_files_cache[i].buf[j] = (char)rand();

        mbedtls_md5_ret((const unsigned char *)static_files_cache[i].buf, static_files_cache[i].len, md5);
        ESP_LOGI(TAG, "%s file md5 summ: %s", static_files_cache[i].filename, array2hex((const uint8_t *)&md5, (char *)&tgt, 16, false));
    }
    ESP_LOGI(TAG, "Caching HTTP static files completed");
}

static void on_wifi_ap_staconnected(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "station connected to AP:" MACSTR ", AID = %d", MAC2STR(event->mac), event->aid);
}

static void on_wifi_ap_stadisconnected(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "station disconnected from AP: " MACSTR " , AID=%d", MAC2STR(event->mac), event->aid);
}

void wifi_init_softap(void)
{
#ifndef CONFIG_EXAMPLE_CONNECT_WIFI
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
#endif
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, on_wifi_ap_staconnected, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, on_wifi_ap_stadisconnected, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
#else
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
#endif
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

void app_main(void)
{
    static httpd_handle_t server = NULL;
    esp_log_level_set("wifi", ESP_LOG_WARN);
    make_http_cache();
    ESP_ERROR_CHECK(nvs_flash_init());
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_log_level_set("httpd_txrx", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(example_connect());

#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_WIFI
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET
    wifi_init_softap();
    /* Start the server for the first time */
    server = start_webserver();
}
