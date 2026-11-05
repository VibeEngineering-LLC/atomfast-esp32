#pragma once
/* Предустановка платы: ESP32-S3 DevKitC-1. Пины и параметры I2C датчика воздуха - только здесь (e10). */
#define AF_BOARD_NAME     "ESP32-S3-DevKitC-1"
#define AF_BOARD_I2C_PORT 0        /* I2C_NUM_0 */
#define AF_BOARD_I2C_SDA  8        /* GPIO8, как в прежней ESPHome-конфигурации */
#define AF_BOARD_I2C_SCL  9        /* GPIO9 */
#define AF_BOARD_I2C_HZ   50000    /* как в прежней ESPHome-конфигурации (frequency по умолчанию 50 кГц): запас по ёмкости проводов */
