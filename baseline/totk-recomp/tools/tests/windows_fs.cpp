// Exercise the actual filesystem HLE against a disposable fixture directory.
// Link with runtime/hle_fs.cpp and runtime/rt_core.cpp; set TOTK_ROMFS to the fixture.
#include "hle.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

static auto& imports() {
    static std::unordered_map<std::string, GuestFn> table;
    return table;
}
HleReg::HleReg(const char* name, GuestFn fn) { imports()[name] = fn; }
extern "C" uint64_t nvn_present_count() { return 0; }

static void call(const char* name, Ctx& c) {
    auto it = imports().find(name);
    assert(it != imports().end());
    it->second(&c);
    assert(c.x[0] == 0);
}

int main() {
    const char* root = std::getenv("TOTK_ROMFS");
    assert(root && *root);
    assert(std::filesystem::is_empty(root)); // Never overwrite a real asset tree.
    std::string path = std::string(root) + "/positional.bin";
    std::vector<u8> expected(8192);
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = (u8)(i * 37 + i / 256);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        assert(out);
        out.write((const char*)expected.data(), expected.size());
        assert(out);
    }
    Ctx c{};
    c.x[0] = (u64)"test";
    call("_ZN2nn2fs8MountRomEPKcPvm", c);
    u64 handle = 0;
    c.x[0] = (u64)&handle; c.x[1] = (u64)"test:/positional.bin"; c.x[2] = 1;
    call("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci", c);
    assert(handle);
    s64 length = -1;
    c.x[0] = (u64)&length; c.x[1] = handle;
    call("_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE", c);
    assert(length == (s64)expected.size());
    auto read = [&](size_t offset, size_t size) {
        std::vector<u8> actual(size, 0xcc);
        u64 done = ~0ull;
        Ctx local{};
        local.x[0] = (u64)&done; local.x[1] = handle; local.x[2] = offset;
        local.x[3] = (u64)actual.data(); local.x[4] = size;
        call("_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm", local);
        assert(done == size);
        assert(std::memcmp(actual.data(), expected.data() + offset, size) == 0);
    };
    read(0, expected.size());
    read(4093, 31);
    read(3, 1175);
    std::thread a([&] { for (int i = 0; i < 100; ++i) read(17, 1024); });
    std::thread b([&] { for (int i = 0; i < 100; ++i) read(4081, 2048); });
    a.join(); b.join();
    c.x[0] = handle;
    imports().at("_ZN2nn2fs9CloseFileENS0_10FileHandleE")(&c);

    const char* heap_source = std::getenv("TOTK_FS_TEST_HEAPDEF");
    if (heap_source && *heap_source) {
        const std::string heap_path = std::string(root) + "/System/HeapSize/Product.HeapDef.byml.zs";
        std::filesystem::create_directories(std::filesystem::path(heap_path).parent_path());
        std::filesystem::copy_file(heap_source, heap_path);
        std::ifstream input(heap_source, std::ios::binary);
        std::vector<u8> heap_expected((std::istreambuf_iterator<char>(input)), {});
        assert(!heap_expected.empty());

        u64 heap_handle = 0;
        c = Ctx{};
        c.x[0] = (u64)&heap_handle; c.x[1] = (u64)"test:/System/HeapSize/Product.HeapDef.byml.zs"; c.x[2] = 1;
        call("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci", c);
        s64 heap_size = -1;
        c = Ctx{}; c.x[0] = (u64)&heap_size; c.x[1] = heap_handle;
        call("_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE", c);
        assert(heap_size == (s64)heap_expected.size());
        std::vector<u8> heap_actual(heap_expected.size());
        u64 heap_read = 0;
        c = Ctx{}; c.x[0] = (u64)&heap_read; c.x[1] = heap_handle; c.x[2] = 0;
        c.x[3] = (u64)heap_actual.data(); c.x[4] = heap_actual.size();
        call("_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm", c);
        assert(heap_read == heap_expected.size());
        assert(heap_actual == heap_expected);
        c = Ctx{}; c.x[0] = heap_handle;
        imports().at("_ZN2nn2fs9CloseFileENS0_10FileHandleE")(&c);
        assert(std::filesystem::remove(heap_path));
        assert(std::filesystem::remove(std::filesystem::path(heap_path).parent_path()));
        assert(std::filesystem::remove(std::filesystem::path(root) / "System"));
    }
    assert(std::remove(path.c_str()) == 0);
    std::puts("Windows filesystem HLE tests passed");
}
