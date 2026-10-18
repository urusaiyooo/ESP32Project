# DeskHub S3 v1.4.4

ESP32-S3 N16R8 + ILI9488 3.5 英寸触摸屏的白底桌面时钟、音乐与账号额度看板。

## 下载

普通使用下载 **DeskHub-Windows-1.4.4.zip**，完整解压后：

1. 必要时运行 `Install-CH340-Driver.cmd`。
2. 插入 S3 数据线，运行 `Flash-Firmware.cmd`。
3. 初次运行 `Calibrate-Touch.cmd`，依次校准四角。
4. 运行 `Install.cmd`，开启 Windows 登录自动运行。
5. 登录本机 Codex，播放 QQ 音乐，按需运行 WorkBuddy 本地面板。

无需另装 Python、.NET SDK 或 PlatformIO。源码/开发者下载 **DeskHub-Source-1.4.4.zip**。用 SHA256SUMS.txt 校验下载文件。

## 功能

时分秒与日期星期、歌曲名滚动、今日/明日天气图标、温湿度和 US AQI、Codex 双窗口剩余与重置时间、WorkBuddy 剩余/总积分；Codex 运行蓝边、完成绿边。字体、排版、歌词时间轴、滚动和封面解码在 S3。

## 修复

触摸不再依赖 PEN 中断电平；保存四角校准；扩大应用按钮点击范围。额度卡居中、重置时间分栏、天气等宽布局、应用文字背景透明。公开版后台自动识别设备，不绑定作者 MAC。

仅针对已验证的 N16R8 与 ILI9488 接线；天气默认北京市大兴区；账号与 WorkBuddy 配置不随包提供。详细接线及使用方法见源码 README 或使用包 docs。
