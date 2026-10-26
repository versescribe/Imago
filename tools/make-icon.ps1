# Regenerate the geometric application icon without third-party packages.
$ErrorActionPreference = 'Stop'
$destination = Join-Path (Split-Path -Parent $PSScriptRoot) 'resources\imago.ico'
$sizes = @(16, 24, 32, 48, 64)
$images = @()
foreach ($size in $sizes) {
    $stream = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($stream)
    $maskStride = [int]([Math]::Ceiling($size / 32.0) * 4)
    $writer.Write([uint32]40)
    $writer.Write([int]$size)
    $writer.Write([int]($size * 2))
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]0)
    $writer.Write([uint32]($size * $size * 4 + $maskStride * $size))
    1..4 | ForEach-Object { $writer.Write([uint32]0) }
    for ($y = $size - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $size; $x++) {
            $alpha = 0; $white = 0
            for ($sy = 0; $sy -lt 4; $sy++) {
                for ($sx = 0; $sx -lt 4; $sx++) {
                    $u = ($x + ($sx + 0.5) / 4) / $size
                    $v = ($y + ($sy + 0.5) / 4) / $size
                    $dx = [Math]::Max([Math]::Abs($u - 0.5) - 0.27, 0)
                    $dy = [Math]::Max([Math]::Abs($v - 0.5) - 0.27, 0)
                    if ($dx * $dx + $dy * $dy -le 0.18 * 0.18) {
                        $alpha++
                        $vertical = (($u -ge 0.25 -and $u -le 0.32) -or ($u -ge 0.68 -and $u -le 0.75)) -and (($v -ge 0.25 -and $v -le 0.43) -or ($v -ge 0.57 -and $v -le 0.75))
                        $horizontal = (($v -ge 0.25 -and $v -le 0.32) -or ($v -ge 0.68 -and $v -le 0.75)) -and (($u -ge 0.25 -and $u -le 0.43) -or ($u -ge 0.57 -and $u -le 0.75))
                        if ($vertical -or $horizontal) { $white++ }
                    }
                }
            }
            $mix = if ($alpha) { $white / [double]$alpha } else { 0 }
            $writer.Write([byte][Math]::Round(192 + (255 - 192) * $mix))
            $writer.Write([byte][Math]::Round(103 + (255 - 103) * $mix))
            $writer.Write([byte][Math]::Round(255 * $mix))
            $writer.Write([byte][Math]::Round(255 * $alpha / 16))
        }
    }
    $writer.Write([byte[]]::new($maskStride * $size))
    $writer.Flush()
    $images += ,$stream.ToArray()
    $writer.Dispose(); $stream.Dispose()
}
$output = [System.IO.File]::Create($destination)
$icon = [System.IO.BinaryWriter]::new($output)
try {
    $icon.Write([uint16]0); $icon.Write([uint16]1); $icon.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $icon.Write([byte]$sizes[$i]); $icon.Write([byte]$sizes[$i]); $icon.Write([byte]0); $icon.Write([byte]0)
        $icon.Write([uint16]1); $icon.Write([uint16]32)
        $icon.Write([uint32]$images[$i].Length); $icon.Write([uint32]$offset)
        $offset += $images[$i].Length
    }
    foreach ($image in $images) { $icon.Write([byte[]]$image) }
} finally { $icon.Dispose(); $output.Dispose() }
