# Marvel's Guardians of the Galaxy: Thai Localization Mod
## Project Architecture & Postmortem

This document serves as a comprehensive knowledge base (KI) for the technical implementation of the Thai localization mod for the Dawn Engine (*Marvel's Guardians of the Galaxy*).

### 1. System Architecture (DLL Proxying)
The core of the mod is a custom `version.dll` written in C++. 
When the game launches, the Windows Loader automatically loads our `version.dll` instead of the system's default version.dll. 
- **Export Forwarding**: We use `#pragma comment(linker, "/export:...")` to forward all standard version.dll calls (like `GetFileVersionInfoW`) to the real `C:\Windows\System32\version.dll`. This ensures the game runs normally.
- **Dependency Management**: We statically link the MSVC runtime (`/MT` flag) so end-users don't need to install any C++ Redistributable packages.

### 2. File Loading & CWD Safety
Initially, the mod used `SetCurrentDirectoryA` to force the game's Current Working Directory (CWD) to `bin\` (for Steam versions). This caused the game to crash because it couldn't find its own `.archive` data files.
**The Fix:** We completely removed CWD manipulation. The DLL now dynamically calculates its absolute path using `sp::env::lib_dir()` and strictly loads `strings_th.json` and `font_th.ttf` relative to the DLL's location. This makes the mod completely transparent and non-destructive to the game's native file mapping.

### 3. MinHook & Universal Cross-Platform RVAs
The Dawn Engine renders UI text using `ZTextFieldEntity::OnSetText` and `ZSubtitlesField::SetText`. To translate text on the fly, we detour these functions using **MinHook**.
Because the Steam executable (508 MB) and Epic Games executable (497 MB) have different internal structures, hardcoded Relative Virtual Addresses (RVAs) will cause immediate crashes if the wrong platform is loaded.

**The Fix:** We implemented a cross-platform RVA resolution system in `DllMain.cpp`.
We read the physical file size of `gotg.exe` via `std::filesystem::file_size()`. 
- If `gotg_size == 508186624`, it loads the **Steam** RVAs.
- Otherwise, it falls back to the **Epic Games** RVAs.
*(Note: We originally tried using the Virtual Module Size loaded in RAM, but Virtual Size varies due to PE section padding. Physical file size on disk is the correct metric).*

### 4. Memory Hook Targets
We successfully intercepted the following engine functions:
1. **`FT_New_Memory_Face`** (FreeType 2): Swaps the game's default byte buffer with our `font_th.ttf` memory buffer. Fixes Thai floating vowel issues.
2. **`ZSubtitlesField::SetText`**: Translates in-game subtitles perfectly.
3. **`ZTextFieldEntity::OnSetText` / `GetText`**: Translates all UI elements, menus, datalogs, and codex entries.

*(Note: We initially tried hooking `StringAlloc`, but this caused fatal heap corruption and game crashes due to multi-threaded thread contention. UI-level hooking via `OnSetText` is significantly safer).*

### 5. CRLF Mismatch Bug (The Codex Issue)
We encountered a bug where long, multi-paragraph text (like the "Ko-Rel" or "Drax" bios) would not translate. 
**Root Cause:** The Dawn Engine passes strings with Carriage Return + Line Feed (`\r\n`). Our Python scripts generated JSON dictionaries with standard Line Feeds (`\n`). This 4-byte mismatch caused the C++ dictionary lookup to fail.
**The Fix:** We added a real-time `\r` sanitizer in `lookup_translation` (`TranslationHooks.cpp`) which strips `\r` characters from the engine's query string before performing the `unordered_map` lookup.

### 6. Python Translation Pipeline
The repository relies on custom Python scripts (`extract_gotg_strings.py`, `run_translation_GOTG.py`) to parse game binaries, isolate text, and pass them to the DeepSeek AI API.
- **Tag Masking**: Game-specific markup (e.g., `<color=red>`, `{0}`) is temporarily replaced with generic HTML tags like `<a>` during translation, preventing the AI from corrupting the engine's formatting syntax.

### 7. False Positives (Antivirus)
Because the mod utilizes DLL Hijacking, Pattern Scanning, and MinHook memory modification, Windows Defender and VirusTotal heavily flag it as a Trojan/Injector. 
**Solution:** Open-source the entire C++ codebase on GitHub and provide an explicit "Note to Moderators" when publishing to NexusMods. 

**GitHub Repository:** `https://github.com/memolyviza2012-max/Marvel-GOTG-Thai-Mod`
