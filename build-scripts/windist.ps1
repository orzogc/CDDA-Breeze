if (Test-path bindist) {
  rm -Force -Recurse bindist
}

mkdir bindist
cp cataclysm-tiles.exe bindist/cataclysm-tiles.exe
cp tools/format/json_formatter.exe bindist/json_formatter.exe

mkdir bindist/lang
Copy-Item -LiteralPath "lang/mo" -Destination "bindist/lang" -Recurse -Force

$extras = "data", "doc", "gfx", "LICENSE.txt", "README.md", "VERSION.txt"
ForEach ($extra in $extras) {
	Copy-Item -LiteralPath $extra -Destination bindist -Recurse -Force
}

# Compress-Archive ignores hidden files, which breaks mods that use dot-prefixed
# JSON filenames.  Use the .NET ZIP API so the distribution contains every file
# staged under bindist.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archivePath = Join-Path (Get-Location) "cataclysmdda-0.G.zip"
if (Test-Path -LiteralPath $archivePath) {
	Remove-Item -LiteralPath $archivePath -Force
}
[System.IO.Compression.ZipFile]::CreateFromDirectory(
	(Join-Path (Get-Location) "bindist"),
	$archivePath,
	[System.IO.Compression.CompressionLevel]::Optimal,
	$false
)
