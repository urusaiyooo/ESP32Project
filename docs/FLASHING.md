# 刷固件与触摸校准

## 推荐：Windows 使用包

完整解压 `DeskHub-Windows-1.4.4.zip`，插入 S3 数据线，双击 `Flash-Firmware.cmd`。工具包含 Python/esptool 运行环境，会：

1. 校验四段固件长度与 SHA256、芯片类型标记和固定写入地址。
2. 自动寻找串口；多个候选时显示列表供选择。
3. 临时停止本项目后台，读取 ESP32-S3 型号及 Flash 容量，确认 16MB 后才写入。
4. 烧录、校验并重启；恢复此前运行的后台。

工具不执行整片擦除，保留 NVS 中的 Wi-Fi 和触摸校准。首次刷机若板子尚未识别，可按住 BOOT、轻按 RST，再松开 BOOT 后重试。

## 地址与参数

| 文件 | 地址 |
|---|---|
| bootloader.bin | 0x00000000 |
| partitions.bin | 0x00008000 |
| boot_app0.bin | 0x0000E000 |
| firmware.bin | 0x00010000 |

目标 `esp32s3`，Flash 16MB；烧录 DIO / 80MHz，运行内存配置 qio_opi、8MB OPI PSRAM。LCD SPI 为 40MHz，与 Flash 时钟是不同参数。本包不适用于 ESP-WROOM-32。

## 命令行

```powershell
.\DeskHubFlash.exe --list-ports
.\DeskHubFlash.exe --check
.\DeskHubFlash.exe --probe --port COM5
.\DeskHubFlash.exe --port COM5
.\DeskHubFlash.exe --port COM5 --baud 115200
.\DeskHubFlash.exe --calibrate --port COM5
```

`--check` 不打开串口。`--probe` 只读取芯片/Flash 信息，但会重启开发板。COM5 只是示例，工具没有绑定这个端口。

源码用户先安装开发依赖，再运行 `python tools/flash_firmware.py`；默认读取 `prebuilt/firmware-s3`，可通过 `--firmware-dir` 指定新构建目录。

## 校准

双击 `Calibrate-Touch.cmd`，使用触控笔或指甲，依次按稳十字约半秒再松开。总共四角；完成后返回时钟并保存校准。也可按住 BOOT 上电进入校准，或在应用菜单点击“触摸校准”。

校准工具不会重刷固件。超时或取消后按 RST 退出，再重试；不要在等待十字时同时运行串口监视器。

## 故障处理

- 没有 COM 口：使用数据线；CH340 接口运行 `Install-CH340-Driver.cmd`，按 Windows 提示授权安装驱动。
- 串口被占用：关闭 Arduino/PlatformIO 串口监视器；工具只会停止本项目后台，不终止其他程序。
- 连接失败：按 BOOT+RST 进入下载模式，或使用 `--baud 115200`。
- 显示不亮：先检查 VCC、GND、BL 和屏幕供电；刷机不会替代电源连接。
- 有背光无画面：核对 CS10/RST8/DC9/MOSI11/SCK12 及屏幕芯片。
- 触摸偏移：重新校准；触摸完全无响应时核对 TCK16/TCS15/TDI17/TDO18 和共地。

官方参数参考：[Espressif esptool ESP32-S3 文档](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)。
