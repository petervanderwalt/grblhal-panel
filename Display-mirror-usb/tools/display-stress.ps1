param([int]$Seconds = 300)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$screen = [System.Windows.Forms.Screen]::AllScreens | Where-Object { $_.Bounds.Width -eq 1024 -and $_.Bounds.Height -eq 600 } | Select-Object -First 1
if (-not $screen) { throw 'No 1024x600 monitor found' }
$form = New-Object System.Windows.Forms.Form
$form.Text = 'USB display moving-image test'
$form.FormBorderStyle = 'None'
$form.StartPosition = 'Manual'
$form.Bounds = $screen.Bounds
$form.TopMost = $true
$property = [System.Windows.Forms.Control].GetProperty('DoubleBuffered', [System.Reflection.BindingFlags]'Instance,NonPublic')
$property.SetValue($form, $true, $null)
$state = @{ frames = 0; started = [DateTime]::UtcNow }
$form.Add_Paint({
    param($sender, $event)
    $n = $state.frames
    for ($y = 0; $y -lt 600; $y += 30) {
        for ($x = 0; $x -lt 1024; $x += 32) {
            $color = [System.Drawing.Color]::FromArgb((($x + $n * 7) % 256), (($y + $n * 3) % 256), (($x + $y + $n * 11) % 256))
            $brush = New-Object System.Drawing.SolidBrush($color)
            $event.Graphics.FillRectangle($brush, $x, $y, 32, 30)
            $brush.Dispose()
        }
    }
    $event.Graphics.DrawString("USB display test - frame $n - drag here to test touch", $sender.Font, [System.Drawing.Brushes]::White, 20, 20)
    $state.frames++
})
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 60
$timer.Add_Tick({
    if (([DateTime]::UtcNow - $state.started).TotalSeconds -ge $Seconds) { $form.Close() }
    else { $form.Invalidate() }
})
$timer.Start()
try { [System.Windows.Forms.Application]::Run($form) }
finally { $timer.Dispose(); $form.Dispose() }
"Rendered $($state.frames) moving images in $Seconds seconds"
