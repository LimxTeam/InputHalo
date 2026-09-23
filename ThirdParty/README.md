# Third-party dependencies

InputHalo is proprietary software developed by LimxTeam for Foshan Yier Ranran
Technology Co., Ltd. Third-party components retain their own license terms.

| Component | Pinned version/source | Selected license |
| --- | --- | --- |
| spdlog | v1.15.3 | MIT |
| fmt | 11.2.0 bundled in spdlog | MIT with optional exception |
| nlohmann/json | v3.12.0 | MIT |
| FreeType | VER-2-14-3 | FreeType License (FTL) |
| HarfBuzz | 14.2.0 | Old MIT |
| Volk | Vulkan SDK 1.4.357.0 | MIT |
| Vulkan-Headers | v1.4.357 | MIT option for the used dual-licensed C headers |

Retained originals live in `licenses/`. FreeType's BDF, PCF, hash and bundled
zlib notices, plus HarfBuzz's Microsoft USE notice, are included.

Portions of this software are copyright © 2026 The FreeType Project
(https://freetype.org). All rights reserved.

Fonts are loaded from the Windows Fonts directory and are not redistributed.
Optional HarfBuzz raster/vector/GPU backends and external FreeType compression
and image dependencies remain disabled. Development tools and GPU drivers are
not shipped as application executables.

Run `Tools/Update-Notices.ps1` after changing licenses or component versions.
The resulting root `THIRD_PARTY_NOTICES.txt` is embedded in About and copied,
with the individual licenses, beside InputHalo.exe by the normal build.
