# 人物视频彩蛋（v1.4.6）

时钟页点击左侧人物（x=6..149、y=33..138）播放视频。视频以原生 320×180 尺寸居中显示，四周黑边；点击屏幕任意位置退出，播完自动返回时钟。原生尺寸降低 SPI 传输和缩放开销。时间中央继续打开应用菜单。播放在 S3 完成，无需电脑流式供给，也不需要 SD 卡或额外接线。当前硬件不输出声音。

视频素材为用户提供的「新宿决战」雨爱版，完整 110 秒，320×180、12 fps，共 1320 帧。显示按原时间轴调度，遇到来不及显示的帧会跳帧，不延长视频时长。实际显示帧率以设备运行结果为准。

## 分区

`firmware/partitions_video.csv` 保留 NVS 0x9000/0x5000、OTA 选择区 0xe000/0x2000、主固件 0x10000/0x640000 和崩溃记录区 0xff0000/0x10000。取消备用固件和 SPIFFS，将 0x650000/0x9a0000（9.625 MiB）用于原始视频。

此版本使用 USB 更新，不支持双分区 OTA。升级不执行 erase-all，因此原触摸校准及 Wi-Fi 设置可保留。旧版只认识四个镜像的刷机程序不能刷此版，必须使用随 v1.4.6 提供的新 DeskHubFlash.exe。

## 转换与打包

在装有 FFmpeg 的开发电脑运行（不截取时长）：

```powershell
ffmpeg -i "input.mp4" -map 0:v:0 -an -vf "fps=12,scale=320:180:force_original_aspect_ratio=decrease,pad=320:180:(ow-iw)/2:(oh-ih)/2:color=black" -c:v mjpeg -q:v 6 -pix_fmt yuvj420p -f mjpeg "video.mjpeg"
python tools/pack_video.py "video.mjpeg" prebuilt/firmware-s3/video.bin
```

转换仅在制作素材时进行，正常播放不需要 FFmpeg。原始 MJPEG 不含帧率；本包固定 12 fps。打包器生成 64 字节版本头、逐帧偏移/长度索引、4096 对齐的 JPEG 数据；含索引和视频 CRC，整镜像 SHA256 记录在 manifest.json。视频超出分区时拒绝生成。

然后编译固件、运行 `python tools/package_s3.py` 更新五个镜像的 manifest，并重新发布固件包。视频只替换时仍须重新打包，禁止混用旧校验信息。

## 烧录

完整解压升级包，运行 Check-Firmware.cmd 校验，再运行 Flash-Firmware.cmd。新工具先验证镜像 SHA256、S3 芯片、物理 16MB Flash、视频格式及固定分区布局，再写入五个镜像。原有 Windows 1.4.4 采集后台继续兼容，不用重装。

## 诊断

`HUB_VIDEO_STATUS` 返回 ready、playing、result、1320 帧中的进度、实际显示/跳过帧数、渲染耗时和剩余 PSRAM。视频缺失、索引损坏、内存不足或解码失败时退出播放器，恢复时钟。

视频和人物属于提供的素材，不因附带于源码/固件而自动取得项目 MIT 授权；替换素材后可自行打包。
