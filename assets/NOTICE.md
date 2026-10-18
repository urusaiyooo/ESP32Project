# 人物资源说明

character-left.png、character-right.png 及 firmware/src/clock_art.h 中嵌入的人物图来自项目使用者提供的素材。人物与原作权利归相应权利人；这些资源不自动适用项目原创代码的 MIT 许可证。对外使用或再发布时，应依据素材自身授权。

开发者可用自有或已获授权图片替换 assets 中的两张 PNG，再运行 `python tools/generate_clock_art.py` 生成固件资源；图片只在构建阶段转换，设备运行时的界面和动态文字仍由 S3 绘制。
