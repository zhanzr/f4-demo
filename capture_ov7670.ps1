# Capture ov7670_to_lcd boot log: open COM42 (CH340 VCP), reset target via OpenOCD, print 12s of serial.
$ErrorActionPreference = 'Continue'
$openocd = 'C:\msys64\mingw64\bin\openocd.exe'
$ifcfg = 'C:/msys64/mingw64/share/openocd/scripts/interface/cmsis-dap.cfg'
$tgcfg = 'C:/msys64/mingw64/share/openocd/scripts/target/stm32f4x.cfg'
$outfile = 'd:\f4-demo\ov7670_boot_log.txt'
$tmpfile = 'd:\f4-demo\ov7670_boot_log_tmp.txt'
Remove-Item $tmpfile -ErrorAction SilentlyContinue

$job = Start-Job -ArgumentList $tmpfile {
    param($tf)
    $p = [System.IO.Ports.SerialPort]::new('COM42', 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $p.ReadTimeout = 6000
    $p.Open()
    $sw = [System.IO.StreamWriter]::new($tf, $true)
    try {
        while ($true) {
            $line = $p.ReadLine()
            $sw.WriteLine($line)
            $sw.Flush()
        }
    } catch { }
    $sw.Close()
    $p.Close()
}

Start-Sleep -Milliseconds 1000
& $openocd -f $ifcfg -f $tgcfg -c 'adapter speed 4000' -c 'init' -c 'reset run' -c 'shutdown' 2>$null | Out-Null
Start-Sleep -Seconds 20
Stop-Job $job -ErrorAction SilentlyContinue
Remove-Job $job -Force -ErrorAction SilentlyContinue
if (Test-Path $tmpfile) {
    $out = Get-Content $tmpfile -Raw
} else {
    $out = ''
}
Set-Content -Path $outfile -Value $out -Encoding UTF8
Write-Output "----- BOOT LOG ($($out.Length) chars) -----"
Write-Output $out
Write-Output "----- END -----"