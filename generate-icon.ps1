$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot 'assets') | Out-Null
$iconPath = Join-Path $PSScriptRoot 'assets\MiniPulse.ico'
$frames = @()
foreach ($size in @(16, 24, 32, 48, 64, 128, 256)) {
    $bitmap = New-Object System.Drawing.Bitmap($size, $size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $background = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(17, 22, 32))
    $outline = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(75, 205, 236), [single]($size * 0.045))
    $pulse = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(88, 223, 171), [single]($size * 0.065))
    $pulse.StartCap = $pulse.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pulse.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $graphics.FillEllipse($background, [single]($size * 0.04), [single]($size * 0.04), [single]($size * 0.92), [single]($size * 0.92))
    $graphics.DrawEllipse($outline, [single]($size * 0.07), [single]($size * 0.07), [single]($size * 0.86), [single]($size * 0.86))
    $points = [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new($size * 0.19, $size * 0.54),
        [System.Drawing.PointF]::new($size * 0.33, $size * 0.54),
        [System.Drawing.PointF]::new($size * 0.43, $size * 0.28),
        [System.Drawing.PointF]::new($size * 0.55, $size * 0.74),
        [System.Drawing.PointF]::new($size * 0.65, $size * 0.46),
        [System.Drawing.PointF]::new($size * 0.81, $size * 0.46)
    )
    $graphics.DrawLines($pulse, $points)
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $frames += [pscustomobject]@{ Size = $size; Bytes = $stream.ToArray() }
    $stream.Dispose()
    $pulse.Dispose()
    $outline.Dispose()
    $background.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}
$output = [System.IO.File]::Create($iconPath)
$writer = New-Object System.IO.BinaryWriter($output)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$frames.Count)
    $offset = 6 + 16 * $frames.Count
    foreach ($frame in $frames) {
        $dimension = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$frame.Bytes.Length)
        $writer.Write([uint32]$offset)
        $offset += $frame.Bytes.Length
    }
    foreach ($frame in $frames) { $writer.Write([byte[]]$frame.Bytes) }
} finally {
    $writer.Dispose()
    $output.Dispose()
}
Write-Host "Icon: $iconPath"
