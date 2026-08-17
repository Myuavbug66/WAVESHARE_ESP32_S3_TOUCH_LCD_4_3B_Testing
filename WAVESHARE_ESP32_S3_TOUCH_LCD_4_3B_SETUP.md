# Waveshare ESP32-S3-Touch-LCD-4.3B Setup Reference

Use this reference when starting an ESP-IDF project for the Waveshare ESP32-S3-Touch-LCD-4.3B.

## Hardware

| Function | Setting |
| --- | --- |
| ESP-IDF target | `esp32s3` |
| Flash | 16 MB, DIO, 80 MHz |
| PSRAM | 8 MB octal, 80 MHz |
| Display | 800 x 480 RGB565, 16-bit parallel RGB |
| Touch controller | GT911 at I2C address `0x5D` |
| I2C SDA / SCL | GPIO8 / GPIO9 |
| Touch interrupt | GPIO4, active low |

## Required `sdkconfig.defaults`

```ini
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_TYPE_AUTO=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_CAPS_ALLOC=y
```

The RGB panel framebuffer requires PSRAM. Configure the RGB panel with `.flags.fb_in_psram = true`.

## RGB Pins

```c
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
```

Use these 800 x 480 timings:

```c
.timings = {
    .pclk_hz = 16000000,
    .h_res = 800,
    .v_res = 480,
    .hsync_pulse_width = 4,
    .hsync_back_porch = 8,
    .hsync_front_porch = 8,
    .vsync_pulse_width = 4,
    .vsync_back_porch = 8,
    .vsync_front_porch = 8,
    .flags.pclk_active_neg = true,
},
.data_width = 16,
.flags.fb_in_psram = true,
```

## GT911 Touch Setup

The GT911 shares I2C with the CH422G I/O expander. Before probing the GT911, reset it through the expander and use GPIO4 to select address `0x5D`.

```c
#define TOUCH_SDA_GPIO 8
#define TOUCH_SCL_GPIO 9
#define TOUCH_INT_GPIO 4
#define TOUCH_I2C_FREQUENCY_HZ 400000

static void reset_gt911(i2c_master_bus_handle_t bus)
{
    i2c_master_dev_handle_t expander;
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x24,
        .scl_speed_hz = TOUCH_I2C_FREQUENCY_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &config, &expander));

    const uint8_t output_enable = 0x01;
    ESP_ERROR_CHECK(i2c_master_transmit(expander, &output_enable, 1, -1));

    config.device_address = 0x38;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &config, &expander));

    const uint8_t reset_low = 0x2C;
    const uint8_t reset_high = 0x2E;
    ESP_ERROR_CHECK(i2c_master_transmit(expander, &reset_low, 1, -1));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_INT_GPIO, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(i2c_master_transmit(expander, &reset_high, 1, -1));
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_ERROR_CHECK(gpio_reset_pin(TOUCH_INT_GPIO));
}
```

Configure GPIO4 as an output low before calling `reset_gt911()`:

```c
gpio_config_t int_output_config = {
    .pin_bit_mask = 1ULL << TOUCH_INT_GPIO,
    .mode = GPIO_MODE_OUTPUT,
};
ESP_ERROR_CHECK(gpio_config(&int_output_config));
ESP_ERROR_CHECK(gpio_set_level(TOUCH_INT_GPIO, 0));
```

Do not use GPIO4 as a display backlight output. It is the GT911 interrupt/address-selection line.

## GT911 Register Access

GT911 register addresses are transmitted most-significant byte first.

```c
static esp_err_t gt911_read(i2c_master_dev_handle_t device, uint16_t reg,
                            uint8_t *data, size_t length)
{
    const uint8_t address[] = {reg >> 8, reg & 0xFF};
    return i2c_master_transmit_receive(device, address, sizeof(address),
                                       data, length, -1);
}

static esp_err_t gt911_write(i2c_master_dev_handle_t device, uint16_t reg,
                             uint8_t value)
{
    const uint8_t data[] = {reg >> 8, reg & 0xFF, value};
    return i2c_master_transmit(device, data, sizeof(data), -1);
}
```

Use `0x814E` for touch status and `0x814F` for the first touch point. After reading a completed touch report, write `0` back to `0x814E` to acknowledge it.

## ESP-IDF Dependencies

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2c esp_lcd
)
```

## Verification Log

A correct startup reports all of the following:

```text
boot.esp32s3: SPI Flash Size : 16MB
esp_psram: Found 8MB PSRAM device
esp_psram: SPI SRAM memory test OK
touch_lcd_43b: GT911 found at I2C address 0x5D
touch_lcd_43b: Touch test ready
```
