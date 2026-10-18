# 第三方组件与资源

原创 DeskHub 代码使用 MIT；下列组件保留上游许可，不能将整个使用包误认为仅包含 MIT 内容。

| 组件 | 用途/许可与来源 |
|---|---|
| MusicDisplay 原模块 | MIT，见 LICENSE-MusicDisplay |
| LovyanGFX 1.2.7 | 图形与触摸，FreeBSD/BSD 类许可；由 PlatformIO 下载上游版本 |
| Arduino-ESP32 2.0.17 | 芯片运行框架，LGPL 等上游许可 |
| ArduinoJson 7.4.2 | MIT，JSON 协议 |
| stb_image v2.30 | MIT/公有领域双许可，原许可保留在 firmware/third_party/stb_image.h |
| Python 3.12 | PSF 及其第三方条款；见 third_party/licenses/Python-LICENSE.txt |
| .NET 8 运行时 | MIT 与第三方条款；见 dotnet-LICENSE/THIRD-PARTY-NOTICES |
| Codex CLI | Apache-2.0；见 codex-LICENSE/NOTICE；https://github.com/openai/codex |
| esptool 5.2.0 | GPL-2.0-or-later；对应源码包在 third_party/sources，刷机包装脚本源码在 tools/flash_firmware.py |
| PyInstaller | GPL 带引导程序例外；许可证与例外见 third_party/licenses/pyinstaller |
| pyserial、cryptography 等 | 许可文件与实际版本清单见 third_party/licenses |
| CH340 驱动 | 沁恒官方签名驱动，按供应商许可提供；使用包内保留原 INF/CAT/SYS/DLL |
| 人物图片 | 独立权利归属，见 assets/NOTICE.md |

上游源码与资料：
- https://github.com/lovyan03/LovyanGFX
- https://github.com/espressif/arduino-esp32
- https://github.com/bblanchon/ArduinoJson
- https://github.com/espressif/esptool
- https://www.python.org/
- https://github.com/dotnet/runtime
- https://www.wch.cn/downloads/CH341SER_EXE.html

Windows 使用包包含本刷机包装脚本、依赖版本与 esptool 对应源码，便于审阅和重建；项目仓库不包含任何使用者账号凭证。
