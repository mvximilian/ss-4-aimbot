$ErrorActionPreference = 'Stop'
$tag = 'v1.92.9b'
$root = Join-Path $PSScriptRoot 'third_party\imgui'
$files = @(
 'imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui.h','imgui_internal.h','imconfig.h','imstb_rectpack.h','imstb_textedit.h','imstb_truetype.h',
 'backends/imgui_impl_win32.cpp','backends/imgui_impl_win32.h','backends/imgui_impl_dx11.cpp','backends/imgui_impl_dx11.h'
)
New-Item -ItemType Directory -Force -Path $root,(Join-Path $root 'backends') | Out-Null
foreach($f in $files){
  $dest = Join-Path $root ($f -replace '/', '\')
  $url = "https://raw.githubusercontent.com/ocornut/imgui/$tag/$f"
  Write-Host "Downloading $f"
  Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $dest
}
Write-Host "Dear ImGui $tag downloaded."
