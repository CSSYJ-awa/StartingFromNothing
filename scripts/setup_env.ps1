# ============================================================
#   Vulkan 开发环境半自动部署脚本
#   适用于 Windows 10/11
#   需要管理员权限
#   作者: GitHub Copilot
#   版本: 1.0
# ============================================================
#Requires -RunAsAdministrator

# ============================================================
# 全局配置
# ============================================================
$script:LogFile = Join-Path $PSScriptRoot "install_log.txt"
$script:StartTime = Get-Date

# 组件状态表：用于最终总结报告
$script:ComponentStatus = @()

# MSYS2 可能路径（按优先级）
$script:Msys2Candidates = @(
    "C:\msys64",
    "C:\msys2",
    "$env:ProgramFiles\msys64",
    "${env:ProgramFiles(x86)}\msys64",
    "D:\msys64",
    "D:\msys2",
    "D:\Program Files\msys64",
    "E:\msys64",
    "E:\msys2"
)

# ============================================================
# 1. 辅助函数
# ============================================================

function Write-Log {
    param([string]$Message)
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $line = "[$timestamp] $Message"
    Add-Content -Path $script:LogFile -Value $line -Encoding UTF8
}

function Write-Color {
    param(
        [string]$Message,
        [ConsoleColor]$Color = "White",
        [switch]$NoNewline
    )
    $originalColor = $host.UI.RawUI.ForegroundColor
    $host.UI.RawUI.ForegroundColor = $Color
    if ($NoNewline) {
        Write-Host -NoNewline $Message
    } else {
        Write-Host $Message
    }
    $host.UI.RawUI.ForegroundColor = $originalColor
    Write-Log $Message
}

function Write-Info {
    param([string]$Message)
    Write-Color "  [INFO] $Message" -Color Blue
}

function Write-Ok {
    param([string]$Message)
    Write-Color "  [OK]   $Message" -Color Green
}

function Write-Warn {
    param([string]$Message)
    Write-Color "  [WARN] $Message" -Color Yellow
}

function Write-ErrorMsg {
    param([string]$Message)
    Write-Color "  [FAIL] $Message" -Color Red
}

function Write-Section {
    param([string]$Title)
    Write-Host ""
    Write-Color ("-" * 52) -Color DarkGray
    Write-Color "  $Title" -Color Cyan
    Write-Color ("-" * 52) -Color DarkGray
}

function Add-ComponentStatus {
    param(
        [string]$Name,
        [string]$Status     # "已安装" / "已安装(本次)" / "未安装(需手动)" / "跳过"
    )
    $script:ComponentStatus += [PSCustomObject]@{
        Name   = $Name
        Status = $Status
    }
}

function Update-SessionPath {
    <#
    .SYNOPSIS
        从注册表刷新当前会话的 PATH，使后续 where/cmake 等检测立即生效。
    #>
    $machinePath = [Environment]::GetEnvironmentVariable("PATH", "Machine")
    $userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
    $combined = "$machinePath;$userPath"
    # 去重
    $paths = $combined -split ';' | Where-Object { $_ -ne '' } | Select-Object -Unique
    $env:PATH = $paths -join ';'
}

function Add-ToUserPath {
    param([string]$NewPath)
    $currentUserPath = [Environment]::GetEnvironmentVariable("PATH", "User")
    if ($currentUserPath -split ';' -contains $NewPath) {
        Write-Info "PATH 中已存在: $NewPath"
        return
    }
    $newUserPath = if ($currentUserPath) { "$currentUserPath;$NewPath" } else { $NewPath }
    [Environment]::SetEnvironmentVariable("PATH", $newUserPath, "User")
    Write-Ok "已将 $NewPath 添加到用户 PATH"
    Update-SessionPath
}

function Test-CommandExists {
    param([string]$Command)
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Stop"
    try {
        $null = Get-Command $Command -ErrorAction Stop
        return $true
    } catch {
        return $false
    } finally {
        $ErrorActionPreference = $oldPreference
    }
}

