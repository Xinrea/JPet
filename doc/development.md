# 开发与发布

本文补充 [README](../README.md) 中的快速构建步骤，供开发与维护时查阅。

## 依赖与平台

- 客户端：C++17、CMake 3.27+；Windows x64 使用 Visual Studio 2022，macOS ARM64 使用 Xcode Command Line Tools 和 Ninja。
- 设置面板：Node.js 22+、npm、Svelte + Vite；源码位于 `resources/panel/`。
- C++ 依赖：部分随 `thirdparty/` 提供，包括 GLFW、FMOD CORE 和 Cubism SDK；其余见 [`vcpkg.json`](../vcpkg.json)。RocksDB 子模块需通过 `git submodule update --init --recursive` 初始化。
- 云端服务：位于 `cloud/`，本地开发和部署见[云端游戏说明](cloud-game.md)。

Windows 面板使用 WebView2；macOS 使用 WKWebView、GLFW/Cocoa 透明窗口、AVFoundation 音频和菜单栏状态项。
macOS 登录页使用系统持久化的 WebKit Cookie 存储。当前 Live2D 渲染仍使用 OpenGL，Apple 已弃用该接口。

## macOS 构建

首次准备环境：

```sh
./.agents/prepare
./build-scripts/build_and_run_macos.sh
```

准备脚本检查工具、下载并初始化 vcpkg、安装前端与 C++ 依赖。
构建脚本依次构建前端、配置 CMake、编译并启动应用，默认使用 Release。

默认路径为 `build/vcpkg` 和 `build/vcpkg_installed`，可通过环境变量覆盖：

```sh
VCPKG_DIR=/path/to/vcpkg \
VCPKG_INSTALLED_DIR=/path/to/vcpkg_installed \
./build-scripts/build_and_run_macos.sh
```

`.agents/linked` 可将 `build/vcpkg` 链接到本机共享目录，让多个工作区复用同一份 checkout。
调试构建使用 `BUILD_TYPE=Debug ./build-scripts/build_and_run_macos.sh`。

### 手动构建

准备好 vcpkg 后，也可以逐步执行：

```sh
cd resources/panel
npm ci --legacy-peer-deps
npm run build
cd ../..

./build/vcpkg/bootstrap-vcpkg.sh -disableMetrics
./build/vcpkg/vcpkg install --triplet arm64-osx \
  --x-install-root="$PWD/build/vcpkg_installed"

cmake -S . -B build/macos-arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/build/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx \
  -DVCPKG_INSTALLED_DIR="$PWD/build/vcpkg_installed" \
  -DVCPKG_MANIFEST_INSTALL=OFF
cmake --build build/macos-arm64 --parallel
open build/macos-arm64/bin/JPet/JPet.app
```

构建时从 `cloud/wrangler.jsonc` 读取生产域名。开发环境的服务地址通过 CMake 参数 `JPET_CLOUD_URL` 指定，详见[云端服务本地开发](cloud-game.md#本地开发)。

## Windows 构建

可直接使用 [README 中的 PowerShell 命令](../README.md#windows--x64)。
现有 [`build_and_run.ps1`](../build-scripts/build_and_run.ps1) 默认使用 `D:\vcpkg`、Debug 配置和 `2.0.0-alpha` 版本号，使用前应按本机环境调整。

## 回归检查

以下命令复用已配置的 macOS 构建目录，并启用更新与语音测试：

```sh
cmake -S . -B build/macos-arm64 \
  -DJPET_BUILD_UPDATE_TESTS=ON -DJPET_BUILD_VOICE_TESTS=ON
cmake --build build/macos-arm64 --parallel
ctest --test-dir build/macos-arm64 --output-on-failure
python3 tests/update_helper_test.py
```

云端任务、队列与存档检查：

```sh
cd cloud
npm ci
npm run types
npm test
cd ..
python3 tests/cloud_config_test.py
bash build-scripts/test_cloud_sync_macos.sh
bash build-scripts/test_achievements_macos.sh
bash build-scripts/test_task_queue_macos.sh
```

云端与成就回归使用临时存储，原生同步检查连接本机测试服务。
语音协议、原生设置接口与显式联网检查见[语音验证说明](voice-chat.md#验证)。

## 自动发布

推送版本 tag（例如 `3.0.0` 或 `v3.0.0`）会触发[发布工作流](../.github/workflows/release-build.yml)。
Windows x64 和 macOS ARM64 均通过构建与更新测试后，工作流创建 GitHub Release 并上传：

- `jpet-<版本>-windows-x64.zip`：完整便携包，同时用于 Windows 自动更新。
- `jpet-<版本>-windows-x64-setup.exe`：当前用户安装程序。
- `jpet-<版本>-macos-arm64.zip`：包含 `JPet.app` 的完整应用包。
- `SHA256SUMS`：下载文件的 SHA-256 校验值。

```sh
git tag 3.0.0
git push origin 3.0.0
```

也可在 Actions 中手动运行工作流并输入版本号；手动运行只上传 Actions artifacts。
带预发布后缀的 tag（如 `3.0.0-rc.1`）会创建 prerelease。
客户端只选择正式版本：带 beta、alpha、rc 等后缀的 Release 即使未标记为 prerelease，也会被跳过；最新 Release 为测试版时会继续查找正式版本。

## 更新与排查

更新包通过 HTTPS 下载，并验证 GitHub Release asset 的 SHA-256 和文件大小。
下载、校验与解压完成后，用户点击「重启并更新」才会安装。
安装助手等待旧进程正常退出，替换应用后重新启动；替换或启动失败会尝试恢复旧版本，本地用户数据保留在原目录。

macOS 应先将 `JPet.app` 移到可写的应用目录；发布包使用 ad-hoc 签名，未进行 Apple Developer ID 签名和公证。
Windows 旧版若安装在 Program Files，更新时会请求 UAC 授权。

更新失败信息位于用户数据目录的 `updates/last-error.txt`，Mac 安装助手详细日志位于相应下载目录。
Mac 用户数据目录默认为 `~/Library/Application Support/JPet/`。
