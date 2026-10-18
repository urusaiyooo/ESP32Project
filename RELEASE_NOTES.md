# DeskHub v1.4.6 — 人物视频彩蛋

时钟页点击左侧人物可播放完整 110 秒视频，点击播放画面退出，播放结束自动返回时钟。时间中央的透明菜单入口保持原有功能。视频存储及播放均由 S3 完成。

本版使用 9.625 MiB 原始视频分区，取消备用固件及 SPIFFS。主固件和 NVS 地址不变，保留 Wi-Fi 配置和触摸校准，继续使用 USB 更新，不支持双分区 OTA。

下载 `DeskHub-Firmware-1.4.6.zip`，完整解压后运行 `Flash-Firmware.cmd`，会同时写入分区表、固件和视频。必须使用本版附带的新刷机程序。Windows 1.4.4 采集后台保持兼容，无需重新安装。

源码见 `DeskHub-Source-1.4.6.zip`。替换视频及打包说明见 `docs/VIDEO.md`。素材不纳入代码 MIT 授权。

首次安装电脑后台：[Windows 1.4.4 完整安装包](https://github.com/urusaiyooo/ESP32Project/releases/download/ESP32/DeskHub-Windows-1.4.4.zip)。已经安装则无需重装。