function Find-Msys2Root {
    <#
    .SYNOPSIS
        自动检测 MSYS2 安装根目录。
    .DESCRIPTION
        检测策略（按优先级）：
          1. 环境变量 MSYS2_ROOT
          2. where bash 路径反推 (寻找 etc/pacman.conf)
          3. where g++ 路径反推 (UC64/mingw64 的父目录的父目录)
          4. 常见安装路径
    #>
    # 策略 1: 环境变量
    if ($env:MSYS2_ROOT -and (Test-Path "$env:MSYS2_ROOT\etc\pacman.conf")) {
        Write-Info "从 MSYS2_ROOT 环境变量获取: $env:MSYS2_ROOT"
        return $env:MSYS2_ROOT
    }

    # 策略 2: where bash 路径反推
    $bashPath = Get-Command "bash" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
    if ($bashPath) {
        $candidate = Split-Path (Split-Path $bashPath) -Parent  # {bash_dir}/../
        if (Test-Path "$candidate\etc\pacman.conf") {
            Write-Info "从 bash 路径反推: $candidate"
            return $candidate
        }
    }

    # 策略 3: where g++ 路径反推
    $gppPath = Get-Command "g++" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
    if ($gppPath) {
        # g++ 在 {msys2}/{subsys}/bin/g++.exe
        $subsysDir = Split-Path (Split-Path $gppPath) -Parent  # 子系统目录
        $candidate = Split-Path $subsysDir -Parent              # MSYS2 根目录
        if (Test-Path "$candidate\etc\pacman.conf") {
            Write-Info "从 g++ 路径反推: $candidate"
            return $candidate
        }
    }

    # 策略 4: 常见路径
    foreach ($path in $script:Msys2Candidates) {
        if (Test-Path "$path\etc\pacman.conf") {
            Write-Info "从常见路径找到: $path"
            return $path
        }
    }

    return $null
}

function Invoke-Msys2Pacman {
    <#
    .SYNOPSIS
        通过 MSYS2 的 bash.exe 执行 pacman 命令。
    #>
    param(
        [string]$Msys2Root,
        [string]$PackageSpec  # 例如 "mingw-w64-x86_64-toolchain"
    )

    $bashExe = "$Msys2Root\usr\bin\bash.exe"
    if (-not (Test-Path $bashExe)) {
        Write-ErrorMsg "MSYS2 bash.exe 未找到: $bashExe"
        return $false
    }

    $pacmanCmd = "pacman -S $PackageSpec --noconfirm"
    Write-Info "执行: $pacmanCmd"

    try {
        $proc = Start-Process -FilePath $bashExe -ArgumentList "-lc", $pacmanCmd -NoNewWindow -Wait -PassThru
        if ($proc.ExitCode -eq 0) {
            Write-Ok "pacman 安装成功: $PackageSpec"
            return $true
        } else {
            Write-ErrorMsg "pacman 安装失败 (退出码: $($proc.ExitCode)): $PackageSpec"
            Write-Info "请尝试在 MSYS2 终端中手动运行: pacman -S $PackageSpec"
            return $false
        }
    } catch {
        Write-ErrorMsg "执行 pacman 时出错: $_"
        return $false
    }
}

function Open-DownloadPage {
    param([string]$Url, [string]$ComponentName)
    Write-Warn "$ComponentName 未安装，正在打开下载页面..."
    Write-Info "URL: $Url"
    try {
        Start-Process $Url
    } catch {
        Write-ErrorMsg "无法打开浏览器，请手动访问: $Url"
    }
}

# ============================================================
# 2. 脚本入口
# ============================================================

