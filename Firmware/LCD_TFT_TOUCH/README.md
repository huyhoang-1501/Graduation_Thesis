
# Set up thư viện
- Tải thư viện TFT_ESPI
- Vào folder User_Setups
---
```
Thêm file header sau: Setup_ST7796_ESP32_4inch_SPI_I2C_CTP.h
#define ST7796_DRIVER

// Panel gốc: 320 (ngang) x 480 (dọc)

#define TFT_WIDTH  480
#define TFT_HEIGHT 320

#define TFT_MISO  19
#define TFT_MOSI  23
#define TFT_SCLK  18

#define TFT_CS    5
#define TFT_DC    2
#define TFT_RST   4

#define TFT_BL    15
#define TFT_BACKLIGHT_ON HIGH

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
```
---
- Xong include vào file User_Setup_Select.header
```
//#include <User_Setup.h>           // Default setup is root library folder
//#include <User_Setup.h>           // Default setup is root library folder
#include <Setup_ST7796_ESP32_4inch_SPI_I2C_CTP.h>
```
---