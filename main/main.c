#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_HOST SPI2_HOST
#define PIN_NUM_MOSI 11
#define PIN_NUM_MISO 13
#define PIN_NUM_SCLK 12
#define PIN_NUM_LCD_CS 10
#define PIN_NUM_LCD_DC 9
#define PIN_NUM_LCD_RST 8
#define PIN_NUM_TOUCH_CS 7
#define PIN_NUM_TOUCH_IRQ 6

#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#define TOUCH_RAW_X_MIN 200
#define TOUCH_RAW_X_MAX 3900
#define TOUCH_RAW_Y_MIN 200
#define TOUCH_RAW_Y_MAX 3900
#define TOUCH_SWAP_XY 0
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 1

#define BUTTON_COUNT 4
#define BUTTON_MARGIN 8
#define BUTTON_GAP 8
#define BUTTON_TOP 52
#define BUTTON_HEIGHT 76

static const char *TAG = "touch_test";
static spi_device_handle_t lcd_device;
static spi_device_handle_t touch_device;

static void lcd_command(uint8_t command)
{
	gpio_set_level(PIN_NUM_LCD_DC, 0);
	spi_transaction_t transaction = {.length = 8, .tx_buffer = &command};
	ESP_ERROR_CHECK(spi_device_transmit(lcd_device, &transaction));
}

static void lcd_data(const uint8_t *data, size_t length)
{
	gpio_set_level(PIN_NUM_LCD_DC, 1);
	spi_transaction_t transaction = {.length = length * 8, .tx_buffer = data};
	ESP_ERROR_CHECK(spi_device_transmit(lcd_device, &transaction));
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	uint8_t column[] = {x0 >> 8, x0, x1 >> 8, x1};
	uint8_t row[] = {y0 >> 8, y0, y1 >> 8, y1};
	lcd_command(0x2A); lcd_data(column, sizeof(column));
	lcd_command(0x2B); lcd_data(row, sizeof(row));
	lcd_command(0x2C);
}

static void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
	if (x >= LCD_WIDTH || y >= LCD_HEIGHT || width == 0 || height == 0) return;
	if (x + width > LCD_WIDTH) width = LCD_WIDTH - x;
	if (y + height > LCD_HEIGHT) height = LCD_HEIGHT - y;
	uint16_t line[64];
	uint16_t wire_color = (color >> 8) | (color << 8);
	for (size_t index = 0; index < 64; index++) line[index] = wire_color;
	lcd_set_window(x, y, x + width - 1, y + height - 1);
	for (uint32_t row = 0; row < height; row++) {
		uint16_t remaining = width;
		while (remaining > 0) {
			uint16_t count = remaining > 64 ? 64 : remaining;
			lcd_data((const uint8_t *)line, count * sizeof(uint16_t));
			remaining -= count;
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
	if (character >= '0' && character <= '9') return digits[character - '0'][column];
	if (character >= 'A' && character <= 'Z') return letters[character - 'A'][column];
	return 0;
}

static void lcd_text(uint16_t x, uint16_t y, const char *text, uint16_t color, uint8_t scale)
{
	for (size_t character_index = 0; text[character_index] != '\0'; character_index++) {
		for (uint8_t column = 0; column < 5; column++) {
			uint8_t bits = glyph_column(text[character_index], column);
			for (uint8_t row = 0; row < 7; row++) {
				if (bits & (1 << row)) lcd_fill_rect(x + column * scale, y + row * scale, scale, scale, color);
			}
		}
		x += 6 * scale;
	}
}

static void lcd_init(void)
{
	gpio_set_level(PIN_NUM_LCD_RST, 0);
	vTaskDelay(pdMS_TO_TICKS(20));
	gpio_set_level(PIN_NUM_LCD_RST, 1);
	vTaskDelay(pdMS_TO_TICKS(120));
	lcd_command(0x01); vTaskDelay(pdMS_TO_TICKS(120));
	uint8_t madctl = 0x28, pixel_format = 0x55;
	lcd_command(0x36); lcd_data(&madctl, 1);
	lcd_command(0x3A); lcd_data(&pixel_format, 1);
	lcd_command(0x11); vTaskDelay(pdMS_TO_TICKS(120));
	lcd_command(0x29);
}

static bool touch_read(uint16_t *x, uint16_t *y)
{
	uint8_t commands[] = {0xD0, 0x90};
	uint16_t samples[2] = {0};
	for (int index = 0; index < 2; index++) {
		uint8_t transmit[3] = {commands[index], 0, 0};
		uint8_t response[3] = {0};
		spi_transaction_t transaction = {.length = 24, .tx_buffer = transmit, .rx_buffer = response};
		ESP_ERROR_CHECK(spi_device_transmit(touch_device, &transaction));
		samples[index] = ((response[1] << 8) | response[2]) >> 3;
	}
	if (gpio_get_level(PIN_NUM_TOUCH_IRQ) != 0) return false;
	uint16_t raw_x = samples[0], raw_y = samples[1];
	if (TOUCH_SWAP_XY) { uint16_t swap = raw_x; raw_x = raw_y; raw_y = swap; }
	if (raw_x < TOUCH_RAW_X_MIN || raw_x > TOUCH_RAW_X_MAX || raw_y < TOUCH_RAW_Y_MIN || raw_y > TOUCH_RAW_Y_MAX) return false;
	*x = (uint32_t)(raw_x - TOUCH_RAW_X_MIN) * LCD_WIDTH / (TOUCH_RAW_X_MAX - TOUCH_RAW_X_MIN);
	*y = (uint32_t)(raw_y - TOUCH_RAW_Y_MIN) * LCD_HEIGHT / (TOUCH_RAW_Y_MAX - TOUCH_RAW_Y_MIN);
	if (TOUCH_INVERT_X) *x = LCD_WIDTH - 1 - *x;
	if (TOUCH_INVERT_Y) *y = LCD_HEIGHT - 1 - *y;
	return true;
}

static void draw_screen(int pressed_button)
{
	const uint16_t navy = 0x18E3, white = 0xFFFF, blue = 0x435F, green = 0x2E86;
	lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, navy);
	lcd_text(18, 12, "TOUCH TEST", white, 2);
	for (int button = 0; button < BUTTON_COUNT; button++) {
		uint16_t x = BUTTON_MARGIN + (button % 2) * (LCD_WIDTH / 2);
		uint16_t y = BUTTON_TOP + (button / 2) * (BUTTON_HEIGHT + BUTTON_GAP);
		uint16_t color = button == pressed_button ? green : blue;
		uint16_t width = LCD_WIDTH / 2 - BUTTON_MARGIN - BUTTON_GAP / 2;
		lcd_fill_rect(x, y, width, BUTTON_HEIGHT, color);
		const char *label = button == 0 ? "TOP LEFT" : button == 1 ? "TOP RIGHT" : button == 2 ? "BOTTOM LEFT" : "BOTTOM RIGHT";
		uint16_t text_width = strlen(label) * 6 * 2;
		lcd_text(x + (width - text_width) / 2, y + 28, label, white, 2);
	}
	if (pressed_button >= 0) ESP_LOGI(TAG, "Button %d pressed", pressed_button + 1);
}

