<#
================================================================================
 build.ps1 —— cpp-notes 统一构建/运行脚本（MSVC / Visual Studio）
================================================================================
 不需要手配环境变量：脚本用 vswhere.exe 定位 Visual Studio，
 再调用 vcvars64.bat 进入 Developer 环境，然后编译。

 常用命令：
   .\build.ps1                  # 编译全部章节的示例
   .\build.ps1 -Chapter 02-oop  # 只编译某一章
   .\build.ps1 -Run             # 编译并运行全部示例
   .\build.ps1 -Clean           # 先删除 build 目录
   .\build.ps1 -Std c++20       # 指定语言标准（默认 c++20）
   .\build.ps1 -WX              # 把警告当成错误（提交前建议开）

 设计说明：
   每个 .cpp 文件都自带 int main()，所以是「一个 .cpp = 一个独立可执行文件」，
   编译产物放到 build\<章节>\<文件名>.exe，与源码目录分离（源码目录保持干净）。
================================================================================
#>
[CmdletBinding()]
param(
    [string]   $Chapter = '',
    [ValidateSet('c++17', 'c++20', 'c++23', 'latest')]
    [string]   $Std = 'c++20',
    [string]   $Config = 'Debug',
    [switch]   $Run,
    [switch]   $Clean,
    [switch]   $WX,
    [switch]   $Quiet
)

$ErrorActionPreference = 'Stop'
$Root     = $PSScriptRoot
$BuildDir = Join-Path $Root 'build'

# 默认参与统一构建的章节目录。
# 08-boost 故意不在列表里：它依赖外部 Boost 头文件，是独立工程，单独构建
# （见 08-boost\README_Boost_vs_STL.md）。
$ChapterRoots = @('01-basics', '02-oop', '03-modern', '04-templates', '05-stl', '06-algorithms', '07-engineering')

# ---------------------------------------------------------------- 1. 定位 VS
function Find-VsDevShell {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw "找不到 vswhere.exe。请安装 Visual Studio（含「使用 C++ 的桌面开发」工作负载）。"
    }
    $vsPath = & $vswhere -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw "vswhere 没找到带 C++ 工具集的 Visual Studio 安装。" }
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "找不到 vcvars64.bat：$vcvars" }
    return $vcvars
}

# 把 vcvars64.bat 设置的 PATH/INCLUDE/LIB 导入当前 PowerShell 会话
function Enter-VsDevEnvironment([string] $vcvars) {
    Write-Verbose "进入 MSVC 开发者环境: $vcvars"
    $lines = & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            $name, $value = $Matches[1], $Matches[2]
            # PATH 必须整体替换，不能 Set-Item 追加
            if ($name -ieq 'PATH') { $env:PATH = $value }
            else { Set-Item -Path "env:$name" -Value $value -ErrorAction SilentlyContinue }
        }
    }
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "vcvars64.bat 执行后仍然找不到 cl.exe。"
    }
}

# ---------------------------------------------------------------- 2. 编译选项
function Get-ClOptions {
    $opts = @(
        '/nologo'
        "/std:$Std"
        '/EHsc'          # 标准 C++ 异常模型
        '/W4'            # 高警告级别
        '/utf-8'         # 源码按 UTF-8 解析（含中文注释）
        '/permissive-'   # 关闭宽松模式，强制标准两阶段查找
        '/Zc:__cplusplus'# 让 __cplusplus 报真实值（MSVC 默认恒为 199711L）
        '/diagnostics:caret'
        '/bigobj'
        '/MDd'
        '/Zi'
    )
    if ($WX) { $opts += '/WX' }
    if ($Config -eq 'Release') { $opts += '/O2'; $opts += '/DNDEBUG' }
    return $opts
}

# ---------------------------------------------------------------- 3. 主流程
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "[clean] 删除 $BuildDir" -ForegroundColor DarkGray
    Remove-Item $BuildDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$vcvars = Find-VsDevShell
Enter-VsDevEnvironment $vcvars
if (-not $Quiet) {
    Write-Host ("[env] cl.exe = " + (Get-Command cl.exe).Source) -ForegroundColor DarkGray
    Write-Host ("[env] 语言标准 = /std:$Std, 配置 = $Config") -ForegroundColor DarkGray
}

