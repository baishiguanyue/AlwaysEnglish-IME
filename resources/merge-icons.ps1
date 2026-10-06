# Merge keyboard_16/24/32/48.ico into a single multi-size icon.ico
# Pure PowerShell + .NET (System.Drawing), no third-party deps.
# Usage:  powershell -ExecutionPolicy Bypass -File merge-icons.ps1

$base = Split-Path -Parent $MyInvocation.MyCommand.Path
$sizes = 16, 24, 32, 48

$entries = New-Object System.Collections.ArrayList
$images  = New-Object System.Collections.ArrayList

foreach ($s in $sizes) {
    $f = Join-Path $base ("keyboard_{0}.ico" -f $s)
    $bytes = [System.IO.File]::ReadAllBytes($f)

    # Single-ICO layout: 6-byte ICONDIR + 16-byte ICONDIRENTRY + image data
    # ICONDIRENTRY (file offsets 6..21):
    #   6  byte  Width
    #   7  byte  Height
    #   8  byte  ColorCount
    #   9  byte  Reserved
    #  10  WORD  Planes
    #  12  WORD  BitCount
    #  14  DWORD BytesInRes
    #  18  DWORD ImageOffset   (== 22 in a single-image ICO)
    $w       = $bytes[6]
    $h       = $bytes[7]
    $cc      = $bytes[8]
    $planes  = [BitConverter]::ToUInt16($bytes, 10)
    $bits    = [BitConverter]::ToUInt16($bytes, 12)
    $imgSize = [BitConverter]::ToUInt32($bytes, 14)

    $imgData = New-Object byte[] $imgSize
    [Buffer]::BlockCopy($bytes, 22, $imgData, 0, $imgSize)

    [void]$entries.Add(@{ W = $w; H = $h; ColorCount = $cc; Planes = $planes; Bits = $bits; Size = $imgSize })
    [void]$images.Add($imgData)
}

# Total length
$headerLen = 6 + 16 * $entries.Count
$total = $headerLen
foreach ($d in $images) { $total += $d.Length }

$out = New-Object byte[] $total
$pos = 0

# ICONDIR
[Buffer]::BlockCopy([BitConverter]::GetBytes([UInt16]0), 0, $out, $pos, 2); $pos += 2
[Buffer]::BlockCopy([BitConverter]::GetBytes([UInt16]1), 0, $out, $pos, 2); $pos += 2
[Buffer]::BlockCopy([BitConverter]::GetBytes([UInt16]$entries.Count), 0, $out, $pos, 2); $pos += 2

# ICONDIRENTRYs
$offset = $headerLen
for ($i = 0; $i -lt $entries.Count; $i++) {
    $e = $entries[$i]
    $out[$pos] = $e.W;           $pos += 1
    $out[$pos] = $e.H;           $pos += 1
    $out[$pos] = $e.ColorCount;  $pos += 1
    $out[$pos] = 0;              $pos += 1   # reserved
    [Buffer]::BlockCopy([BitConverter]::GetBytes([UInt16]$e.Planes), 0, $out, $pos, 2); $pos += 2
    [Buffer]::BlockCopy([BitConverter]::GetBytes([UInt16]$e.Bits),   0, $out, $pos, 2); $pos += 2
    [Buffer]::BlockCopy([BitConverter]::GetBytes([UInt32]$e.Size),   0, $out, $pos, 4); $pos += 4
    [Buffer]::BlockCopy([BitConverter]::GetBytes([UInt32]$offset),   0, $out, $pos, 4); $pos += 4
    $offset += $e.Size
}

# Image data
for ($i = 0; $i -lt $images.Count; $i++) {
    $d = $images[$i]
    [Buffer]::BlockCopy($d, 0, $out, $pos, $d.Length)
    $pos += $d.Length
}

$outFile = Join-Path $base 'icon.ico'
[System.IO.File]::WriteAllBytes($outFile, $out)
Write-Host ("merged: " + $outFile + " (" + (Get-Item $outFile).Length + " bytes)")
