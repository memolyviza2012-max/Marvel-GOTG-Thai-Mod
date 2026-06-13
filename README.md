# Marvel's Guardians of the Galaxy: Thai Localization Mod 🇹🇭

This repository contains the source code for the Thai Translation Mod for **Marvel's Guardians of the Galaxy** (Dawn Engine). 
It utilizes a custom proxy DLL (`version.dll`) and API Hooking to inject a custom TrueType font and translate in-game text seamlessly.

---

## ⚠️ Antivirus / False Positive Warning (For NexusMods & Users)
**Why does VirusTotal / Antivirus flag this file?**
This mod functions as a **DLL Proxy** (`version.dll`) that intercepts the game's execution. It utilizes **MinHook** for API detouring and scans the game's memory (Pattern Scanning) to dynamically locate rendering functions. 
Because these techniques are functionally identical to how game trainers or overlays work, heuristic antivirus engines may incorrectly flag the compiled `version.dll` as a "Generic Trojan" or "Malware Injector" (False Positive).

**This project is 100% open-source and safe.** 
Moderators and users are encouraged to review the C++ source code in this repository. There is no malicious code, no internet connection established, and no file manipulation outside of the game's localization scope.

---

## 🌟 Features
*   **Dynamic Pattern Scanning:** Automatically finds engine offsets in memory, making it highly resilient to game updates.
*   **FreeType Font Swapping:** Intercepts `FT_New_Memory_Face` to dynamically swap the game's default font buffers with a custom Thai font (`font_th.ttf`) at runtime, fixing character spacing and floating vowel issues.
*   **Multi-layered Text Hooking:** Uses MinHook to intercept:
    *   `ZSubtitlesField::SetText` (Subtitles)
    *   `ZTextFieldEntity::OnSetText` and `GetText` (UI, Menus, Codex, Lore)
*   **CRLF Normalization:** Resolves Dawn Engine's `\r\n` vs JSON's `\n` mismatches to support multi-paragraph Codex entries perfectly.

## 🛠️ Build Instructions
To compile the proxy DLL yourself:
1. Ensure you have **Microsoft Visual Studio 2022** (with C++ Desktop Development and MSVC v143 build tools) installed.
2. Clone this repository.
3. Run the `build_cli.bat` script via the Developer Command Prompt, or open the Visual Studio Solution (`GOTG_LanguageTranslation.sln`) and build it in **Release x64** mode.
4. The output `version.dll` is compiled with the `/MT` flag (statically linked MSVC runtime), meaning end-users **do not** need to install any MSVC Redistributable packages.

## 📦 Installation for Players
1. Download the release archive.
2. Extract the files (`version.dll`, `strings_th.json`, `font_th.ttf`) into your game directory: 
   `...\MarvelGOTG\retail\`
3. Launch the game. (If your antivirus flags the file, please add it to your exclusions).

---
## 🇹🇭 สำหรับผู้เล่นชาวไทย
นี่คือ Source Code ของม็อดภาษาไทยครับ หากโปรแกรมสแกนไวรัสหรือ Windows Defender เด้งเตือนว่าไฟล์ `version.dll` เป็นไวรัส **นั่นคือการแจ้งเตือนผิดพลาด (False Positive)** 
เนื่องจากเทคนิคการแทรกภาษาไทยที่เราใช้นั้น ต้องทำการอ่านและเขียนหน่วยความจำของเกม (คล้ายกับการทำงานของโปรแกรมโกงเกม) ทำให้แอนตี้ไวรัสมองว่าน่าสงสัยครับ แต่ปลอดภัย 100% แน่นอน!


## 📝 Recent Updates
*   **อัปเดตไฟล์แปลภาษา (JSON):** เพิ่มเนื้อหาคำแปลภาษาไทยใหม่ลงในเกมมากกว่า **5,000 บรรทัด**! (อัปเดตล่าสุด)
