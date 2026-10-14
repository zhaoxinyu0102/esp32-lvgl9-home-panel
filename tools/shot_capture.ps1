# 屏幕截图抓取：串口发 `shot <步长>` → 读回 RGB565 → 存成 PNG
#
# 用法:
#   pwsh -File tools\shot_capture.ps1 [-Port COM9] [-Step 2] [-Out shot.png]
#
# 前置：板子固件支持 `shot` 命令（LV_USE_SNAPSHOT 1 + serial_cli 里的实现），
#       并且没有别的程序占着串口（串口监视器要先关掉）。
param(
    [string]$Port = "COM9",
    [int]$Step = 2,
    [string]$Out = "shot.png"
)

Add-Type -AssemblyName System.Drawing

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.ReadTimeout = 3000
$sp.Open()
Write-Host "[shot] 已打开 $Port，发送 shot $Step …"
$sp.DiscardInBuffer()
$sp.Write("shot $Step`n")

# ---- 1) 找文件头 @@SHOT w h step stride ----
$hdr = ""
$deadline = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $deadline) {
    try { $hdr += $sp.ReadExisting() } catch {}
    if ($hdr -match '@@SHOT\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)') { break }
    Start-Sleep -Milliseconds 100
}
if ($hdr -notmatch '@@SHOT\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)') {
    $sp.Close()
    Write-Host "[shot] 没有收到 @@SHOT 头。收到的内容（尾部）:"
    Write-Host ($hdr.Substring([Math]::Max(0, $hdr.Length - 300)))
    exit 1
}
$w = [int]$Matches[1]; $h = [int]$Matches[2]; $st = [int]$Matches[3]; $stride = [int]$Matches[4]
$sw = [int][Math]::Ceiling($w / $st)
$sh = [int][Math]::Ceiling($h / $st)
$need = $sw * $sh * 2
Write-Host "[shot] 分辨率 ${w}x${h} 采样 1/$st → ${sw}x${sh}，需读取 $need 字节"

# ---- 2) 读像素 ----
$buf = New-Object byte[] $need
$got = 0
$deadline = (Get-Date).AddSeconds(180)
while ($got -lt $need -and (Get-Date) -lt $deadline) {
    try {
        $n = $sp.Read($buf, $got, [Math]::Min(4096, $need - $got))
        if ($n -gt 0) { $got += $n }
    } catch {}
}
try { [void]$sp.ReadExisting() } catch {}
$sp.Close()
Write-Host "[shot] 收到 $got / $need 字节"
if ($got -lt $need) { Write-Host "[shot] 数据不完整，仍按已收到部分生成"; }

# ---- 3) RGB565 → PNG ----
$rows = [Math]::Floor($got / ($sw * 2))
$imgH = [Math]::Max(1, [Math]::Min($sh, $rows))
$bmp = New-Object System.Drawing.Bitmap $sw, $imgH, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$rect = New-Object System.Drawing.Rectangle 0, 0, $sw, $imgH
$bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$outBytes = New-Object byte[] ($bd.Stride * $imgH)
for ($y = 0; $y -lt $imgH; $y++) {
    $rowOff = $y * $sw * 2
    $dstOff = $y * $bd.Stride
    for ($x = 0; $x -lt $sw; $x++) {
        $v = [BitConverter]::ToUInt16($buf, $rowOff + $x * 2)
        $r = [byte](((($v -shr 11) -band 0x1F) * 255) / 31)
        $g = [byte](((($v -shr 5) -band 0x3F) * 255) / 63)
        $b = [byte]((($v -band 0x1F) * 255) / 31)
        $o = $dstOff + $x * 3
        $outBytes[$o] = $b
        $outBytes[$o + 1] = $g
        $outBytes[$o + 2] = $r
    }
}
[System.Runtime.InteropServices.Marshal]::Copy($outBytes, 0, $bd.Scan0, $outBytes.Length)
$bmp.UnlockBits($bd)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "[shot] 已保存 $Out (${sw}x${imgH})"
