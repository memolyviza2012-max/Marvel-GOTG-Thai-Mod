; Exports.asm
; Assembly thunks for wrapping version.dll exports
; Based on SeanPesce/DXMD-Translations

.code

; Each export thunk preserves all registers, loads the real
; function address from export_locs array, and jumps to it

GetFileVersionInfoA_wrapper PROC
    push qword ptr [rcx + 8 * 0]     ; Save registers
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 0]
    add rsp, 28h
    pop rax
    jmp rax
GetFileVersionInfoA_wrapper ENDP

GetFileVersionInfoByHandle_wrapper PROC
    push qword ptr [rcx + 8 * 1]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 1]
    add rsp, 28h
    pop rax
    jmp rax
GetFileVersionInfoByHandle_wrapper ENDP

GetFileVersionInfoExW_wrapper PROC
    push qword ptr [rcx + 8 * 2]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 2]
    add rsp, 28h
    pop rax
    jmp rax
GetFileVersionInfoExW_wrapper ENDP

GetFileVersionInfoSizeA_wrapper PROC
    push qword ptr [rcx + 8 * 3]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 3]
    add rsp, 28h
    pop rax
    jmp rax
GetFileVersionInfoSizeA_wrapper ENDP

GetFileVersionInfoSizeExW_wrapper PROC
    push qword ptr [rcx + 8 * 4]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 4]
    add rsp, 28h
    pop rax
    jmp rax
GetFileVersionInfoSizeExW_wrapper ENDP

GetFileVersionInfoSizeW_wrapper PROC
    push qword ptr [rcx + 8 * 5]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 5]
    add rsp, 28h
    pop rax
    jmp rax
GetFileVersionInfoSizeW_wrapper ENDP

GetFileVersionInfoW_wrapper PROC
    push qword ptr [rcx + 8 * 6]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 6]
    add rsp, 28h
    pop rax
    jmp rax
GetFileVersionInfoW_wrapper ENDP

VerFindFileA_wrapper PROC
    push qword ptr [rcx + 8 * 7]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 7]
    add rsp, 28h
    pop rax
    jmp rax
VerFindFileA_wrapper ENDP

VerFindFileW_wrapper PROC
    push qword ptr [rcx + 8 * 8]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 8]
    add rsp, 28h
    pop rax
    jmp rax
VerFindFileW_wrapper ENDP

VerInstallFileA_wrapper PROC
    push qword ptr [rcx + 8 * 9]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 9]
    add rsp, 28h
    pop rax
    jmp rax
VerInstallFileA_wrapper ENDP

VerInstallFileW_wrapper PROC
    push qword ptr [rcx + 8 * 10]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 10]
    add rsp, 28h
    pop rax
    jmp rax
VerInstallFileW_wrapper ENDP

VerLanguageNameA_wrapper PROC
    push qword ptr [rcx + 8 * 11]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 11]
    add rsp, 28h
    pop rax
    jmp rax
VerLanguageNameA_wrapper ENDP

VerLanguageNameW_wrapper PROC
    push qword ptr [rcx + 8 * 12]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 12]
    add rsp, 28h
    pop rax
    jmp rax
VerLanguageNameW_wrapper ENDP

VerQueryValueA_wrapper PROC
    push qword ptr [rcx + 8 * 13]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 13]
    add rsp, 28h
    pop rax
    jmp rax
VerQueryValueA_wrapper ENDP

VerQueryValueW_wrapper PROC
    push qword ptr [rcx + 8 * 14]
    mov qword ptr [rsp + 8], rax
    sub rsp, 28h
    mov rax, qword ptr [export_locs + 8 * 14]
    add rsp, 28h
    pop rax
    jmp rax
VerQueryValueW_wrapper ENDP

END