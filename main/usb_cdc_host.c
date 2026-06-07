#include "usb_cdc_host.h"
#include "bridge.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

static const char *TAG = "usb_cdc";

// Candidate Virtual COM Port USB IDs across the FC MCU families Betaflight
// targets. We try each in turn; the first that enumerates wins. Extend this list
// as new vendors appear.
typedef struct {
    uint16_t vid;
    uint16_t pid;
    const char *name;
} vcp_id_t;

static const vcp_id_t k_vcp_ids[] = {
    { 0x0483, 0x5740, "STM32 VCP" },     // ST (and many clones reporting ST IDs)
    { 0x2E3C, 0x5740, "AT32 VCP" },      // Artery
    { 0x314B, 0x5740, "APM32 VCP" },     // Geehy
};

#define VCP_LINE_CODING_BAUD   115200
#define USB_RX_TX_TIMEOUT_MS   1000

static cdc_acm_dev_hdl_t s_cdc_dev = NULL;
static volatile bool s_connected = false;
static SemaphoreHandle_t s_dev_gone;   // given when the device disconnects

bool usb_cdc_host_is_connected(void)
{
    return s_connected;
}

// FC -> bridge. Runs in the CDC driver task context; keep it short.
static bool on_cdc_rx(const uint8_t *data, size_t data_len, void *arg)
{
    bridge_usb_to_net_push(data, data_len);
    return true;  // we have consumed the buffer
}

static void on_cdc_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    switch (event->type) {
    case CDC_ACM_HOST_ERROR:
        ESP_LOGE(TAG, "CDC-ACM error %d", event->data.error);
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        ESP_LOGW(TAG, "FC disconnected");
        s_connected = false;
        cdc_acm_host_close(event->data.cdc_hdl);
        s_cdc_dev = NULL;
        xSemaphoreGive(s_dev_gone);
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        // DCD/DSR line state changes — informational for a transparent bridge.
        break;
    case CDC_ACM_HOST_NETWORK_CONNECTION:
        break;
    default:
        break;
    }
}

// Drives the USB host library event loop for the lifetime of the app.
static void usb_lib_task(void *arg)
{
    while (1) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

// net -> FC. Drains the bridge and forwards to the open VCP.
static void usb_tx_task(void *arg)
{
    static uint8_t buf[512];
    while (1) {
        size_t n = bridge_net_to_usb_pop(buf, sizeof(buf), 100);
        if (n == 0 || !s_connected || s_cdc_dev == NULL) {
            continue;
        }
        esp_err_t err = cdc_acm_host_data_tx_blocking(s_cdc_dev, buf, n, USB_RX_TX_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "tx to FC failed: %s", esp_err_to_name(err));
        }
    }
}

// Repeatedly attempts to open a known FC VCP; reconnects after disconnects.
static void usb_connect_task(void *arg)
{
    const cdc_acm_host_device_config_t dev_cfg = {
        .connection_timeout_ms = 1000,
        .out_buffer_size = 512,
        .in_buffer_size = 512,
        .user_arg = NULL,
        .event_cb = on_cdc_event,
        .data_cb = on_cdc_rx,
    };

    while (1) {
        bool opened = false;
        for (size_t i = 0; i < sizeof(k_vcp_ids) / sizeof(k_vcp_ids[0]); i++) {
            esp_err_t err = cdc_acm_host_open(k_vcp_ids[i].vid, k_vcp_ids[i].pid,
                                              0, &dev_cfg, &s_cdc_dev);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "opened %s (%04x:%04x)", k_vcp_ids[i].name,
                         k_vcp_ids[i].vid, k_vcp_ids[i].pid);
                cdc_acm_line_coding_t coding = {
                    .dwDTERate = VCP_LINE_CODING_BAUD,
                    .bCharFormat = 0,   // 1 stop bit
                    .bParityType = 0,   // none
                    .bDataBits = 8,
                };
                cdc_acm_host_line_coding_set(s_cdc_dev, &coding);
                cdc_acm_host_set_control_line_state(s_cdc_dev, true, true);  // DTR, RTS
                bridge_reset();
                s_connected = true;
                opened = true;
                break;
            }
        }

        if (!opened) {
            // No FC present yet; back off briefly and retry.
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // Block until this device disconnects, then loop to reconnect.
        xSemaphoreTake(s_dev_gone, portMAX_DELAY);
    }
}

void usb_cdc_host_start(void)
{
    s_dev_gone = xSemaphoreCreateBinary();
    configASSERT(s_dev_gone);

    const usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_cfg));

    BaseType_t ok = xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 10, NULL);
    configASSERT(ok == pdTRUE);

    ESP_ERROR_CHECK(cdc_acm_host_install(NULL));

    xTaskCreate(usb_tx_task, "usb_tx", 4096, NULL, 6, NULL);
    xTaskCreate(usb_connect_task, "usb_connect", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "USB host started; waiting for FC VCP");
}