$chapters = if ($Chapter) { @($Chapter) } else { $ChapterRoots }
$optList  = Get-ClOptions
$sources  = @()
foreach ($ch in $chapters) {
    $dir = Join-Path $Root $ch
    if (-not (Test-Path $dir)) { Write-Warning "章节目录不存在，跳过: $ch"; continue }
    # 含 .build-skip 标记的目录需要外部依赖（如 08-boost 需要 Boost 头文件），
    # 默认不参与统一构建，避免因为缺依赖而报一堆无关错误。
    if ((Test-Path (Join-Path $dir '.build-skip')) -and -not $Chapter) {
        Write-Host "[skip] $ch （含 .build-skip 标记，需外部依赖，单独构建）" -ForegroundColor DarkYellow
        continue
    }
    # 刻意只取本章目录「第一层」的 .cpp，不递归。
    # 子目录 tests\ 里是 CMake/CTest 专用的多文件测试（实现与测试分离、没有 main），
    # 用「一个 .cpp = 一个可执行文件」的方式编译必然失败（LNK1561 / LNK2019）。
    # 它由 07-engineering/tests/CMakeLists.txt 配套构建，详见该文件。
    $sources += Get-ChildItem -Path $dir -Filter *.cpp -File
}

if ($sources.Count -eq 0) { Write-Warning '没有找到任何 .cpp 源文件。'; exit 0 }

$ok = 0; $failed = @(); $warned = @()
foreach ($src in $sources) {
    $rel     = $src.FullName.Substring($Root.Length + 1)
    $outDir  = Join-Path $BuildDir (Split-Path $rel -Parent)
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $exe     = Join-Path $outDir ($src.BaseName + '.exe')

    Write-Host ("[cl] $rel") -ForegroundColor Cyan
    # /FS 是必须的：不加的话多个 cl.exe 同时写同一个 PDB 会报 fatal error C1041，
    # 并行编译（多个终端同时验证 / 多核构建）时必然踩到。
    # /Fd 把 PDB 放进 build 目录，免得它跑到仓库根目录里污染工作区。
    $pdb    = Join-Path $outDir ($src.BaseName + '.pdb')
    $clOut  = & cl.exe @optList "/FS" "/Fd$pdb" "/Fo$outDir\" "/Fe$exe" $src.FullName 2>&1
    $clCode = $LASTEXITCODE

    # 把警告和错误都打出来（/W4 下警告正是我们想看到的）。
    # 只匹配「诊断编号」形态（warning C4996 / error C2039），否则像
    # "01_compiler_warnings_and_tools.cpp" 这种文件名回显会被误判成警告。
    $diag = @($clOut | Where-Object { $_ -match '\b(?:warning|error)\s+[A-Z]+\d+' -or $_ -match 'fatal error' })
    foreach ($d in $diag) {
        if ($d -match '\berror\s+[A-Z]+\d+' -or $d -match 'fatal error') { Write-Host "    $d" -ForegroundColor Red }
        else                                                             { Write-Host "    $d" -ForegroundColor Yellow }
    }
    if (@($diag | Where-Object { $_ -match '\bwarning\s+[A-Z]+\d+' }).Count -gt 0) { $warned += $rel }

    if ($clCode -ne 0) {
        Write-Host "    [FAIL] 编译失败: $rel" -ForegroundColor Red
        $failed += $rel
        continue
    }
    Write-Host "    [ OK ] $exe" -ForegroundColor Green
    $ok++

    if ($Run) {
        Write-Host ("    [run] ---- " + $src.BaseName + " ----") -ForegroundColor DarkGray
        # 示例程序有交互输入，用重定向的空 stdin 让它不阻塞；程序自己返回非 0 也算编译验证通过
        $runOut = '' | & $exe 2>&1
        $runOut | ForEach-Object { Write-Host "    $_" }
        Write-Host ("    [exit] " + $LASTEXITCODE) -ForegroundColor DarkGray
    }
}

Write-Host ''
Write-Host ("==== 编译结果: 成功 $ok / 共 " + $sources.Count + " ====") -ForegroundColor White
if ($warned.Count -gt 0) {
    Write-Host "以下文件有警告（建议修掉）:" -ForegroundColor Yellow
    $warned | Sort-Object -Unique | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
}
if ($failed.Count -gt 0) {
    Write-Host '失败列表:' -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
Write-Host '全部通过。' -ForegroundColor Green
