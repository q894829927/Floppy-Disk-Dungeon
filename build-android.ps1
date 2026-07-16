$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = Join-Path $root 'android'
$out = Join-Path $project 'build'
$dist = Join-Path $root 'dist'

$sdkCandidates = @(
    $env:ANDROID_SDK_ROOT,
    $env:ANDROID_HOME,
    "$env:LOCALAPPDATA\Android\Sdk"
) | Where-Object { $_ -and (Test-Path $_) }
if (!$sdkCandidates) { throw '没有找到 Android SDK。请设置 ANDROID_SDK_ROOT。' }
$sdk = $sdkCandidates[0]

$tools = Get-ChildItem (Join-Path $sdk 'build-tools') -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'aapt2.exe') } |
    Sort-Object { [version]$_.Name } -Descending
if (!$tools) { throw 'Android SDK 中没有可用的 build-tools。' }

$tool = $tools | Select-Object -First 1
$bt = $tool.FullName

$platform = Get-ChildItem (Join-Path $sdk 'platforms') -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'android.jar') } |
    Sort-Object { if ($_.Name -match 'android-(\d+)') { [int]$Matches[1] } else { 0 } } -Descending |
    Select-Object -First 1
if (!$platform) { throw 'Android SDK 中没有可用的平台 android.jar。' }
$androidJar = Join-Path $platform.FullName 'android.jar'

$androidStudioJdk = 'C:\Program Files\Android\Android Studio\jbr'
$jdk = if (Test-Path "$androidStudioJdk\bin\javac.exe") {
    $androidStudioJdk
} elseif ($env:JAVA_HOME -and (Test-Path "$env:JAVA_HOME\bin\javac.exe")) {
    $env:JAVA_HOME
} elseif (Test-Path 'E:\java\jdk-11\bin\javac.exe') {
    'E:\java\jdk-11'
} else {
    throw '没有找到包含 javac、jar 和 keytool 的 JDK。'
}
$env:JAVA_HOME = $jdk

$resolvedRoot = [IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
$resolvedOut = [IO.Path]::GetFullPath($out)
if (!$resolvedOut.StartsWith($resolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw '构建目录不在项目工作区内，拒绝清理。'
}
if (Test-Path -LiteralPath $resolvedOut) { Remove-Item -LiteralPath $resolvedOut -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $out 'classes'), (Join-Path $out 'dex'), $dist | Out-Null

$javac = Join-Path $jdk 'bin\javac.exe'
$jar = Join-Path $jdk 'bin\jar.exe'
$keytool = Join-Path $jdk 'bin\keytool.exe'
$aapt2 = Join-Path $bt 'aapt2.exe'
$d8 = Join-Path $bt 'd8.bat'
$zipalign = Join-Path $bt 'zipalign.exe'
$apksigner = Join-Path $bt 'apksigner.bat'
$manifest = Join-Path $project 'AndroidManifest.xml'
$source = Join-Path $project 'src\com\ruoxin\floppydungeon\MainActivity.java'
$unsigned = Join-Path $out 'unsigned.apk'
$unaligned = Join-Path $out 'with-dex.apk'
$aligned = Join-Path $out 'aligned.apk'
$apk = Join-Path $dist 'FloppyDungeon-Android.apk'
$keystore = Join-Path $project 'debug.keystore'

& $javac -encoding UTF-8 -source 8 -target 8 -g:none -classpath $androidJar -d (Join-Path $out 'classes') $source
if ($LASTEXITCODE -ne 0) { throw 'Java 编译失败。' }

$classFiles = Get-ChildItem (Join-Path $out 'classes') -Recurse -Filter '*.class' | ForEach-Object FullName
& $d8 --min-api 23 --release --lib $androidJar --output (Join-Path $out 'dex') $classFiles
if ($LASTEXITCODE -ne 0) { throw 'D8 生成 DEX 失败。' }

& $aapt2 link -o $unsigned --manifest $manifest -I $androidJar --min-sdk-version 23 --target-sdk-version 34
if ($LASTEXITCODE -ne 0) { throw 'AAPT2 生成 APK 失败。' }

Copy-Item $unsigned $unaligned
Push-Location (Join-Path $out 'dex')
try { & $jar uf $unaligned 'classes.dex' } finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw '写入 classes.dex 失败。' }

& $zipalign -p -f 4 $unaligned $aligned
if ($LASTEXITCODE -ne 0) { throw 'zipalign 失败。' }

if (!(Test-Path $keystore)) {
    & $keytool -genkeypair -keystore $keystore -storepass android -alias androiddebugkey -keypass android `
        -dname 'CN=Android Debug,O=Floppy Dungeon,C=CN' -keyalg RSA -keysize 2048 -validity 10000
    if ($LASTEXITCODE -ne 0) { throw '生成调试签名失败。' }
}

Copy-Item $aligned $apk -Force
& $apksigner sign --v4-signing-enabled false --ks $keystore --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey $apk
if ($LASTEXITCODE -ne 0) { throw 'APK 签名失败。' }
& $apksigner verify --verbose $apk
if ($LASTEXITCODE -ne 0) { throw 'APK 签名验证失败。' }

$file = Get-Item $apk
$limit = 1440KB
if ($file.Length -gt $limit) { throw "APK 大小为 $($file.Length) 字节，超过 1.44MB 限制。" }

"Android APK 构建完成：$($file.FullName)"
"大小：$($file.Length) 字节（{0:N2} KB），上限：$limit 字节" -f ($file.Length / 1KB)



