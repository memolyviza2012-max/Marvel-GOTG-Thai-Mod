// PatternScanner.cpp
// Implementation of pattern scanning utilities

#include "PatternScanner.h"
#include <algorithm>

void* PatternScanner::find_pattern(
    uint8_t* region_start,
    DWORD64 region_size,
    const uint8_t* pattern,
    DWORD64 pattern_len,
    const char* mask
)
{
    if (!region_start || !pattern || !mask || pattern_len == 0)
        return nullptr;

    DWORD64 max_search = region_size - pattern_len;
    for (DWORD64 i = 0; i <= max_search; i++)
    {
        if (matches_mask(region_start + i, pattern, mask, pattern_len))
        {
            return (void*)(region_start + i);
        }
    }
    return nullptr;
}

std::vector<void*> PatternScanner::find_all_patterns(
    uint8_t* region_start,
    DWORD64 region_size,
    const uint8_t* pattern,
    DWORD64 pattern_len,
    const char* mask
)
{
    std::vector<void*> results;
    if (!region_start || !pattern || !mask || pattern_len == 0)
        return results;

    DWORD64 max_search = region_size - pattern_len;
    for (DWORD64 i = 0; i <= max_search; i++)
    {
        if (matches_mask(region_start + i, pattern, mask, pattern_len))
        {
            results.push_back((void*)(region_start + i));
        }
    }
    return results;
}

void* PatternScanner::find_pattern_with_offset(
    uint8_t* region_start,
    DWORD64 region_size,
    const uint8_t* pattern,
    DWORD64 pattern_len,
    const char* mask,
    int offset_instruction_count,
    int offset_byte_offset
)
{
    void* base = find_pattern(region_start, region_size, pattern, pattern_len, mask);
    if (!base)
        return nullptr;

    // Simple instruction length estimation for x64:
    // Most instructions are 1-4 bytes, we approximate 4 bytes per instruction
    uint64_t target = (uint64_t)base + (offset_instruction_count * 4) + offset_byte_offset;
    return (void*)target;
}

bool PatternScanner::matches_mask(const uint8_t* data, const uint8_t* pattern, const char* mask, DWORD64 len)
{
    for (DWORD64 i = 0; i < len; i++)
    {
        if (mask[i] == 'x' && data[i] != pattern[i])
            return false;
        // '?' or any other char = wildcard, match anything
    }
    return true;
}

std::vector<PatternScanner::ScanResult> PatternScanner::scan_dawn_engine_strings(
    uint8_t* region_start,
    DWORD64 region_size
)
{
    std::vector<ScanResult> results;

    // Pattern definitions for Dawn Engine string hooks
    // These patterns target common string allocation/interception points
    // Note: 0x00 in pattern is placeholder for wildcard bytes (mask determines which are matched)

    struct {
        const char* name;
        uint8_t pattern[16];
        DWORD64 pattern_len;
        const char* mask;
        int instruction_offset; // instructions after pattern
        int byte_offset;         // byte offset in target instruction
    } patterns[] = {
        // Pattern: String allocation entry (push rsi; mov rdi,rsi; call qword ptr)
        {
            "string_alloc_entry",
            { 0x48, 0x89, 0xFE, 0x48, 0x89, 0xD7, 0xE8 },
            7,
            "xx???xx",
            0, 0
        },
        // Pattern: Memory manager retrieve (mov rax,[rcx+xxx])
        {
            "mem_mgr_retrieve",
            { 0x48, 0x8B, 0x01, 0x48, 0x85, 0xC0 },
            6,
            "xx?xxx",
            2, 1
        },
        // Pattern: UI text render call (sub rsp,xx; mov qword ptr [rsp+xx],rcx)
        {
            "ui_text_render",
            { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0x4C, 0x24 },
            8,
            "????????",
            3, 2
        },
        // Pattern: String equality check (test rax,rax; jz near) - using 0x00 as wildcard placeholder
        {
            "string_equality",
            { 0x48, 0x85, 0xC0, 0x74, 0x00, 0x48, 0x89 },
            7,
            "xxxx?xx",
            1, 0
        },
        // Pattern: Loading screen subtitle setup
        {
            "loading_subs",
            { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B },
            8,
            "??????xx",
            4, 1
        }
    };

    for (const auto& p : patterns)
    {
        void* result = find_pattern_with_offset(
            region_start, region_size,
            p.pattern, p.pattern_len, p.mask,
            p.instruction_offset, p.byte_offset
        );

        if (result)
        {
            ScanResult sr;
            sr.name = p.name;
            sr.address = result;
            sr.confidence = 0.7; // Base confidence for pattern match
            results.push_back(sr);
        }
    }

    return results;
}