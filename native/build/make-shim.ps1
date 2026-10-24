$dir = 'D:\VS2022BT\VC\Tools\MSVC\14.44.35207\bin\Hostx64'
$shim = Join-Path $dir 'vcvars64.bat'
$content = "@echo off`r`ncall `"D:\VS2022BT\VC\Auxiliary\Build\vcvars64.bat`" %*`r`n"
Set-Content -Path $shim -Value $content -Encoding ascii
Write-Output "shim written: $shim"
