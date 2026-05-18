// PatternScanner.h
// Pattern scanning utility for finding byte patterns in memory
// Based on SeanPesce/DXMD-Translations

#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

class PatternScanner {
public:
  // Find a byte pattern in memory
  // pattern: byte array to search for
  // pattern_mask: mask string where 'x' = exact match, '?' = wildcard
  //              e.g., "48 89 ?? ?? E8" with mask "xx??xx???"
  static void *find_pattern(uint8_t *region_start, DWORD64 region_size,
                            const uint8_t *pattern, DWORD64 pattern_len,
                            const char *mask);

  // Find a pattern and return ALL matches
  static std::vector<void *> find_all_patterns(uint8_t *region_start,
                                               DWORD64 region_size,
                                               const uint8_t *pattern,
                                               DWORD64 pattern_len,
                                               const char *mask);

  // Find an instruction relative to a pattern match
  static void *find_pattern_with_offset(
      uint8_t *region_start, DWORD64 region_size, const uint8_t *pattern,
      DWORD64 pattern_len, const char *mask,
      int offset_instruction_count, // how many instructions after pattern
      int offset_byte_offset        // byte offset within target instruction
  );

  // Scan for multiple common Dawn Engine string patterns
  struct ScanResult {
    std::string name;
    void *address;
    double confidence; // 0.0 - 1.0
  };
  static std::vector<ScanResult> scan_dawn_engine_strings(uint8_t *region_start,
                                                          DWORD64 region_size);

private:
  static bool matches_mask(const uint8_t *data, const uint8_t *pattern,
                           const char *mask, DWORD64 len);
};