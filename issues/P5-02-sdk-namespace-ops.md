# P5-02: C++ SDK Complete Namespace Operations

## Summary

Extend C++ SDK from MVP (Open/Read/Write/Close/Create/Complete) to full namespace support: Mkdir, Rmdir, ListDirectory, Rename, Exists.

## Current State

- SDK supports: `Create`, `Open`, `Delete`, `Stat` (on FluxCacheSDK)
- FileHandle supports: `Read`, `Write`, `Stat`, `Close`
- Header explicitly documents: "NOT supported in MVP (namespace APIs, to be added later)"
- Master RPCs will be available from P5-01

## Acceptance Criteria

1. `FluxCacheSDK::Mkdir(const std::string& path)` — create directory
2. `FluxCacheSDK::Rmdir(const std::string& path)` — remove empty directory
3. `FluxCacheSDK::ListDirectory(const std::string& path)` — return vector of DirEntry (name + metadata)
4. `FluxCacheSDK::Rename(const std::string& src, const std::string& dst)` — rename/move
5. `FluxCacheSDK::Exists(const std::string& path)` — check path existence
6. Unit tests for each new method
7. Update SDK header to remove "NOT supported in MVP" comment

## Non-Goals

- Recursive directory operations (Mkdirs, Rmtree)
- Permission management
- Extended attributes

## Dependencies

- P5-01 (Master proto namespace RPCs)

## Change Tier

P1
