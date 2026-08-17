idf_component_register(SRCS "touch_lcd_43b_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_driver_i2c esp_lcd)
