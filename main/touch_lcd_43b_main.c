#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_WIDTH 800
#define LCD_HEIGHT 480

#define LCD_DATA0_GPIO 14
#define LCD_DATA1_GPIO 38
#define LCD_DATA2_GPIO 18
#define LCD_DATA3_GPIO 17
#define LCD_DATA4_GPIO 10
#define LCD_DATA5_GPIO 39
#define LCD_DATA6_GPIO 0
#define LCD_DATA7_GPIO 45
#define LCD_DATA8_GPIO 48
#define LCD_DATA9_GPIO 47
#define LCD_DATA10_GPIO 21
#define LCD_DATA11_GPIO 1
#define LCD_DATA12_GPIO 2
#define LCD_DATA13_GPIO 42
#define LCD_DATA14_GPIO 41
#define LCD_DATA15_GPIO 40
#define LCD_PCLK_GPIO 7
#define LCD_HSYNC_GPIO 46
#define LCD_VSYNC_GPIO 3
#define LCD_DE_GPIO 5

#define TOUCH_SDA_GPIO 8
#define TOUCH_SCL_GPIO 9
#define TOUCH_INT_GPIO 4
#define TOUCH_I2C_FREQUENCY_HZ 400000
#define GT911_CONFIG_X_MAX_REG 0x8048
#define GT911_CONFIG_Y_MAX_REG 0x804A
#define GT911_STATUS_REG 0x814E
#define GT911_POINT1_REG 0x814F

#define BUTTON_COUNT 4
#define BUTTON_MARGIN 24
#define BUTTON_GAP 20
#define BUTTON_TOP 76
#define BUTTON_HEIGHT 166
#define BUTTON_WIDTH ((LCD_WIDTH - (2 * BUTTON_MARGIN) - BUTTON_GAP) / 2)

static const char *TAG = "touch_lcd_43b";
static i2c_master_dev_handle_t touch_device;
static uint16_t *frame_buffer;
static uint16_t touch_max_x = LCD_WIDTH;
static uint16_t touch_max_y = LCD_HEIGHT;

static void fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT || width == 0 || height == 0) {
        return;
    }
    if (x + width > LCD_WIDTH) {
        width = LCD_WIDTH - x;
    }
    if (y + height > LCD_HEIGHT) {
        height = LCD_HEIGHT - y;
    }
    for (uint16_t row = y; row < y + height; row++) {
        uint16_t *line = frame_buffer + (size_t)row * LCD_WIDTH + x;
        for (uint16_t column = 0; column < width; column++) {
            line[column] = color;
        }
    }
}

