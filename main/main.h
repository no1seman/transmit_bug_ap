#include <stdint.h>
#include <stddef.h>

#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_AP_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_AP_PASSWORD
#define EXAMPLE_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN

typedef enum
{
    _RES_TYPE_JS = 0,
    _RES_TYPE_JSON,
    _RES_TYPE_HTML,
    _RES_TYPE_CSS,
    _RES_TYPE_ICO,
    _RES_TYPE_FONT_WOFF2,
    _RES_TYPE_PDF,
    _RES_TYPE_MAX
} res_type_t;

typedef enum
{
    _STATIC_INDEX_HTML = 0,
    _STATIC_INDEX_CSS,
    _STATIC_INDEX_JS,
    _STATIC_MAX_FILE
} static_file_t;

typedef struct file_res_type
{
    char *uri_type;
    char *file_ext;
} file_res_type_t;

typedef struct static_cache
{
    const char *filename;
    const char *uri;
    const res_type_t resptype;
    bool gzipped;
    char *buf;
    size_t len;
} static_cache_t;