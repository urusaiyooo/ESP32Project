# DeskHub S3 — 桌面时钟与音乐屏

基于 **ESP32-S3 N16R8 + ILI9488 3.5 英寸触摸屏** 的桌面信息终端。固件 **v1.4.7**，支持 Windows 10/11 x64。

- 点击左侧人物播放完整视频彩蛋，点击画面退出，播完返回时钟。详见 [视频说明](docs/VIDEO.md)。
- 开机显示白底时钟：24 小时制时分秒、年月日、星期与人物背景。
- 固定“正在播放”标签，当前歌曲名在设备上滚动。
- 今日/明日天气图标、当前温湿度和空气质量，默认北京市大兴区。
- Codex 五小时/每周**剩余额度**及北京时间重置日期、时分秒。
- WorkBuddy 本地面板账号池**剩余积分/总积分**。
- Codex 运行蓝边、完成绿边，失联/无状态灰边；不显示或传输任务回答和历史记录。
- 白底音乐页：封面、播放进度、上一首/播放暂停/下一首、任务状态，以及平滑滚动的原文/译文歌词。
- 本地持久缓存歌曲资料、原始封面与歌词，重复播放优先读取缓存。
- Windows 一次安装，之后登录自动启动、USB 自动连接。

电脑负责采集数据；S3 负责字体、排版、图标、滚动、歌词时间轴与封面解码。不是脱离电脑即可读取电脑账号和播放器的数据：音乐/Codex/WorkBuddy 数据仍由当前电脑提供。

## 快速开始

1. 按 [接线说明](docs/HARDWARE.md) 连接屏幕，确认供电与共地。
2. 下载并完整解压 [DeskHub-Windows-1.4.7.zip](https://github.com/urusaiyooo/ESP32Project/releases/download/v1.4.7/DeskHub-Windows-1.4.7.zip)；不要在压缩包内运行。
3. 若设备管理器没有 CH340 串口，先运行 `Install-CH340-Driver.cmd`。使用原生 USB CDC 通常不需要 CH340 驱动。
4. 插入 S3 USB **数据线**，运行 `Flash-Firmware.cmd`；多个串口时选择开发板端口。
5. 首次使用运行 `Calibrate-Touch.cmd`，依次按稳四角十字并松开；已有校准会保留。
6. 运行 `Install.cmd`，点击“安装并开机自动运行”。本版音乐控制与缓存需要配套升级至 Windows 后台 1.4.7。
7. 保持 QQ 音乐播放即可同步歌曲；Codex 未登录时在安装器点击“登录 Codex”；WorkBuddy 积分需本机面板运行。

**普通使用无需另外安装 Python、.NET SDK、PlatformIO、Node.js。** 安装包和刷机工具已带运行环境；CH340 离线驱动随 Windows 使用包提供。开发者依赖另见 [开发说明](docs/DEVELOPMENT.md)。

## 文档

| 文档 | 内容 |
|---|---|
| [使用说明](docs/USAGE.md) | 安装、自启动、音乐、额度、积分、天气口径 |
| [接线说明](docs/HARDWARE.md) | GPIO、供电、USB 接口、触摸轮询 |
| [刷机与校准](docs/FLASHING.md) | 一键刷机、命令行、校准、失败处理 |
| [开发说明](docs/DEVELOPMENT.md) | 依赖安装、编译固件、生成安装包、测试 |
| [音乐页与缓存](docs/MUSIC.md) | 播放控制、平滑歌词、本地缓存与升级 |
| [视频彩蛋](docs/VIDEO.md) | 点击入口、完整视频转换、分区布局与烧录 |
| [v1.4.7 验证结果](docs/VALIDATION-1.4.7.md) | 真实 QQ 音乐控制、缓存重启和歌词过渡检查 |
| [v1.4.6 验证结果](docs/VALIDATION-1.4.6.md) | 完整 110 秒播放、帧率及菜单回归验证 |
| [GitHub 发布指南](docs/PUBLISHING.md) | 建仓库、提交源码、上传 Release 附件 |
| [版本说明](CHANGELOG.md) | 当前版本变化及历史修复 |

## 目录

```text
firmware/                  S3 固件源码与 PlatformIO 配置
prebuilt/firmware-s3/       已验证的五段镜像（含完整视频）及 SHA256 manifest
desktop-usb/worker/         统一 USB 数据后台
desktop-usb/MediaCollector/ Windows 音乐信息采集器
desktop-usb/Installer/      每用户安装器与登录自启
music-companion/            音乐采集器依赖的少量原始 C# 模块
tools/                     刷机、构建、依赖安装和测试工具
assets/                    人物资源；许可独立于代码
third_party/               许可证、版本信息及 esptool 对应源码
```

公开版会通过固件握手自动识别 DeskHub，**不绑定作者开发板的 MAC 或 COM5**。多设备场景可设置用户环境变量 `DESKHUB_DEVICE_MAC` 指定目标。

## 数据与隐私

安装包不包含账号凭证、WorkBuddy 配置密钥、Wi-Fi 密码、个人运行日志或任务数据库。Codex 额度从本机登录账号只读获取；Codex 状态只处理生命周期事件，不保存或转发回答。WorkBuddy 只调用本机 `overview` GET 接口，不签到、不领取、不修改账号。天气/歌词查询需要联网；无法获取时显示待更新，不生成假数值。

天气来自 [Open-Meteo](https://open-meteo.com/en/docs) 的持续更新预测资料；空气数值使用 [US AQI 美标](https://open-meteo.com/en/docs/air-quality-api)，并非中国国标 AQI 或本机传感器。屏幕显示 `US` 标识。

## 许可

项目原创代码采用 MIT，音乐采集原模块许可见 `LICENSE-MusicDisplay`。第三方库、工具和运行时保留原许可，见 [第三方说明](THIRD_PARTY_NOTICES.md)。人物图片不自动适用代码许可证，见 [资源说明](assets/NOTICE.md)。
