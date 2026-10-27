# GLB Thumbnail Provider for Windows Explorer

[![Build](https://github.com/Sickops/glb-thumbnail-provider/actions/workflows/build.yml/badge.svg)](https://github.com/Sickops/glb-thumbnail-provider/actions)
[![MIT License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Windows 10/11 x64](https://img.shields.io/badge/Windows-10%20%7C%2011%20x64-0078D4)

[🇫🇷 Français](README.md) · 🇬🇧 English

Shows a **3D thumbnail for `.glb` files** (binary glTF 2.0) directly in Windows Explorer,
just like Explorer already does for `.3mf` files.

![Preview](docs/preview.png)

## Install
1. Go to [**Releases**](../../releases/latest).
2. Download and run **`GlbThumbnail_Setup_x.y.z.exe`** (administrator rights required).
3. Thumbnails show up in Explorer (use *Large / Extra large icons* view).

> **SmartScreen warning**: the installer is not code-signed. Click **More info → Run anyway**.
> Verify integrity with `SHA256SUMS.txt` (`certutil -hashfile file SHA256`) or build from source.

**Portable**: extract `GlbThumbnail_Portable_x64_x.y.z.zip` to a permanent folder and run `install.bat`
as administrator. Use `uninstall.bat` to remove. **Uninstall (installer)**: Settings → Apps → GLB Thumbnail Provider.

## Compatibility
Windows 10/11, 64-bit only. No dependencies (static C++ runtime). No GPU needed (software renderer).

## Supported
Self-contained `.glb`, node hierarchy and transforms, material colors, vertex colors, base color textures
(PNG/JPEG… via WIC), `MASK` alpha, `KHR_materials_unlit`, `KHR_materials_pbrSpecularGlossiness` (diffuse),
transparent background, anti-aliasing.

## Known limitations
Draco / meshopt compression and external `.gltf` + `.bin` are not supported; no animation/skinning, no full PBR
(normal/metal/emissive maps), no KTX2. Models above ~2.5 M triangles are subsampled.

## Troubleshooting
- **No thumbnail**: not in *List/Details* view; Folder Options → View → untick "Always show icons, never thumbnails".
- **Stale cache**: re-run `install.bat`, use Disk Cleanup → Thumbnails, or delete `%LocalAppData%\Microsoft\Windows\Explorer\thumbcache_*.db` with Explorer closed.
- **Another app owns `.glb`**: check `HKCR\.glb\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}` equals `{B7E1F4A2-6C3D-4E8A-9F15-2D7A8C0B3E64}`; reinstall if overwritten.
- **Empty thumbnail for one file**: likely Draco/meshopt-compressed or external `.bin`. Open an issue with the file.
- **Antivirus alert**: possible false positive (unsigned shell-extension DLL). Compare the hash or build from source.

## How it works
In-process COM DLL implementing `IInitializeWithStream` + `IThumbnailProvider`, registered under
`HKLM\SOFTWARE\Classes\.glb\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}`. Windows hosts it in `dllhost.exe`
(isolated from Explorer). The file is parsed and rasterized in software (z-buffer, orthographic camera, 2-3× supersampling)
into a premultiplied ARGB bitmap. No network access, no file writes. See [SECURITY.md](SECURITY.md).

## Build
Visual Studio 2019/2022 with *Desktop development with C++*. In **x64 Native Tools Command Prompt**:
`scripts\build.bat`, then `build\install.bat` (admin). Installer: Inno Setup 6.3+ → `ISCC installer\installer.iss`.
Renderer-only test on any OS: `g++ -std=c++17 -O2 tests/render_test.cpp -o render_test && ./render_test samples/test.glb`.

## Releasing
Update `CHANGELOG.md`, then `git tag v1.1.0 && git push origin v1.1.0` — GitHub Actions builds and publishes the release.

## License
[MIT](LICENSE)