function Start-Setup {
    # 初始化日志文件
    "=" * 60 | Out-File -FilePath $script:LogFile -Encoding UTF8
    "Vulkan 开发环境部署日志" | Out-File -FilePath $script:LogFile -Encoding UTF8 -Append
    "开始时间: $($script:StartTime.ToString('yyyy-MM-dd HH:mm:ss'))" | Out-File -FilePath $script:LogFile -Encoding UTF8 -Append
    "=" * 60 | Out-File -FilePath $script:LogFile -Encoding UTF8 -Append

    Write-Host ""
    Write-Color "============================================================" -Color Cyan
    Write-Color "   Vulkan 开发环境半自动部署脚本" -Color Cyan
    Write-Color "   适用于 Windows 10/11  |  需要管理员权限" -Color Cyan
    Write-Color "============================================================" -Color Cyan
    Write-Host ""

    # ============================================================
    # 2.1 管理员权限检查
    # ============================================================
    Write-Section "权限检查"
    $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-ErrorMsg "未以管理员身份运行！"
        Write-Warn "请右键点击脚本，选择"以管理员身份运行""
        Write-Host ""
        Read-Host "按 Enter 退出"
        exit 1
    }
    Write-Ok "已获得管理员权限"

    # ============================================================
    # 2.2 检测并安装 MSYS2
    # ============================================================
    Write-Section "MSYS2"

    $msys2Root = Find-Msys2Root
    if ($msys2Root) {
        Write-Ok "MSYS2 已安装在: $msys2Root"
        Add-ComponentStatus -Name "MSYS2" -Status "已安装"
    } else {
        Write-Warn "MSYS2 未安装"
        Add-ComponentStatus -Name "MSYS2" -Status "未安装(需手动)"
        Open-DownloadPage -Url "https://www.msys2.org/" -ComponentName "MSYS2"
        Write-Host ""
        Write-Color "  安装指引:" -Color Yellow
        Write-Color "  1. 下载并运行 msys2-x86_64-xxxxx.exe" -Color Yellow
        Write-Color "  2. 按默认选项安装到 C:\msys64" -Color Yellow
        Write-Color "  3. 安装完成后，重新运行此脚本以继续后续安装" -Color Yellow
        Write-Host ""
        Read-Host "按 Enter 退出脚本，安装完成后重新运行"
        exit 0
    }

    # ============================================================
    # 2.3 检测并安装 MinGW-w64
    # ============================================================
    Write-Section "MinGW-w64 (GCC 工具链)"

    $gppExists = Test-CommandExists "g++"
    if ($gppExists) {
        $gppVersion = & g++ -dumpfullversion 2>$null
        $gppPath = (Get-Command g++).Source
        Write-Ok "MinGW-w64 已安装: $gppPath"
        Write-Info "GCC 版本: $gppVersion"
        Add-ComponentStatus -Name "MinGW-w64" -Status "已安装"
    } else {
        if ($msys2Root) {
            Write-Info "MSYS2 已安装，正在通过 pacman 安装 MinGW-w64 工具链..."
            $ok = Invoke-Msys2Pacman -Msys2Root $msys2Root -PackageSpec "mingw-w64-x86_64-toolchain"
            if ($ok) {
                # 添加到 PATH
                $mingwBin = "$msys2Root\ucrt64\bin"
                if (Test-Path $mingwBin) {
                    Add-ToUserPath -NewPath $mingwBin
                    Write-Ok "已将 $mingwBin 添加到 PATH"
                } else {
                    # fallback: mingw64
                    $mingwBin = "$msys2Root\mingw64\bin"
                    if (Test-Path $mingwBin) {
                        Add-ToUserPath -NewPath $mingwBin
                    }
                }
                Update-SessionPath
                # 重新检测
                if (Test-CommandExists "g++") {
                    Write-Ok "MinGW-w64 安装完成"
                    Add-ComponentStatus -Name "MinGW-w64" -Status "已安装(本次)"
                } else {
                    Write-ErrorMsg "MinGW-w64 安装后仍无法检测到 g++，请手动检查 PATH 设置"
                    Add-ComponentStatus -Name "MinGW-w64" -Status "未安装(需手动)"
                }
            } else {
                Add-ComponentStatus -Name "MinGW-w64" -Status "未安装(需手动)"
            }
        } else {
            Write-Warn "MSYS2 未安装，无法安装 MinGW-w64，请先安装 MSYS2"
            Add-ComponentStatus -Name "MinGW-w64" -Status "未安装(需手动)"
        }
    }

    # ============================================================
    # 2.4 检测并安装 CMake
    # ============================================================
    Write-Section "CMake"

    $cmakeExists = Test-CommandExists "cmake"
    if ($cmakeExists) {
        $cmakeVersion = & cmake --version | Select-Object -First 1
        Write-Ok "CMake 已安装: $cmakeVersion"
        Add-ComponentStatus -Name "CMake" -Status "已安装"
    } else {
        Write-Info "CMake 未安装，尝试通过 winget 安装..."
        $wingetExists = Test-CommandExists "winget"
        if ($wingetExists) {
            try {
                $proc = Start-Process -FilePath "winget" -ArgumentList "install CMake -e --silent" -NoNewWindow -Wait -PassThru
                if ($proc.ExitCode -eq 0) {
                    Write-Ok "CMake 安装完成"
                    Add-ComponentStatus -Name "CMake" -Status "已安装(本次)"
                    # 刷新 PATH 使 cmake 立即可用
                    Update-SessionPath
                } else {
                    Write-ErrorMsg "winget 安装 CMake 失败 (退出码: $($proc.ExitCode))"
                    Open-DownloadPage -Url "https://cmake.org/download/" -ComponentName "CMake"
                    Add-ComponentStatus -Name "CMake" -Status "未安装(需手动)"
                }
            } catch {
                Write-ErrorMsg "winget 执行出错: $_"
                Open-DownloadPage -Url "https://cmake.org/download/" -ComponentName "CMake"
                Add-ComponentStatus -Name "CMake" -Status "未安装(需手动)"
            }
        } else {
            Write-Warn "winget 不可用，打开 CMake 下载页面..."
            Open-DownloadPage -Url "https://cmake.org/download/" -ComponentName "CMake"
            Add-ComponentStatus -Name "CMake" -Status "未安装(需手动)"
        }
    }

    # ============================================================
    # 2.5 检测并安装 GLFW
    # ============================================================
    Write-Section "GLFW"

    $glfwHeader = "$msys2Root\mingw64\include\GLFW\glfw3.h"
    if (Test-Path $glfwHeader) {
        Write-Ok "GLFW 已安装 (头文件: $glfwHeader)"
        Add-ComponentStatus -Name "GLFW" -Status "已安装"
    } else {
        if ($msys2Root) {
            Write-Info "MSYS2 已安装，正在通过 pacman 安装 GLFW..."
            $ok = Invoke-Msys2Pacman -Msys2Root $msys2Root -PackageSpec "mingw-w64-x86_64-glfw"
            if ($ok) {
                if (Test-Path $glfwHeader) {
                    Write-Ok "GLFW 安装完成"
                    Add-ComponentStatus -Name "GLFW" -Status "已安装(本次)"
                } else {
                    Write-ErrorMsg "GLFW 安装后头文件仍不存在，请尝试在 MSYS2 终端中手动安装"
                    Add-ComponentStatus -Name "GLFW" -Status "未安装(需手动)"
                }
            } else {
                Add-ComponentStatus -Name "GLFW" -Status "未安装(需手动)"
            }
        } else {
            Write-Warn "MSYS2 未安装，跳过 GLFW 安装"
            Add-ComponentStatus -Name "GLFW" -Status "跳过"
        }
    }

    # ============================================================
    # 2.6 检测并安装 GLM
    # ============================================================
    Write-Section "GLM"

    $glmHeader = "$msys2Root\mingw64\include\glm\glm.hpp"
    if (Test-Path $glmHeader) {
        Write-Ok "GLM 已安装 (头文件: $glmHeader)"
        Add-ComponentStatus -Name "GLM" -Status "已安装"
    } else {
        if ($msys2Root) {
            Write-Info "MSYS2 已安装，正在通过 pacman 安装 GLM..."
            $ok = Invoke-Msys2Pacman -Msys2Root $msys2Root -PackageSpec "mingw-w64-x86_64-glm"
            if ($ok) {
                if (Test-Path $glmHeader) {
                    Write-Ok "GLM 安装完成"
                    Add-ComponentStatus -Name "GLM" -Status "已安装(本次)"
                } else {
                    Write-ErrorMsg "GLM 安装后头文件仍不存在，请尝试在 MSYS2 终端中手动安装"
                    Add-ComponentStatus -Name "GLM" -Status "未安装(需手动)"
                }
            } else {
                Add-ComponentStatus -Name "GLM" -Status "未安装(需手动)"
            }
        } else {
            Write-Warn "MSYS2 未安装，跳过 GLM 安装"
            Add-ComponentStatus -Name "GLM" -Status "跳过"
        }
    }

    # ============================================================
    # 2.7 检测并提示安装 Vulkan SDK
    # ============================================================
    Write-Section "Vulkan SDK"

    if ($env:VULKAN_SDK) {
        Write-Ok "Vulkan SDK 已安装: $env:VULKAN_SDK"
        Add-ComponentStatus -Name "Vulkan SDK" -Status "已安装"
    } else {
        Write-Warn "Vulkan SDK 未安装（环境变量 VULKAN_SDK 不存在）"
        Add-ComponentStatus -Name "Vulkan SDK" -Status "未安装(需手动)"
        Open-DownloadPage -Url "https://vulkan.lunarg.com/sdk/home" -ComponentName "Vulkan SDK"
        Write-Host ""
        Write-Color "  安装指引:" -Color Yellow
        Write-Color "  1. 下载并运行 VulkanSDK-xxxx-Installer.exe" -Color Yellow
        Write-Color "  2. 务必勾选以下组件:" -Color Yellow
        Write-Color "     - Vulkan Runtime（运行时）" -Color Yellow
        Write-Color "     - Vulkan SDK Development Components（开发组件）" -Color Yellow
        Write-Color "  3. 安装完成后，请设置环境变量 VULKAN_SDK" -Color Yellow
        Write-Color "     指向安装根目录（例如 D:\Program Files\VulkanSDK\1.4.341.1）" -Color Yellow
        Write-Host ""
        Read-Host "按 Enter 继续（安装 Vulkan SDK 后请重新运行此脚本以获得完整环境验证）"
    }

    # ============================================================
    # 2.8 检测并安装 Git
    # ============================================================
    Write-Section "Git"

    $gitExists = Test-CommandExists "git"
    if ($gitExists) {
        $gitVersion = & git --version
        Write-Ok "Git 已安装: $gitVersion"
        Add-ComponentStatus -Name "Git" -Status "已安装"
    } else {
        Write-Info "Git 未安装，尝试通过 winget 安装..."
        $wingetExists = Test-CommandExists "winget"
        if ($wingetExists) {
            try {
                $proc = Start-Process -FilePath "winget" -ArgumentList "install Git.Git -e --silent" -NoNewWindow -Wait -PassThru
                if ($proc.ExitCode -eq 0) {
                    Write-Ok "Git 安装完成"
                    Add-ComponentStatus -Name "Git" -Status "已安装(本次)"
                    Update-SessionPath
                } else {
                    Write-ErrorMsg "winget 安装 Git 失败 (退出码: $($proc.ExitCode))"
                    Open-DownloadPage -Url "https://git-scm.com/download/win" -ComponentName "Git"
                    Add-ComponentStatus -Name "Git" -Status "未安装(需手动)"
                }
            } catch {
                Write-ErrorMsg "winget 执行出错: $_"
                Open-DownloadPage -Url "https://git-scm.com/download/win" -ComponentName "Git"
                Add-ComponentStatus -Name "Git" -Status "未安装(需手动)"
            }
        } else {
            Write-Warn "winget 不可用，打开 Git 下载页面..."
            Open-DownloadPage -Url "https://git-scm.com/download/win" -ComponentName "Git"
            Add-ComponentStatus -Name "Git" -Status "未安装(需手动)"
        }
    }

    # ============================================================
    # 2.9 最终 PATH 刷新 & 环境验证
    # ============================================================
    Write-Section "环境验证"

    Update-SessionPath

    # 调用 check_env.bat
    $checkEnvPath = Join-Path $PSScriptRoot "check_env.bat"
    if (Test-Path $checkEnvPath) {
        Write-Info "正在调用 check_env.bat 验证环境..."
        Write-Host ""
        Write-Color ("-" * 52) -Color DarkGray
        Write-Color "  check_env.bat 输出:" -Color Cyan
        Write-Color ("-" * 52) -Color DarkGray
        try {
            & $checkEnvPath --no-pause
        } catch {
            Write-ErrorMsg "check_env.bat 执行出错: $_"
        }
        Write-Color ("-" * 52) -Color DarkGray
        Write-Host ""
    } else {
        Write-Warn "check_env.bat 未找到 (预期位置: $checkEnvPath)"
        Write-Info "请手动运行 scripts\check_env.bat 验证环境"
    }

    # ============================================================
    # 2.10 总结报告
    # ============================================================
    Write-Section "部署总结"

    $endTime = Get-Date
    $duration = $endTime - $script:StartTime

    Write-Host ""
    Write-Color " 组件部署状态:" -Color Cyan
    Write-Host ("  " + "-" * 50)

    foreach ($item in $script:ComponentStatus) {
        $icon = ""
        $color = "White"
        switch ($item.Status) {
            "已安装" { $icon = "[OK]"; $color = "Green" }
            "已安装(本次)" { $icon = "[OK]"; $color = "Green" }
            "未安装(需手动)" { $icon = "[--]"; $color = "Red" }
            "跳过" { $icon = "[..]"; $color = "DarkGray" }
            default { $icon = "[??]"; $color = "Yellow" }
        }
        Write-Color ("    {0,-8} {1,-20} {2}" -f $icon, $item.Name, $item.Status) -Color $color
    }

    Write-Host ("  " + "-" * 50)
    Write-Host ""
    Write-Info "总耗时: $($duration.Minutes) 分 $($duration.Seconds) 秒"
    Write-Info "日志已保存至: $script:LogFile"

    # 检查是否有组件需要手动处理
    $pending = $script:ComponentStatus | Where-Object { $_.Status -eq "未安装(需手动)" }
    if ($pending) {
        Write-Host ""
        Write-Warn "以下组件需要手动安装:"
        foreach ($item in $pending) {
            Write-Color "    - $($item.Name)" -Color Yellow
        }
        Write-Host ""
        Write-Info "安装完上述组件后，重新运行此脚本以完成环境验证"
    }

    # 检查是否有安装失败的组件
    $installed = $script:ComponentStatus | Where-Object { $_.Status -like "已安装*" }
    $total = $script:ComponentStatus.Count
    Write-Host ""
    Write-Color ("  完成: {0}/{1} 个组件" -f $installed.Count, $total) -Color Cyan

    # 最终提示：建议重启终端
    Write-Host ""
    Write-Color "  [提示] 如果在 PowerShell 中仍然找不到新安装的命令，" -Color Yellow
    Write-Color "         请关闭当前终端并重新打开，或重启计算机。" -Color Yellow
    Write-Host ""

    Write-Color "============================================================" -Color Cyan
    Write-Color "   脚本执行完毕" -Color Cyan
    Write-Color "============================================================" -Color Cyan
    Write-Host ""
}

# ============================================================
# 启动
# ============================================================
Start-Setup
