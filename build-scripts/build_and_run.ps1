# 停止执行脚本在遇到错误时
$ErrorActionPreference = 'Stop'

try {
    # 运行 npm 构建命令
    Push-Location ./resources/panel
    Write-Host "Running npm build ..." -ForegroundColor Green
    npm run build
    Pop-Location

    # 配置 CMake 项目
    Write-Host "Configuring CMake project ..." -ForegroundColor Green
    cmake . -Bbuild -DVERSION=20240809

    # 构建项目
    Write-Host "Building project ..." -ForegroundColor Green
    cmake --build ./build --config Debug --target ALL_BUILD -j 26

    # 运行构建的程序
    Write-Host "Running built application ..." -ForegroundColor Green
    & "./build/bin/JPet/Debug/JPet.exe"

} catch {
    Write-Host "An error occurred: $_" -ForegroundColor Red
    exit $_.Exception.HResult
} finally {
    Write-Host "Script completed." -ForegroundColor Green
}
