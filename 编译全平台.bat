@echo off
:: Go交叉编译多平台批处理脚本
:: 作者: DeepSeek Chat
:: 日期: %date%
:: 功能: 同时编译Linux, Darwin(macOS), Windows的32位和64位版本

setlocal enabledelayedexpansion

:: 设置输出目录
set OUTPUT_DIR=build
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

:: 设置程序名称(不带扩展名)
set APP_NAME=convnetgo

:: 清空旧构建
echo 正在清理旧构建文件...
del /q "%OUTPUT_DIR%\*" >nul 2>&1

:: 支持的平台和架构组合
set PLATFORMS=windows linux darwin
set ARCHS=386 amd64

:: 开始构建
echo 开始跨平台构建...

for %%P in (%PLATFORMS%) do (
    for %%A in (%ARCHS%) do (
        :: 设置GO环境变量
        set GOOS=%%P
        set GOARCH=%%A
        
        :: 确定文件扩展名
        if "%%P"=="windows" (
            set EXT=.exe
        ) else (
            set EXT=
        )
        
        :: 确定输出文件名
        set OUTPUT_FILE=!APP_NAME!-%%P-%%A!EXT!
        
        :: 执行编译
        echo 正在构建: %%P-%%A...
        set "CMD=go env -w GOOS=%%P GOARCH=%%A && go build -ldflags="-s -w" -o !OUTPUT_DIR!\!OUTPUT_FILE!"
        cmd /c "!CMD!"
        
        if errorlevel 1 (
            echo [错误] 构建 %%P-%%A 失败
        ) else (
            echo [成功] 构建完成: !OUTPUT_FILE!
        )
    )
)

echo.
echo 所有平台构建完成!
echo 输出目录: %CD%\%OUTPUT_DIR%\
dir /b "%OUTPUT_DIR%"

pause