void app_main(void)
{
	gpio_config_t output = {.pin_bit_mask = (1ULL << PIN_NUM_LCD_DC) | (1ULL << PIN_NUM_LCD_RST), .mode = GPIO_MODE_OUTPUT};
	ESP_ERROR_CHECK(gpio_config(&output));
	gpio_config_t touch_irq = {.pin_bit_mask = 1ULL << PIN_NUM_TOUCH_IRQ, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
	ESP_ERROR_CHECK(gpio_config(&touch_irq));
	spi_bus_config_t bus_config = {.mosi_io_num = PIN_NUM_MOSI, .miso_io_num = PIN_NUM_MISO, .sclk_io_num = PIN_NUM_SCLK, .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096};
	ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));
	spi_device_interface_config_t lcd_config = {.clock_speed_hz = 40 * 1000 * 1000, .mode = 0, .spics_io_num = PIN_NUM_LCD_CS, .queue_size = 1};
	spi_device_interface_config_t touch_config = {.clock_speed_hz = 2 * 1000 * 1000, .mode = 0, .spics_io_num = PIN_NUM_TOUCH_CS, .queue_size = 1};
	ESP_ERROR_CHECK(spi_bus_add_device(LCD_HOST, &lcd_config, &lcd_device));
	ESP_ERROR_CHECK(spi_bus_add_device(LCD_HOST, &touch_config, &touch_device));
	lcd_init();
	draw_screen(-1);
	ESP_LOGI(TAG, "Touch test ready. Adjust pins and calibration constants at the top of main.c if needed.");
	int last_button = -1;
	while (true) {
		uint16_t x, y;
		int button = -1;
		if (touch_read(&x, &y) && y >= BUTTON_TOP && y < BUTTON_TOP + 2 * BUTTON_HEIGHT + BUTTON_GAP) {
			int column = x >= LCD_WIDTH / 2;
			int row = y >= BUTTON_TOP + BUTTON_HEIGHT + BUTTON_GAP;
			button = row * 2 + column;
		}
		if (button != last_button) {
			draw_screen(button);
			last_button = button;
		}
		vTaskDelay(pdMS_TO_TICKS(50));
	}
}
