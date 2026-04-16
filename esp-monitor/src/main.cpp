#include <inttypes.h>

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr spi_host_device_t kSpiHost = SPI2_HOST;

constexpr int kPanelWidth = 240;
constexpr int kPanelHeight = 320;
constexpr int kLineCount = 20;
constexpr uint32_t kPixelClockHz = 20 * 1000 * 1000;

constexpr int kPinMosi = 4;
constexpr int kPinSclk = 5;
constexpr int kPinCs = 12;
constexpr int kPinDc = 18;
constexpr int kPinRst = 19;

constexpr TickType_t kFrameDelay = pdMS_TO_TICKS(1200);
const char* kTag = "esp-monitor";

struct ColorFrame {
  uint16_t color;
  const char* label;
};

constexpr ColorFrame kFrames[] = {
    {0xF800, "RED"},
    {0x07E0, "GREEN"},
    {0x001F, "BLUE"},
    {0xFFFF, "WHITE"},
    {0x0000, "BLACK"},
    {0xFFE0, "YELLOW"},
};

esp_lcd_panel_handle_t s_panel = nullptr;
uint16_t s_line_buffer[kPanelWidth * kLineCount];

esp_err_t fill_screen(uint16_t color) {
  for (size_t i = 0; i < sizeof(s_line_buffer) / sizeof(s_line_buffer[0]); ++i) {
    s_line_buffer[i] = color;
  }

  for (int y = 0; y < kPanelHeight; y += kLineCount) {
    const int lines = (y + kLineCount > kPanelHeight) ? (kPanelHeight - y) : kLineCount;
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, kPanelWidth, y + lines, s_line_buffer),
        kTag,
        "draw_bitmap failed at row %d",
        y);
  }

  return ESP_OK;
}

esp_err_t init_panel() {
  const spi_bus_config_t bus_config = {
      .mosi_io_num = kPinMosi,
      .miso_io_num = -1,
      .sclk_io_num = kPinSclk,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .data4_io_num = -1,
      .data5_io_num = -1,
      .data6_io_num = -1,
      .data7_io_num = -1,
      .max_transfer_sz = static_cast<int>(sizeof(s_line_buffer)),
      .flags = SPICOMMON_BUSFLAG_MASTER,
      .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
      .intr_flags = 0,
  };
  ESP_RETURN_ON_ERROR(spi_bus_initialize(kSpiHost, &bus_config, SPI_DMA_CH_AUTO), kTag, "spi_bus_initialize failed");

  esp_lcd_panel_io_handle_t io_handle = nullptr;
  const esp_lcd_panel_io_spi_config_t io_config = {
      .cs_gpio_num = kPinCs,
      .dc_gpio_num = kPinDc,
      .spi_mode = 0,
      .pclk_hz = kPixelClockHz,
      .trans_queue_depth = 10,
      .on_color_trans_done = nullptr,
      .user_ctx = nullptr,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .cs_ena_pretrans = 0,
      .cs_ena_posttrans = 0,
      .flags = {},
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(kSpiHost, &io_config, &io_handle), kTag, "new_panel_io_spi failed");

  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = kPinRst,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
      .bits_per_pixel = 16,
      .flags = {
          .reset_active_high = 0,
      },
      .vendor_config = nullptr,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel), kTag, "new_panel_st7789 failed");

  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), kTag, "panel_reset failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), kTag, "panel_init failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, false), kTag, "invert_color failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 0, 0), kTag, "set_gap failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, false), kTag, "mirror failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, false), kTag, "swap_xy failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), kTag, "disp_on_off failed");

  return ESP_OK;
}

}  // namespace

extern "C" void app_main(void) {
  ESP_LOGI(kTag, "st7789 bring-up start");
  ESP_LOGI(
      kTag,
      "pins: MOSI=%d SCLK=%d CS=%d DC=%d RST=%d",
      kPinMosi,
      kPinSclk,
      kPinCs,
      kPinDc,
      kPinRst);

  ESP_ERROR_CHECK(init_panel());
  ESP_LOGI(kTag, "panel init done");

  size_t frame = 0;
  while (true) {
    const ColorFrame& current = kFrames[frame];
    ESP_LOGI(kTag, "fill screen: %s (0x%04X)", current.label, current.color);
    ESP_ERROR_CHECK(fill_screen(current.color));
    frame = (frame + 1) % (sizeof(kFrames) / sizeof(kFrames[0]));
    vTaskDelay(kFrameDelay);
  }
}
