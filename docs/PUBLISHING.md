# 上传到 GitHub

当前文件已整理为干净源码目录，没有个人凭证、密钥、日志、数据库或旧备份。建议仓库名 `deskhub-s3`，简介：`ESP32-S3 桌面时钟、音乐屏、Codex 额度与 WorkBuddy 积分看板，Windows USB 一次安装自动运行。`

## 1. 创建仓库

登录 GitHub，点击右上角 `+` → `New repository`，填写名称，选择公开或私有，创建空仓库。准备直接提交本目录时，不必让 GitHub 另生成 README。

## 2. 提交源码

在解压后的 `GitHub-Source` 目录打开终端：

```powershell
git init -b main
git add .
git commit -m "Release DeskHub S3 v1.4.6"
git remote add origin https://github.com/urusaiyooo/ESP32Project.git
git push -u origin main
```

首次使用 Git 如提示身份缺失，按自己的 GitHub 昵称和提交邮箱设置 `git config user.name`、`git config user.email`。身份验证使用 GitHub 登录流程，不要把 token 写进仓库 URL。

也可用 GitHub 网页 `Add file` → `Upload files`，上传**解压后的源码目录内容**。不要只上传一个源码 ZIP，否则首页不会正常展示 README。

## 3. 创建 Release

仓库页面 `Releases` → `Draft a new release`，创建 tag `v1.4.6`，标题 `DeskHub S3 v1.4.6`；正文可复制根目录 `RELEASE_NOTES.md`。

上传本次提供的：

- `DeskHub-Firmware-1.4.6.zip`：新版固件、新刷机工具及完整视频。
- `DeskHub-Source-1.4.6.zip`：源码与开发说明。
- `SHA256SUMS.txt`：上述附件哈希。

大安装器不提交 Git 历史：GitHub 普通仓库会拦截超过 100MiB 的文件，使用 [Release 附件](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github) 保存大包。`.gitignore` 已排除 exe、zip、构建缓存和运行数据。

## 发布前核对

当前源码与固件对应 v1.4.6；Windows 后台发布版已取消作者 MAC 绑定；图片与第三方软件许可独立于 MIT 原创代码。新增截图时留意截图里的账号信息。


首次安装的 Windows 后台继续使用既有 DeskHub-Windows-1.4.4.zip；保留旧 Release 并从新 Release 提供其安装包链接。请勿用旧包的刷机工具覆盖新版视频固件。既有仓库更新时保留历史，使用普通提交和推送，不要强制推送或移动旧版本标签。

本项目现有仓库：https://github.com/urusaiyooo/ESP32Project 。已有仓库更新时先 git clone，再提交并正常推送；上方 git init 仅用于首次创建仓库。
