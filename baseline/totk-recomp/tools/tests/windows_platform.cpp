// Native smoke test: link this with runtime/rt_core.cpp, then run the executable.
#include "rt.h"
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <filesystem>

int main() {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const u64 page = info.dwPageSize;
    void* memory = VirtualAlloc(nullptr, (SIZE_T)(2 * page), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    const u64 address = (u64)memory;
    assert(!rt_readable(0, 1));
    assert(!rt_readable(~0ull, 2));
    assert(rt_readable(address, 2 * page));
    DWORD old;
    assert(VirtualProtect((void*)(address + page), (SIZE_T)page, PAGE_READONLY, &old));
    assert(rt_readable(address + page - 8, 16)); // spans two different readable regions
    assert(VirtualProtect((void*)(address + page), (SIZE_T)page, PAGE_NOACCESS, &old));
    assert(!rt_readable(address + page, 1));
    assert(!rt_readable(address + page - 8, 16));
    assert(VirtualProtect((void*)(address + page), (SIZE_T)page, PAGE_READWRITE | PAGE_GUARD, &old));
    assert(!rt_readable(address + page, 1));
    assert(VirtualFree(memory, 0, MEM_RELEASE));
    assert(!rt_readable(address, 1));
    wchar_t executable[32768];
    assert(GetModuleFileNameW(nullptr, executable, 32768));
    assert(std::filesystem::equivalent(std::filesystem::u8path(rt_exe_dir()),
                                      std::filesystem::path(executable).parent_path()));
    std::puts("Windows platform tests passed");
}
