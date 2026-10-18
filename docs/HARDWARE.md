# 硬件与接线

已验证：ESP32-S3-WROOM-1 N16R8（16 MB Quad Flash、8 MB OPI PSRAM），ILI9488 480×320 横屏，ADS7843/XPT2046 电阻触摸。显示 40 MHz；触摸独立 SPI 总线 1 MHz。其他驱动芯片或尺寸的屏幕需适配，不保证直接通用。

| 屏幕序号/标识 | 连接 |
|---|---|
| 1 VCC/VDD | 模块允许的电源输入，通常 5V；以实物说明为准 |
| 2 GND | ESP32-S3 GND，独立屏幕电源也必须共地 |
| 3 CS | GPIO10 |
| 4 RST/RESET | GPIO8，不能接开发板 RST |
| 5 DC/D/C | GPIO9 |
| 6 SDI/MOSI | GPIO11 |
| 7 SCK | GPIO12 |
| 8 LED/BL | 本模块为 3V3 逻辑高常亮 |
| 9 SDO/MISO | 不接，LCD 使用只写模式 |
| 10 TCK/T_CLK | GPIO16 |
| 11 TCS/T_CS | GPIO15 |
| 12 TDI/T_DIN | GPIO17 |
| 13 TDO/T_DO | GPIO18 |
| 14 PEN/T_IRQ | 可接 GPIO7；轮询版不依赖它 |

GPIO 数字不是排针序号。所有信号使用 3.3V 逻辑，5V 只用于允许 5V 输入的模块电源端。

部分双 Type-C 开发板的 5Vin 引脚在未连通 IN-OUT 焊盘时不向外供电。板上电源灯亮不代表屏幕已获得电源。可使用独立合规电源给屏幕供电并共地；本项目已验证保留原供电方式，不要求焊接电源焊盘。

建议日常先使用带 CH340 的 USB 串口接口；原生 USB CDC 接口也受后台识别。必须使用数据线。主控的 RGB 灯不作为本项目状态灯，蓝/绿状态显示在 LCD 四周。

触摸采用压力/坐标轮询，避免 PEN 信号一直高时跳过采样。新板初次刷机需要四角校准；校准保存在 NVS，普通升级不会清除。
