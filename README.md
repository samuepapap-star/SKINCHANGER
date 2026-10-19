# VSkin Premium — Standalone Catalog (C++20)

This Windows desktop application is a standalone VALORANT cosmetic catalog. **It does not modify VALORANT, unlock paid skins, or equip skins in live matches.**

## Build

Requires Windows 10/11, Visual Studio 2022 C++ Desktop workload, CMake, Git and an internet connection on the first build. Run `build.bat`; open `build/VSkinPremium.sln` or run `build/Release/VSkinPremium.exe`. CMake fetches Dear ImGui v1.91.9b and nlohmann/json v3.11.3.

## Usage

Use Dashboard > Sync complete skin catalog to fetch the public weapons catalog from valorant-api.com. Select a skin in Weapons, switch to Skin Changer, then click Load official artwork for an on-demand image from media.valorant-api.com. Choose variant and level; Apply Skin updates **only the local collection**. Settings and favorites persist under `%APPDATA%\VSkinPremium\config.json`. The raw catalog is cached alongside it.

## Limitations

Official artwork loading is synchronous and may briefly pause the UI on a slow network. The demo catalog is shown when there is no cached catalog. Paid-vs-free classification is not authoritative; public catalog items are not evidence of ownership. Real in-game rendering, skin ownership, and anti-cheat compatibility are not provided. Windows build and runtime have not been tested in this Linux environment.
