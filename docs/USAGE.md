# Windows 安装与日常使用

## 安装

Windows 10/11 x64 使用 `DeskHub-Windows-1.4.7.zip`。完整解压后双击 `Install.cmd`，点击“安装并开机自动运行”。安装在 `%LOCALAPPDATA%\DeskHub`，一般不需要管理员权限。只有安装 USB 驱动时会请求 Windows 管理员授权。

软件自带 Python、.NET 运行时、音乐采集器及 Codex 账号接口程序。以后登录 Windows 后自动后台运行，插入 S3 会自动连接，拔插后自动重连。可在 Windows 应用列表卸载 `DeskHub USB Companion`。

## 时钟与菜单

默认北京时间（UTC+8），24 小时制，精确到秒。点击时间数字正中央的透明触摸区域打开菜单，可进入音乐屏、时钟、Wi-Fi 设置和触摸校准；短按 BOOT 返回菜单。S3 当前无电池，断电后需由后台或网络重新校时。

## 视频彩蛋

点击时钟左侧人物播放完整 110 秒视频，点击画面退出，结束后自动返回时钟。素材存储在 S3 Flash；使用本版包含的视频分区固件包。详见 [视频说明](VIDEO.md)。

## 音乐

QQ 音乐需在 Windows 中正常播放并向系统媒体会话提供信息。时钟只显示歌曲名；进入音乐屏显示封面、播放进度、触摸播放控制、五小时剩余额度、任务状态及平滑歌词。相同时间戳的原文/译文合并显示。歌曲、封面和歌词持久缓存在本机，重复播放优先读取。详见 [音乐页与缓存](MUSIC.md)。元数据、歌词和原始图片经 USB 传输；中文/日文字体、滚动、歌词时间轴和图像缩放由 S3 计算。歌词需互联网来源，版权受源站管理；有的歌曲可能没有可用歌词。

封面限制：压缩原图 512KiB；PNG/基线 JPEG/渐进 JPEG，其中渐进 JPEG 最大 1024×1024 像素。超限使用占位图，避免耗尽设备内存。

## Codex

额度来自当前 Windows 用户登录账号；首次未登录时运行安装器并点击“登录 Codex”，按浏览器流程完成授权。不会将原电脑登录凭证复制给新电脑。

五小时/每周显示剩余百分比和北京时间的完整重置日期、时分秒。后台约每分钟只读刷新。失联或过期会灰显并标记待更新；没有数据时显示 `--`。

边框只处理本地 Codex 会话开始、结束事件：运行蓝色，全部完成绿色，失联/无状态灰色。需要本机 Codex 产生日志；远程或没有落本地日志的会话不会自动被检测。回答和历史功能已移除。

## WorkBuddy 账号池

运行本机 [workbuddy2api-panel](https://github.com/linguo2625469/workbuddy2api-panel) 面板（默认 `http://127.0.0.1:7863`）。后台自动寻找 Desktop/wb2api-panel*/config.json，并每 30 秒只读查询 `/panel/api/overview`。

积分是全部账号的 `credits` 合计 / `credits_total` 合计，包含暂停账号；不是今天消费额，也不是 Codex 额度。凭证仅留在本机；不调用签到、任务领取或配置修改接口。

自定义配置位置：

```powershell
[Environment]::SetEnvironmentVariable('DESKHUB_WORKBUDDY_CONFIG','D:\你的面板\config.json','User')
```

修改后重新登录 Windows，或重新运行安装器启动后台。面板程序和账号数据需要自行安装/配置，本项目不附带其私人账号池。

## 天气

默认北京市大兴区。每 10 分钟获取 Open-Meteo 当前温湿度、天气代码、次日预测及当前空气质量估算。US AQI 会标记 `US`；网格预测不是本机传感器或中国监测站实测。当前版本修改地区需要修改 `desktop-usb/worker/dashboard_data.py` 的坐标、地区文案，并同步固件地点标签后重建。

## 多块设备与诊断

默认自动识别 DeskHub 1.4.x 固件及合法 MAC，不绑定作者设备。多设备可设置 `DESKHUB_DEVICE_MAC` 用户环境变量指定目标。

后台日志：`%LOCALAPPDATA%\DeskHub\runtime\worker.log`。需要报错时只分享相关错误行，不要提交 auth.json、面板 config.json 或账号数据库。原生 USB、CH340/CH343 和 CP210x 常见串口 ID 已加入识别；其他桥接器可能需适配。
