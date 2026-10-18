# 开发与依赖

普通使用只需 Windows 使用包，不需要本页开发环境。以下仅用于修改源码、重建固件或安装器。

## 一键安装开发依赖

双击根目录 `Install-Dev-Dependencies.cmd`。脚本通过 Windows winget 安装缺少的 Python 3.12、.NET SDK 8，并在项目 `.venv` 安装固定版本的 pyserial、esptool、PyInstaller、PlatformIO、Pillow。需要联网；不会修改 S3，也不会自动推送 GitHub。若 winget 不可用，手动从 Python/.NET 官方网站安装后重试。

Git 仅在用命令行提交 GitHub 时需要；可以使用 GitHub 网页上传。Codex 账号接口由安装包自带；重建安装器时需准备 **Windows 原生 codex.exe**，npm 的 codex.cmd 不可直接替代。

## 编译固件

```powershell
.\Build-Firmware.cmd
```

配置：`firmware/platformio.ini`，固定 PlatformIO espressif32 7.0.0、Arduino-ESP32 2.0.17、LovyanGFX 1.2.7、ArduinoJson 7.4.2。首次构建自动下载工具链与库。新五段镜像写入 `prebuilt/firmware-s3`，并生成 manifest 与 SHA256。

## 编译 Windows 程序与刷机工具

```powershell
powershell -ExecutionPolicy Bypass -File tools/Build-Windows.ps1 -CodexExe 'C:\路径\codex.exe'
```

顺序：音乐采集器 → Python/账号接口依赖包 → 自包含安装器 → PyInstaller 自包含刷机工具。输出 `release/desktop-usb/DeskHubSetup.exe` 和 `release/flasher/DeskHubFlash.exe`。用户安装无需 .NET SDK；SDK 只用于构建。

Codex CLI 源码/发布包：[openai/codex](https://github.com/openai/codex)。此项目只使用登录和 `app-server --stdio` 账号读取接口，不携带开发者登录状态。

## 更换人物

替换 `assets/character-left.png`、`character-right.png`，运行 `.venv\Scripts\python.exe tools/generate_clock_art.py` 后重建固件。天气默认位置在 `dashboard_data.py`，固件地点标签在 `clock_app.cpp`；修改时一起更新。

## 测试

```powershell
.venv\Scripts\python.exe -m unittest discover -s desktop-usb/worker -p "test_*.py"
.venv\Scripts\python.exe -m unittest discover -s tools -p "test_*.py"
.venv\Scripts\python.exe tools/flash_firmware.py --check
```

`test_dashboard_device.py` 是手动真机脚本，不是 unittest TestCase，不会被自动执行。它会切换屏幕并查询真实账号/天气数据，只有显式运行时才操作开发板。完整 Windows 重启和其他型号屏幕需另做实机验收。

构建输出放在 `%TEMP%/DeskHub-S3-GitHub-build`，避免 ESP32 GCC 8 对中文项目路径的链接问题。若 Windows 用户名也包含非 ASCII 字符，可将 TEMP 指向英文目录后重试。此发布包的开发脚本针对 Windows，其他系统构建流程未验收。

重打包发布文件：准备好与源码同级的 DeskHub-Windows-1.4.4 使用目录（安装器、刷机工具、firmware、drivers 和 docs），运行 `python tools/package_public_release.py`。输出源码 ZIP、Windows ZIP 和附件 SHA256SUMS。脚本会排除构建缓存、凭证、日志、数据库和旧备份。

视频版分区、完整素材转换和打包见 docs/VIDEO.md。发布源码可运行 python tools/package_public_release.py；同时发布固件升级包时加 --firmware-dir 指定包含 DeskHubFlash.exe 和 firmware/ 的目录。文件名自动采用固件版本，拒绝源码与升级包版本不一致。
