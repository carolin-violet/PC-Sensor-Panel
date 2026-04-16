#include <assert.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <unistd.h>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "dashboard_ui.h"

namespace {

constexpr spi_host_device_t kSpiHost = SPI2_HOST;

constexpr int kPanelWidth = 240;
constexpr int kPanelHeight = 320;
constexpr uint32_t kPixelClockHz = 20 * 1000 * 1000;

constexpr int kPinMosi = 4;
constexpr int kPinSclk = 5;
constexpr int kPinCs = 12;
constexpr int kPinDc = 18;
constexpr int kPinRst = 19;

constexpr int kLvglTickPeriodMs = 2;
constexpr int kLvglTaskMaxDelayMs = 500;
constexpr int kLvglTaskMinDelayMs = 1000 / CONFIG_FREERTOS_HZ;
constexpr int kLvglTaskStackSize = 6 * 1024;
constexpr int kLvglTaskPriority = 2;
constexpr int kLvglDrawBufferLines = 40;

const char* kTag = "esp-monitor";
_lock_t g_lvgl_api_lock;

bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t* edata, void* user_ctx) {
  LV_UNUSED(panel_io);
  LV_UNUSED(edata);
  lv_display_t* display = static_cast<lv_display_t*>(user_ctx);
  lv_display_flush_ready(display);
  return false;
}

void lvgl_flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
  esp_lcd_panel_handle_t panel_handle = static_cast<esp_lcd_panel_handle_t>(lv_display_get_user_data(display));
  const int offset_x1 = area->x1;
  const int offset_y1 = area->y1;
  const int offset_x2 = area->x2;
  const int offset_y2 = area->y2;

  lv_draw_sw_rgb565_swap(px_map, (offset_x2 + 1 - offset_x1) * (offset_y2 + 1 - offset_y1));
  esp_lcd_panel_draw_bitmap(panel_handle, offset_x1, offset_y1, offset_x2 + 1, offset_y2 + 1, px_map);
}

void increase_lvgl_tick(void* arg) {
  lv_tick_inc(kLvglTickPeriodMs);
}

void lvgl_port_task(void* arg) {
  ESP_LOGI(kTag, "Starting LVGL task");
  while (true) {
    _lock_acquire(&g_lvgl_api_lock);
    uint32_t delay_ms = lv_timer_handler();
    _lock_release(&g_lvgl_api_lock);

    delay_ms = MAX(delay_ms, static_cast<uint32_t>(kLvglTaskMinDelayMs));
    delay_ms = MIN(delay_ms, static_cast<uint32_t>(kLvglTaskMaxDelayMs));
    usleep(delay_ms * 1000);
  }
}

esp_lcd_panel_handle_t init_panel(esp_lcd_panel_io_handle_t* out_io_handle) {
  ESP_LOGI(kTag, "Initialize SPI bus");
  spi_bus_config_t bus_config = {};
  bus_config.mosi_io_num = kPinMosi;
  bus_config.miso_io_num = -1;
  bus_config.sclk_io_num = kPinSclk;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  bus_config.data4_io_num = -1;
  bus_config.data5_io_num = -1;
  bus_config.data6_io_num = -1;
  bus_config.data7_io_num = -1;
  bus_config.max_transfer_sz = kPanelWidth * kLvglDrawBufferLines * static_cast<int>(sizeof(lv_color16_t));
  bus_config.flags = SPICOMMON_BUSFLAG_MASTER;
  bus_config.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO;
  bus_config.intr_flags = 0;
  bus_config.data_io_default_level = 0;
  ESP_ERROR_CHECK(spi_bus_initialize(kSpiHost, &bus_config, SPI_DMA_CH_AUTO));

  ESP_LOGI(kTag, "Install ST7789 panel IO");
  esp_lcd_panel_io_handle_t io_handle = nullptr;
  const esp_lcd_panel_io_spi_config_t io_config = {
      .cs_gpio_num = kPinCs,
      .dc_gpio_num = kPinDc,
      .spi_mode = 0,
      .pclk_hz = kPixelClockHz,
      .trans_queue_depth = 2,
      .on_color_trans_done = nullptr,
      .user_ctx = nullptr,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .cs_ena_pretrans = 0,
      .cs_ena_posttrans = 0,
      .flags = {},
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(kSpiHost, &io_config, &io_handle));

  ESP_LOGI(kTag, "Install ST7789 panel driver");
  esp_lcd_panel_handle_t panel_handle = nullptr;
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
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));
  ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, false));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

  *out_io_handle = io_handle;
  return panel_handle;
}

lv_display_t* init_lvgl(esp_lcd_panel_handle_t panel_handle, esp_lcd_panel_io_handle_t io_handle) {
  ESP_LOGI(kTag, "Initialize LVGL");
  lv_init();

  lv_display_t* display = lv_display_create(kPanelWidth, kPanelHeight);
  const size_t draw_buffer_size = kPanelWidth * kLvglDrawBufferLines * sizeof(lv_color16_t);

  void* buffer_a = spi_bus_dma_memory_alloc(kSpiHost, draw_buffer_size, 0);
  void* buffer_b = spi_bus_dma_memory_alloc(kSpiHost, draw_buffer_size, 0);
  assert(buffer_a);
  assert(buffer_b);

  lv_display_set_buffers(display, buffer_a, buffer_b, draw_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_user_data(display, panel_handle);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, lvgl_flush_cb);

  const esp_lcd_panel_io_callbacks_t callbacks = {
      .on_color_trans_done = notify_lvgl_flush_ready,
  };
  ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &callbacks, display));

  const esp_timer_create_args_t lvgl_tick_timer_args = {
      .callback = &increase_lvgl_tick,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lvgl_tick",
      .skip_unhandled_events = false,
  };
  esp_timer_handle_t lvgl_tick_timer = nullptr;
  ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, kLvglTickPeriodMs * 1000));

  xTaskCreate(lvgl_port_task, "lvgl", kLvglTaskStackSize, nullptr, kLvglTaskPriority, nullptr);
  return display;
}

}  // namespace

extern "C" void app_main(void) {
  ESP_LOGI(kTag, "LVGL cyber-industrial dashboard boot");
  ESP_LOGI(kTag, "pins: MOSI=%d SCLK=%d CS=%d DC=%d RST=%d", kPinMosi, kPinSclk, kPinCs, kPinDc, kPinRst);

  esp_lcd_panel_io_handle_t io_handle = nullptr;
  esp_lcd_panel_handle_t panel_handle = init_panel(&io_handle);
  lv_display_t* display = init_lvgl(panel_handle, io_handle);

  _lock_acquire(&g_lvgl_api_lock);
  dashboard_ui_create(display);
  lv_obj_invalidate(lv_display_get_screen_active(display));
  lv_refr_now(display);
  _lock_release(&g_lvgl_api_lock);
}
