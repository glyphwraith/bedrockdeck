# bedrockdeck

A native Windows application for managing **Minecraft: Bedrock Edition**: packs, skins, worlds and mods, with a built-in mod loader.
One small `.exe`, no installer, no dependencies. Written in C++17 with plain Win32.

## Features
- **Drag and drop install**: drop `.mcpack`, `.mcaddon`, `.mcworld` or `.mctemplate` files onto the window
- Tabs for **resource packs, behavior packs, skin packs, worlds and mods**, with names, versions and sizes
- **Safe by design**: installs are all-or-nothing, existing worlds are never overwritten, malicious archives that try to write outside the Minecraft folder are rejected, and worlds are backed up automatically before they are deleted
- **Export worlds** as `.mcworld` files
- **Mods**: add `.dll` mods, tick the ones you want, and load them into the running game with one click
- Launch Minecraft from the app
- Command line for scripting (`bedrockdeck list`, `install`, `export`, ...)

## Download
Get the latest build from the [Releases page](../../releases), extract the zip, and double-click `bedrockdeck.exe`.
Windows may show a SmartScreen warning because the exe is unsigned: click **More info → Run anyway**.

## Using it
1. Run `bedrockdeck.exe`. If it can't find your Minecraft data folder it asks you to pick your `com.mojang` folder (also under **File → Set Minecraft folder**).
2. Drag pack, world or mod files onto the window, or click **Add files...**.
3. Select an item and click **Remove selected**, or **Export world...** on the Worlds tab.
4. To use mods: click **Launch Minecraft**, wait for the title screen, tick your mods in the **Mods** tab, then click **Inject enabled mods**.

### Command line
```
bedrockdeck list
bedrockdeck install MyPack.mcpack Cool.mcworld
bedrockdeck export "<world-folder>"
bedrockdeck remove resource "<folder>"
bedrockdeck mods add MyMod.dll
bedrockdeck mods on MyMod.dll
bedrockdeck inject
bedrockdeck paths
```
Options: `--dir <com.mojang folder>`, `--data <bedrockdeck folder>`.

## About mods
bedrockdeck does not include any mods. A mod is a DLL you add yourself, and it runs inside the game, so **only use mods from sources you trust**.
Many servers ban modified clients, so check a server's rules before joining with mods loaded. Practising offline in your own worlds is always safe.
The loader only ever targets `Minecraft.Windows.exe`, only loads DLLs from bedrockdeck's own mods folder, and asks you to confirm before loading.

## Build from source
```bash
cmake -B build
cmake --build build --config Release
```
Binary: `build/Release/bedrockdeck.exe` (Visual Studio) or `build/bedrockdeck`. On Linux/macOS only the command-line pack manager is built (useful for development); the window, launching and mod loading are Windows-only.

## How it works
- A from-scratch **DEFLATE decompressor** and **ZIP reader/writer** (packs and worlds are zip files)
- A tolerant **JSON parser** for `manifest.json` (accepts the comments and trailing commas Minecraft allows)
- A native **Win32 window** (tabs, list view, drag and drop, background threads so big worlds never freeze it)
- A Windows **mod loader**: grants the DLL read access for app packages, then loads it into the game with `LoadLibraryW` via a remote thread

## Layout
```
include/deck/inflate.hpp     DEFLATE decompressor
include/deck/zip.hpp         ZIP reader and writer
include/deck/json.hpp        JSON parser
include/deck/packs.hpp       find Minecraft folders, list/install/remove/export
include/deck/mods.hpp        mods folder and enabled list
include/deck/inject_win.hpp  launch + mod loader (Windows)
include/deck/app.hpp         settings and state shared by window and CLI
include/deck/gui_win.hpp     the application window (Windows)
src/main.cpp                 entry point and command line
```

## License
MIT