static uint8_t glyph_column(char character, uint8_t column)
{
    static const uint8_t digits[][5] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
        {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
        {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
        {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
        {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}
    };
    static const uint8_t letters[][5] = {
        {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
        {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
        {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
        {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
        {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01},
        {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
        {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
        {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
        {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
        {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01},
        {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
        {0x7F, 0x20, 0x18, 0x20, 0x7F}, {0x63, 0x14, 0x08, 0x14, 0x63},
        {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}
    };
    if (character >= '0' && character <= '9') {
        return digits[character - '0'][column];
    }
    if (character >= 'A' && character <= 'Z') {
        return letters[character - 'A'][column];
    }
    return 0;
}

static void draw_text(uint16_t x, uint16_t y, const char *text, uint16_t color, uint8_t scale)
{
    for (size_t character_index = 0; text[character_index] != '\0'; character_index++) {
        for (uint8_t column = 0; column < 5; column++) {
            uint8_t bits = glyph_column(text[character_index], column);
            for (uint8_t row = 0; row < 7; row++) {
                if (bits & (1 << row)) {
                    fill_rect(x + column * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

static void draw_button(uint8_t button, bool pressed)
{
    static const char *labels[BUTTON_COUNT] = {"BUTTON 1", "BUTTON 2", "BUTTON 3", "BUTTON 4"};
    const uint16_t inactive_colors[BUTTON_COUNT] = {0x19C3, 0xD303, 0xA245, 0x4A69};
    const uint16_t active_colors[BUTTON_COUNT] = {0x07E0, 0xFFE0, 0xF800, 0x07FF};
    uint16_t x = BUTTON_MARGIN + (button % 2) * (BUTTON_WIDTH + BUTTON_GAP);
    uint16_t y = BUTTON_TOP + (button / 2) * (BUTTON_HEIGHT + BUTTON_GAP);
    uint16_t color = pressed ? active_colors[button] : inactive_colors[button];
    uint16_t label_width = strlen(labels[button]) * 6 * 4;

    fill_rect(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, color);
    draw_text(x + (BUTTON_WIDTH - label_width) / 2, y + (BUTTON_HEIGHT - 28) / 2, labels[button], 0xFFFF, 4);
}

static void draw_screen(int pressed_button)
{
    const uint16_t background = 0x0841;
    const char *title = "TOUCH TEST";
    uint16_t title_width = strlen(title) * 6 * 5;

    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, background);
    draw_text((LCD_WIDTH - title_width) / 2, 20, title, 0xFFFF, 5);
    for (uint8_t button = 0; button < BUTTON_COUNT; button++) {
        draw_button(button, button == pressed_button);
    }
}

static bool point_in_button(uint16_t x, uint16_t y, uint8_t *button)
{
    if (y < BUTTON_TOP || y >= BUTTON_TOP + 2 * BUTTON_HEIGHT + BUTTON_GAP) {
        return false;
    }
    uint8_t column = x >= BUTTON_MARGIN + BUTTON_WIDTH + BUTTON_GAP;
    uint8_t row = y >= BUTTON_TOP + BUTTON_HEIGHT + BUTTON_GAP;
    uint16_t button_x = BUTTON_MARGIN + column * (BUTTON_WIDTH + BUTTON_GAP);
    uint16_t button_y = BUTTON_TOP + row * (BUTTON_HEIGHT + BUTTON_GAP);
    if (x < button_x || x >= button_x + BUTTON_WIDTH || y < button_y || y >= button_y + BUTTON_HEIGHT) {
        return false;
    }
    *button = row * 2 + column;
    return true;
}

static esp_err_t gt911_read(uint16_t register_address, uint8_t *data, size_t length)
{
    uint8_t address[] = {register_address >> 8, register_address & 0xFF};
    return i2c_master_transmit_receive(touch_device, address, sizeof(address), data, length, -1);
}

static esp_err_t gt911_write(uint16_t register_address, uint8_t value)
{
    uint8_t data[] = {register_address >> 8, register_address & 0xFF, value};
    return i2c_master_transmit(touch_device, data, sizeof(data), -1);
}

static uint16_t scale_touch_coordinate(uint16_t value, uint16_t source_max, uint16_t target_max)
{
    if (source_max <= 1 || target_max <= 1) {
        return value;
    }
    return (uint32_t)value * (target_max - 1) / (source_max - 1);
}

static void map_touch_point(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y)
{
    if (touch_max_x < touch_max_y) {
        *x = scale_touch_coordinate(raw_y, touch_max_y, LCD_WIDTH);
        *y = scale_touch_coordinate(raw_x, touch_max_x, LCD_HEIGHT);
        return;
    }
    *x = scale_touch_coordinate(raw_x, touch_max_x, LCD_WIDTH);
    *y = scale_touch_coordinate(raw_y, touch_max_y, LCD_HEIGHT);
}

static bool touch_read(uint16_t *x, uint16_t *y)
{
    if (touch_device == NULL) {
        return false;
    }
    uint8_t status;
    if (gt911_read(GT911_STATUS_REG, &status, sizeof(status)) != ESP_OK || !(status & 0x80)) {
        return false;
    }

    uint8_t point[8];
    bool touched = false;
    if ((status & 0x0F) > 0 && gt911_read(GT911_POINT1_REG, point, sizeof(point)) == ESP_OK) {
        uint16_t raw_x = point[1] | ((uint16_t)point[2] << 8);
        uint16_t raw_y = point[3] | ((uint16_t)point[4] << 8);
        map_touch_point(raw_x, raw_y, x, y);
        touched = *x < LCD_WIDTH && *y < LCD_HEIGHT;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(gt911_write(GT911_STATUS_REG, 0));
    return touched;
}

static void init_display(void)
{
    const int data_gpios[] = {
        LCD_DATA0_GPIO, LCD_DATA1_GPIO, LCD_DATA2_GPIO, LCD_DATA3_GPIO,
        LCD_DATA4_GPIO, LCD_DATA5_GPIO, LCD_DATA6_GPIO, LCD_DATA7_GPIO,
        LCD_DATA8_GPIO, LCD_DATA9_GPIO, LCD_DATA10_GPIO, LCD_DATA11_GPIO,
        LCD_DATA12_GPIO, LCD_DATA13_GPIO, LCD_DATA14_GPIO, LCD_DATA15_GPIO,
    };
    const esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_width = 16,
        .num_fbs = 1,
        .bounce_buffer_size_px = LCD_WIDTH * 10,
        .timings = {
            .pclk_hz = 16000000,
            .h_res = LCD_WIDTH,
            .v_res = LCD_HEIGHT,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = true,
        },
        .hsync_gpio_num = LCD_HSYNC_GPIO,
        .vsync_gpio_num = LCD_VSYNC_GPIO,
        .de_gpio_num = LCD_DE_GPIO,
        .pclk_gpio_num = LCD_PCLK_GPIO,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            data_gpios[0], data_gpios[1], data_gpios[2], data_gpios[3],
            data_gpios[4], data_gpios[5], data_gpios[6], data_gpios[7],
            data_gpios[8], data_gpios[9], data_gpios[10], data_gpios[11],
            data_gpios[12], data_gpios[13], data_gpios[14], data_gpios[15],
        },
        .flags.fb_in_psram = true,
    };
    esp_lcd_panel_handle_t panel;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 1, (void **)&frame_buffer));
}

static void init_touch(void)
{
    const gpio_config_t touch_int_output_config = {
        .pin_bit_mask = 1ULL << TOUCH_INT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&touch_int_output_config));
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_INT_GPIO, 0));

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = TOUCH_SDA_GPIO,
        .scl_io_num = TOUCH_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));

    const i2c_device_config_t expander_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x24,
        .scl_speed_hz = TOUCH_I2C_FREQUENCY_HZ,
    };
    i2c_master_dev_handle_t expander;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &expander_config, &expander));
    const uint8_t expander_output_enable = 0x01;
    ESP_ERROR_CHECK(i2c_master_transmit(expander, &expander_output_enable, 1, -1));

    const i2c_device_config_t expander_output_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x38,
        .scl_speed_hz = TOUCH_I2C_FREQUENCY_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &expander_output_config, &expander));
    const uint8_t touch_reset_low = 0x2C;
    const uint8_t touch_reset_high = 0x2E;
    ESP_ERROR_CHECK(i2c_master_transmit(expander, &touch_reset_low, 1, -1));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_INT_GPIO, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(i2c_master_transmit(expander, &touch_reset_high, 1, -1));
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_ERROR_CHECK(gpio_reset_pin(TOUCH_INT_GPIO));

    const uint16_t addresses[] = {0x5D, 0x14};
    for (size_t index = 0; index < sizeof(addresses) / sizeof(addresses[0]); index++) {
        if (i2c_master_probe(bus, addresses[index], 100) == ESP_OK) {
            i2c_device_config_t device_config = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = addresses[index],
                .scl_speed_hz = TOUCH_I2C_FREQUENCY_HZ,
            };
            ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &device_config, &touch_device));
            uint8_t resolution[2];
            if (gt911_read(GT911_CONFIG_X_MAX_REG, resolution, sizeof(resolution)) == ESP_OK) {
                touch_max_x = resolution[0] | ((uint16_t)resolution[1] << 8);
            }
            if (gt911_read(GT911_CONFIG_Y_MAX_REG, resolution, sizeof(resolution)) == ESP_OK) {
                touch_max_y = resolution[0] | ((uint16_t)resolution[1] << 8);
            }
            ESP_LOGI(TAG, "GT911 found at I2C address 0x%02X", addresses[index]);
            ESP_LOGI(TAG, "GT911 coordinates: %ux%u mapped to %dx%d", touch_max_x, touch_max_y, LCD_WIDTH, LCD_HEIGHT);
            return;
        }
    }
    ESP_LOGE(TAG, "GT911 not found on GPIO %d (SDA) / GPIO %d (SCL)", TOUCH_SDA_GPIO, TOUCH_SCL_GPIO);
}

void app_main(void)
{
    init_display();
    init_touch();
    draw_screen(-1);
    ESP_LOGI(TAG, "Touch test ready");

    int last_button = -1;
    bool last_touch = false;
    while (true) {
        uint16_t x;
        uint16_t y;
        uint8_t button;
        bool touching = touch_read(&x, &y);
        int pressed_button = touching && point_in_button(x, y, &button) ? button : -1;
        if (touching && !last_touch) {
            ESP_LOGI(TAG, "Touch at x=%u y=%u", x, y);
        }
        if (pressed_button != last_button) {
            draw_screen(pressed_button);
            last_button = pressed_button;
            if (pressed_button >= 0) {
                ESP_LOGI(TAG, "Button %d pressed at x=%u y=%u", pressed_button + 1, x, y);
            }
        }
        last_touch = touching;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
