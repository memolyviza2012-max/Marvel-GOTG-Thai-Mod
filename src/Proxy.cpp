// Proxy.cpp
// Marvel's GOTG Thai Localization Mod - version.dll proxy
// Direct export forwarding to system32\version.dll via pragma linker comments
// DllMain lives in DllMain.cpp - do NOT define DllMain here

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// ============================================================================
// LoadSystemVersionDll - declared extern in DllMain.cpp
// ============================================================================

extern "C" int LoadSystemVersionDll(void)
{
    static HMODULE h = NULL;
    if (h != NULL) return 1;
    h = LoadLibraryW(L"C:\\Windows\\System32\\version.dll");
    if (h == NULL) h = LoadLibraryW(L"version.dll");
    return (h != NULL) ? 1 : 0;
}

// ============================================================================
// DIRECT EXPORT FORWARDING
// Format: /export:ExportName=targetDll.FunctionName
// Creates export table entries that jump directly to target DLL.
// ============================================================================

#pragma comment(linker, "/export:GetFileVersionInfoW=C:\\Windows\\System32\\version.GetFileVersionInfoW,@1")
#pragma comment(linker, "/export:GetFileVersionInfoA=C:\\Windows\\System32\\version.GetFileVersionInfoA,@2")
#pragma comment(linker, "/export:GetFileVersionInfoByHandle=C:\\Windows\\System32\\version.GetFileVersionInfoByHandle,@3")
#pragma comment(linker, "/export:GetFileVersionInfoExW=C:\\Windows\\System32\\version.GetFileVersionInfoExW,@4")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeW,@5")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeA,@6")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExW,@7")
#pragma comment(linker, "/export:VerFindFileW=C:\\Windows\\System32\\version.VerFindFileW,@8")
#pragma comment(linker, "/export:VerFindFileA=C:\\Windows\\System32\\version.VerFindFileA,@9")
#pragma comment(linker, "/export:VerInstallFileW=C:\\Windows\\System32\\version.VerInstallFileW,@10")
#pragma comment(linker, "/export:VerInstallFileA=C:\\Windows\\System32\\version.VerInstallFileA,@11")
#pragma comment(linker, "/export:VerLanguageNameW=C:\\Windows\\System32\\version.VerLanguageNameW,@12")
#pragma comment(linker, "/export:VerLanguageNameA=C:\\Windows\\System32\\version.VerLanguageNameA,@13")
#pragma comment(linker, "/export:VerQueryValueW=C:\\Windows\\System32\\version.VerQueryValueW,@14")
#pragma comment(linker, "/export:VerQueryValueA=C:\\Windows\\System32\\version.VerQueryValueA,@15")