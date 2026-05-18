// Exports.cpp
// Public export forwarders - these call the static wrappers in Proxy.cpp
// The .def file maps version.dll API names to these functions
// This file must be compiled with /GF (string pooling) to avoid conflicts

#include <windows.h>

// Forward declarations of the static wrappers in Proxy.cpp
static BOOL WINAPI Wrap_GetFileVersionInfoW(LPCWSTR, DWORD, DWORD, LPVOID);
static BOOL WINAPI Wrap_GetFileVersionInfoA(LPCSTR, DWORD, DWORD, LPVOID);
static BOOL WINAPI Wrap_GetFileVersionInfoByHandle(DWORD, DWORD, LPVOID);
static BOOL WINAPI Wrap_GetFileVersionInfoExW(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
static DWORD WINAPI Wrap_GetFileVersionInfoSizeW(LPCWSTR, LPDWORD);
static DWORD WINAPI Wrap_GetFileVersionInfoSizeA(LPCSTR, LPDWORD);
static DWORD WINAPI Wrap_GetFileVersionInfoSizeExW(DWORD, LPCWSTR, LPDWORD);
static DWORD WINAPI Wrap_VerFindFileW(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
static DWORD WINAPI Wrap_VerFindFileA(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
static DWORD WINAPI Wrap_VerInstallFileW(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, PUINT);
static DWORD WINAPI Wrap_VerInstallFileA(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, PUINT);
static DWORD WINAPI Wrap_VerLanguageNameW(DWORD, LPWSTR, DWORD);
static DWORD WINAPI Wrap_VerLanguageNameA(DWORD, LPSTR, DWORD);
static BOOL WINAPI Wrap_VerQueryValueW(LPCVOID, LPCWSTR, LPVOID*, PUINT);
static BOOL WINAPI Wrap_VerQueryValueA(LPCVOID, LPCSTR, LPVOID*, PUINT);

// Public exports - these forward to the static wrappers
// extern "C" + __declspec(dllexport) to ensure C linkage and export

extern "C" {

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoW(LPCWSTR a, DWORD b, DWORD c, LPVOID d)
    { return Wrap_GetFileVersionInfoW(a,b,c,d); }

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoA(LPCSTR a, DWORD b, DWORD c, LPVOID d)
    { return Wrap_GetFileVersionInfoA(a,b,c,d); }

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoByHandle(DWORD a, DWORD b, LPVOID c)
    { return Wrap_GetFileVersionInfoByHandle(a,b,c); }

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoExW(DWORD a, LPCWSTR b, DWORD c, DWORD d, LPVOID e)
    { return Wrap_GetFileVersionInfoExW(a,b,c,d,e); }

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR a, LPDWORD b)
    { return Wrap_GetFileVersionInfoSizeW(a,b); }

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR a, LPDWORD b)
    { return Wrap_GetFileVersionInfoSizeA(a,b); }

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeExW(DWORD a, LPCWSTR b, LPDWORD c)
    { return Wrap_GetFileVersionInfoSizeExW(a,b,c); }

__declspec(dllexport) DWORD WINAPI VerFindFileW(DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPWSTR e, PUINT f, LPWSTR g, PUINT h)
    { return Wrap_VerFindFileW(a,b,c,d,e,f,g,h); }

__declspec(dllexport) DWORD WINAPI VerFindFileA(DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPSTR e, PUINT f, LPSTR g, PUINT h)
    { return Wrap_VerFindFileA(a,b,c,d,e,f,g,h); }

__declspec(dllexport) DWORD WINAPI VerInstallFileW(DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPCWSTR e, LPWSTR f, PUINT g, PUINT h)
    { return Wrap_VerInstallFileW(a,b,c,d,e,f,g,h); }

__declspec(dllexport) DWORD WINAPI VerInstallFileA(DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPCSTR e, LPSTR f, PUINT g, PUINT h)
    { return Wrap_VerInstallFileA(a,b,c,d,e,f,g,h); }

__declspec(dllexport) DWORD WINAPI VerLanguageNameW(DWORD a, LPWSTR b, DWORD c)
    { return Wrap_VerLanguageNameW(a,b,c); }

__declspec(dllexport) DWORD WINAPI VerLanguageNameA(DWORD a, LPSTR b, DWORD c)
    { return Wrap_VerLanguageNameA(a,b,c); }

__declspec(dllexport) BOOL WINAPI VerQueryValueW(LPCVOID a, LPCWSTR b, LPVOID* c, PUINT d)
    { return Wrap_VerQueryValueW(a,b,c,d); }

__declspec(dllexport) BOOL WINAPI VerQueryValueA(LPCVOID a, LPCSTR b, LPVOID* c, PUINT d)
    { return Wrap_VerQueryValueA(a,b,c,d); }

}