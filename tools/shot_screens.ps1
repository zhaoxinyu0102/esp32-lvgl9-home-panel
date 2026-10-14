# 批量截屏：依次切屏 + 抓图 + 存 PNG（逻辑内联，不依赖嵌套 pwsh）
#
# 用法: powershell -File tools\shot_screens.ps1 [-Port COM9] [-Step 3] [-Screens 0,1,2,3,4,5,6,7]
param(
    [string]$Port = "COM9",
    [string]$OutDir = "docs\screenshots",
    [int]$Step = 3,
    [int[]]$Screens = @(0,1,2,3,4,5,6,7)
)

Add-Type -AssemblyName System.Drawing

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
$out  = Join-Path $root $OutDir
New-Item -ItemType Directory -Force $out | Out-Null

$names = @{ 0="01-home"; 1="02-lights"; 2="03-climate"; 3="04-curtains";
            4="05-energy"; 5="06-coffee"; 6="07-weather"; 7="08-settings" }

function Save-Shot {
    param([System.IO.Ports.SerialPort]$sp, [string]$path)

    $sp.DiscardInBuffer()
    $sp.Write("shot $Step`n")

    # 找头 @@SHOT w h step stride
    $hdr = ""
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        try { $hdr += $sp.ReadExisting() } catch {}
        if ($hdr -match '@@SHOT\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)') { break }
        Start-Sleep -Milliseconds 100
    }
    if ($hdr -notmatch '@@SHOT\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)') {
        Write-Host "  [shot] 没收到 @@SHOT 头"
        return $false
    }
    $w = [int]$Matches[1]; $h = [int]$Matches[2]; $st = [int]$Matches[3]
    $sw = [int][Math]::Ceiling($w / $st)
    $sh = [int][Math]::Ceiling($h / $st)
    $need = $sw * $sh * 2

    $buf = New-Object byte[] $need
    $got = 0
    $deadline = (Get-Date).AddSeconds(240)
    while ($got -lt $need -and (Get-Date) -lt $deadline) {
        try {
            $n = $sp.Read($buf, $got, [Math]::Min(4096, $need - $got))
            if ($n -gt 0) { $got += $n }
        } catch {}
    }
    try { [void]$sp.ReadExisting() } catch {}

    $rows = [Math]::Floor($got / ($sw * 2))
    $imgH = [Math]::Max(1, [Math]::Min($sh, $rows))
    $bmp = New-Object System.Drawing.Bitmap $sw, $imgH, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $sw, $imgH
    $bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $ob = New-Object byte[] ($bd.Stride * $imgH)
    for ($y = 0; $y -lt $imgH; $y++) {
        $ro = $y * $sw * 2
        $do = $y * $bd.Stride
        for ($x = 0; $x -lt $sw; $x++) {
            $v = [BitConverter]::ToUInt16($buf, $ro + $x * 2)
            $o = $do + $x * 3
            $ob[$o]     = [byte]((($v -band 0x1F) * 255) / 31)
            $ob[$o + 1] = [byte](((($v -shr 5) -band 0x3F) * 255) / 63)
            $ob[$o + 2] = [byte](((($v -shr 11) -band 0x1F) * 255) / 31)
        }
    }
    [System.Runtime.InteropServices.Marshal]::Copy($ob, 0, $bd.Scan0, $ob.Length)
    $bmp.UnlockBits($bd)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host ("  [shot] {0}  {1}x{2}  ({3} 字节)" -f (Split-Path -Leaf $path), $sw, $imgH, $got)
    return $true
}

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.ReadTimeout = 3000
$sp.Open()
Write-Host "[shots] 端口 $Port 已打开，采样 1/$Step，共 $($Screens.Count) 张"

foreach ($s in $Screens) {
    $sp.DiscardInBuffer()
    $sp.Write("ui $s`n")
    Start-Sleep -Milliseconds 1600
    $nm = $names[$s]; if (-not $nm) { $nm = "screen$s" }
    $png = Join-Path $out "$nm.png"
    Write-Host "[shots] 屏幕 $s -> $nm.png"
    [void](Save-Shot -sp $sp -path $png)
    Start-Sleep -Milliseconds 300
}

# 收尾：切回首页
$sp.Write("ui 0`n")
Start-Sleep -Milliseconds 500
$sp.Close()
Write-Host "[shots] 全部完成，输出目录: $out"
