# Disk Cleanup

**System**: WIN x86

## Usage
1. Download the release from the [Releases](../../releases) page.
2. Click **"Scan"** to view the total cleanable size.
3. Check the items you wish to clean.
4. Click **"Clean"** and confirm to perform the deletion.

## Safety Notes
- The following items are **checked by default**: common caches and temporary directories, including browsers, WeChat, QQ, WPS, Microsoft 365/Office, Teams, DingTalk/Feishu/WeCom (Enterprise WeChat), cloud storage clients, download tools, multimedia software, graphics driver caches, and caches from 360/Huorong/archiver/input method software.
- The following items are **NOT checked by default** and require manual selection:  
  *"Files in Downloads older than 30 days"*, *"Recycle Bin"*, *"Game platform caches"*, *"Adobe/CapCut/OBS creative caches"*, *"Development tool caches"*, and *"Windows Update download caches"*.
- The program **does not** clean protected paths themselves, such as `C:\`, `C:\Windows`, `Program Files`, Desktop, Documents, Pictures, Music, Videos, etc.
- For WeChat/QQ, only cache and temporary folders are cleaned; chat history, received files, and image/video folders are **not** touched.
- Office unsaved file directories are **not** cleaned; Office caches are cleaned only for files older than **24 hours** by default.
- It is recommended to close WeChat, QQ, WPS, Office, browsers, and other related programs before cleaning; otherwise, some cache files may be locked and cannot be deleted.
- Cleaning operations are **irreversible**. Please double‑check your selections before proceeding.

## About
This tool is specially designed for computer beginners.

**Developed by**: MEMZGBL  
**GitHub**: [https://github.com/memzgbl](https://github.com/memzgbl)  
**Bilibili**: [https://space.bilibili.com/326402501](https://space.bilibili.com/326402501)

## Rebuild
Open PowerShell in this directory and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1

# 磁盘清理

**系统**：WIN x86

## 使用方法
1. 从 [Release](../../releases) 页面下载发行版。
2. 点击 **“扫描”** 查看可清理的总大小。
3. 勾选需要清理的项目。
4. 点击 **“清理”**，确认后执行删除操作。

## 安全说明
- 默认勾选的项目包括：常见缓存和临时目录，涵盖浏览器、微信、QQ、WPS、Microsoft 365/Office、Teams、钉钉/飞书/企业微信、网盘、下载工具、影音软件、显卡驱动缓存，以及 360/火绒/压缩/输入法等软件的缓存。
- 以下项目默认**不勾选**，需要您手动选择：  
  *“下载目录中 30 天前的文件”*、*“回收站”*、*“游戏平台缓存”*、*“Adobe/剪映/OBS 创作缓存”*、*“开发工具缓存”* 和 *“Windows 更新下载缓存”*。
- 程序**不会**清理受保护的系统路径本身，例如 `C:\`、`C:\Windows`、`Program Files`、桌面、文档、图片、音乐、视频等。
- 微信/QQ 仅清理缓存和临时文件夹，**不会**删除聊天记录、接收的文件、图片或视频目录。
- Office 未保存的文件目录**不会被清理**；Office 缓存默认仅清理 **24 小时前**的文件。
- 建议在清理前关闭微信、QQ、WPS、Office、浏览器等相关程序，否则部分缓存可能因被占用而无法删除。
- 清理操作**不可撤销**，请务必在使用前仔细确认所勾选的项目。

## 关于
本工具专为计算机初学者设计。

**制作者**：MEMZGBL  
**GitHub**：[https://github.com/memzgbl](https://github.com/memzgbl)  
**哔哩哔哩**：[https://space.bilibili.com/326402501](https://space.bilibili.com/326402501)

## 重新编译
在此目录下打开 PowerShell，执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
