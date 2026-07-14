$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vs = 'E:\VS2022\Common7\Tools\VsDevCmd.bat'
if (!(Test-Path $vs)) { $vs = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat' }
if (!(Test-Path $vs)) { throw 'Visual Studio C++ build tools were not found.' }

$out = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $out | Out-Null
$cmd = '"' + $vs + '" -no_logo -arch=x86 && cd /d "' + $root + '" && cl /nologo /c /TC /Os /O1 /Gy /Gw /GS- /Zl /DWIN32 /D_WINDOWS src\floppy.c /Fo:dist\floppy.obj && link /nologo dist\floppy.obj /OUT:dist\FloppyDungeon.exe /SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup /NODEFAULTLIB /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED kernel32.lib user32.lib gdi32.lib winmm.lib'
cmd.exe /d /s /c $cmd
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
Remove-Item (Join-Path $out 'floppy.obj') -ErrorAction SilentlyContinue

$exe = Get-Item (Join-Path $out 'FloppyDungeon.exe')
$limit = 1440KB
if ($exe.Length -gt $limit) { throw "Executable is $($exe.Length) bytes, above the 1.44MB limit." }
"Built $($exe.FullName)"
"Size: $($exe.Length) bytes ({0:N2} KB), limit: $limit bytes" -f ($exe.Length / 1KB)
