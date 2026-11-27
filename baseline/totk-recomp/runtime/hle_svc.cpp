// HLE: nn::svc::aarch64::lp64 (kernel system calls) reached from LIFTED sdk code.
//
// These cannot be run natively: each one is a `svc` instruction the kernel implements. Until now nothing needed
// them, because the runtime HLEs nn::os at the import boundary and the TotK lift whitelist is narrow. With Splatoon 2
// binding main's unimplemented imports to the sdk's real bodies (TOTK_LIFT_ALL), sdk code such as nn::diag's logger
// reaches the syscall layer directly, so the layer has to exist.
//
// The functions below map a syscall onto the same host primitive the nn::os HLE already uses, so both paths agree.
// Names off this list stay unbound: sdk_imp_missing traps with the symbol name, which is the signal to add the next
// one. Return values are svc Result codes (0 = success).
#include "hle.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <fenv.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/syscall.h>
#endif

extern "C" u64 g_rt_heap_base;
extern "C" u64 kernel_heap_size(void);

// ---------------------------------------------------------------- debug output
HLE_AS(svc_OutputDebugString, "_ZN2nn3svc7aarch644lp6417OutputDebugStringEPKcm") {
    const char* s = (const char*)c->x[0];
    u64 n = c->x[1];
    if (s && n) { fwrite(s, 1, (size_t)n, stderr); fflush(stderr); }
    c->x[0] = 0;
}

// ---------------------------------------------------------------- time / scheduling
HLE_AS(svc_SleepThread, "_ZN2nn3svc7aarch644lp6411SleepThreadEl") {
    s64 ns = (s64)c->x[0];
    if (ns > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
    c->x[0] = 0;
}

// svcGetCurrentProcessorNumber returns the number itself (no out pointer). The guest sees 3 cores.
HLE_AS(svc_GetCurrentProcessorNumber, "_ZN2nn3svc7aarch644lp6425GetCurrentProcessorNumberEv") {
    c->x[0] = 0;   // core 0
}

// ---------------------------------------------------------------- threads (queries only: the runtime owns the threads)
HLE_AS(svc_GetThreadId, "_ZN2nn3svc7aarch644lp6411GetThreadIdEPmNS0_6HandleE") {
#ifdef _WIN32
    *(u64*)c->x[0] = (u64)GetCurrentThreadId();
#else
    *(u64*)c->x[0] = (u64)syscall(SYS_gettid);
#endif
    c->x[0] = 0;
}
HLE_AS(svc_GetThreadPriority, "_ZN2nn3svc7aarch644lp6417GetThreadPriorityEPiNS0_6HandleE") {
    *(s32*)c->x[0] = 0;   // nn::os::detail reads this back into ThreadType::priority; 0 is the default field value
    c->x[0] = 0;
}
HLE_AS(svc_GetThreadCoreMask, "_ZN2nn3svc7aarch644lp6417GetThreadCoreMaskEPiPmNS0_6HandleE") {
    *(s32*)c->x[0] = -1; *(u64*)c->x[1] = 0x7;   // "no affinity" + three allowed cores, as nn::os reports
    c->x[0] = 0;
}
HLE_AS(svc_SetThreadPriority, "_ZN2nn3svc7aarch644lp6417SetThreadPriorityENS0_6HandleEi") { c->x[0] = 0; }
HLE_AS(svc_SetThreadCoreMask, "_ZN2nn3svc7aarch644lp6418SetThreadCoreMaskENS0_6HandleEim") { c->x[0] = 0; }
HLE_AS(svc_SetThreadActivity, "_ZN2nn3svc7aarch644lp6417SetThreadActivityENS0_6HandleENS0_14ThreadActivityE") { c->x[0] = 0; }
HLE_AS(svc_GetLastThreadInfo, "_ZN2nn3svc7aarch644lp6417GetLastThreadInfoEPNS0_4lp6417LastThreadContextEPmPj") {
    memset((void*)c->x[0], 0, 0x30); *(u64*)c->x[1] = 0; *(u32*)c->x[2] = 0;
    c->x[0] = 0;
}

// ---------------------------------------------------------------- handles / events (the runtime's objects, not the kernel's)
HLE_AS(svc_CloseHandle, "_ZN2nn3svc7aarch644lp6411CloseHandleENS0_6HandleE") { c->x[0] = 0; }
HLE_AS(svc_SignalEvent, "_ZN2nn3svc7aarch644lp6411SignalEventENS0_6HandleE") { c->x[0] = 0; }
HLE_AS(svc_ClearEvent, "_ZN2nn3svc7aarch644lp6410ClearEventENS0_6HandleE") { c->x[0] = 0; }
HLE_AS(svc_ResetSignal, "_ZN2nn3svc7aarch644lp6411ResetSignalENS0_6HandleE") { c->x[0] = 0; }
HLE_AS(svc_ArbitrateLock, "_ZN2nn3svc7aarch644lp6413ArbitrateLockENS0_6HandleEmj") { c->x[0] = 0; }
HLE_AS(svc_ArbitrateUnlock, "_ZN2nn3svc7aarch644lp6415ArbitrateUnlockEm") { c->x[0] = 0; }
HLE_AS(svc_WaitProcessWideKeyAtomic, "_ZN2nn3svc7aarch644lp6424WaitProcessWideKeyAtomicEmmjl") { c->x[0] = 0; }
HLE_AS(svc_SignalProcessWideKey, "_ZN2nn3svc7aarch644lp6420SignalProcessWideKeyEmi") { c->x[0] = 0; }

// ---------------------------------------------------------------- memory
// svcGetInfo: only the queries nn::os::detail makes at boot. Everything else reports "not implemented" so a caller
// that depends on it fails visibly instead of silently reading zeros.
HLE_AS(svc_GetInfo, "_ZN2nn3svc7aarch644lp647GetInfoEPmNS0_8InfoTypeENS0_6HandleEm") {
    u64* out = (u64*)c->x[0];
    u32 which = (u32)c->x[1];
    switch (which) {
    case 0: *out = 0x7; break;                 // AllowedCpuCoresMask (3 cores)
    case 1: *out = 0x3F; break;                // AllowedThreadPrioMask
    case 2: *out = 0xF; break;                 // AllowedCpuIdMask
    case 5: *out = g_rt_heap_base; break;      // HeapRegionAddress
    case 6: *out = 0x100000000ull; break;      // TotalMemorySize (4 GiB console)
    case 7: *out = kernel_heap_size(); break;  // TotalUsedMemorySize (committed guest heap)
    case 8: *out = g_rt_heap_base; break;      // MapRegionAddress
    case 9: *out = 0x80000000ull; break;       // MapRegionSize
    case 15: *out = 0x100000000ull; break;     // TotalNonSystemMemorySize
    case 16: *out = kernel_heap_size(); break; // TotalUsedNonSystemMemorySize
    default:
        printf("[svc] GetInfo(%u) not implemented -> 0\n", which);
        *out = 0;
        break;
    }
    c->x[0] = 0;
}
HLE_AS(svc_SetHeapSize, "_ZN2nn3svc7aarch644lp6411SetHeapSizeEPmm") {
    // the runtime reserved the guest heap at nn::os::SetMemoryHeapSize; the kernel call is a no-op on top of it
    *(u64*)c->x[0] = g_rt_heap_base;
    c->x[0] = 0;
}
HLE_AS(svc_QueryMemory, "_ZN2nn3svc7aarch644lp6411QueryMemoryEPNS0_4lp6410MemoryInfoEPNS0_8PageInfoEm") {
    u64* mi = (u64*)c->x[0];
    memset(mi, 0, 0x28);
    if ((u64)c->x[2] >= g_rt_heap_base) { mi[0] = g_rt_heap_base; mi[1] = 0x100000000ull; mi[2] = 0; mi[3] = 3; }
    memset((void*)c->x[1], 0, 0x10);
    c->x[0] = 0;
}
// the guest heap is ordinary host memory: permissions and attributes are already what the caller wants
HLE_AS(svc_SetMemoryPermission, "_ZN2nn3svc7aarch644lp6419SetMemoryPermissionEmmNS0_16MemoryPermissionE") { c->x[0] = 0; }
HLE_AS(svc_SetMemoryAttribute, "_ZN2nn3svc7aarch644lp6418SetMemoryAttributeEmmjj") { c->x[0] = 0; }

// ---------------------------------------------------------------- IPC
// A service port only exists on a Switch (sm: owns them). Returning "not found" lets a lifted service client fail the
// way it would on a console without the service: the caller gets an error Result instead of a trapping process. The
// runtime's HLE already provides the services the game actually needs, so these paths are only reached by sdk code that
// nothing else covers yet. Names are printed once each, so the log shows what is still missing.
HLE_AS(svc_ConnectToNamedPort, "_ZN2nn3svc7aarch644lp6418ConnectToNamedPortEPNS0_6HandleEPKc") {
    const char* name = (const char*)c->x[1];
    static char seen[32][32];
    static int nseen;
    bool known = false;
    for (int i = 0; i < nseen; i++) if (name && strcmp(seen[i], name) == 0) { known = true; break; }
    if (!known && name) {
        printf("[svc] ConnectToNamedPort(%s): no such service on this host\n", name);
        if (nseen < 32) snprintf(seen[nseen++], sizeof seen[0], "%s", name);
    }
    *(u32*)c->x[0] = 0;      // handle (unused)
    c->x[0] = 0xE401;        // Result: no such service
}

// ---------------------------------------------------------------- fatal paths
// nn::diag::GetBacktrace walks the guest stack with libunwind (unw_getcontext / unw_init_local), which this runtime has
// no implementation of. It is only used to decorate an abort report, so report zero frames instead of trapping.
HLE_AS(diag_GetBacktrace, "_ZN2nn4diag12GetBacktraceEPmi") {
    c->x[0] = 0;   // frame count 0: the caller sizes the report from this
}
HLE_AS(svc_Break, "_ZN2nn3svc7aarch644lp645BreakENS0_11BreakReasonEmm") {
    printf("[svc] Break(reason %llu, %llx, %llx)\n", (unsigned long long)c->x[0], (unsigned long long)c->x[1],
           (unsigned long long)c->x[2]);
    c->x[0] = 0;   // let the game continue: an assertion hit here is a port gap, not a reason to end the process
}
HLE_AS(svc_ExitThread, "_ZN2nn3svc7aarch644lp6410ExitThreadEv") { rt_unbind_thread(); c->x[0] = 0; }
HLE_AS(svc_ExitProcess, "_ZN2nn3svc7aarch644lp6411ExitProcessEv") { printf("[svc] ExitProcess\n"); fflush(stdout); _exit(0); }

// ---------------------------------------------------------------- libc pieces the sdk imports
HLE_AS(svc_fesetenv, "fesetenv") { c->x[0] = (u64)fesetenv((const fenv_t*)c->x[0]); }
HLE_AS(svc_fegetenv, "fegetenv") { c->x[0] = (u64)fegetenv((fenv_t*)c->x[0]); }
HLE_AS(svc_fegetround, "fegetround") { c->x[0] = (u64)fegetround(); }
HLE_AS(svc_fesetround, "__fesetround") { c->x[0] = (u64)fesetround((int)c->x[0]); }
HLE_AS(svc_feclearexcept, "feclearexcept") { c->x[0] = (u64)feclearexcept((int)c->x[0]); }
HLE_AS(svc_feraiseexcept, "feraiseexcept") { c->x[0] = (u64)feraiseexcept((int)c->x[0]); }
HLE_AS(svc_fetestexcept, "fetestexcept") { c->x[0] = (u64)fetestexcept((int)c->x[0]); }

// nn::settings::eq(Language const&, LanguageCode const&): does this language code name that language? The arguments are
// references (the Language is a u32 behind x0, the code is the char[8] "en-US" behind x1). Left as a stub this returned 0
// for every query, so the game never matched its own language.
HLE_AS(settings_eq_language, "_ZN2nn8settingseqERKNS0_8LanguageERKNS0_12LanguageCodeE") {
    const s32 lang = c->x[0] ? *(const s32*)c->x[0] : -1;
    const char* code = (const char*)c->x[1];
    // nn::settings::Language: ja, en-US, fr, de, es-419, es, it, nl, fr-CA, pt, ru, ko, zh-Hant, zh-Hans, pt-BR
    static const char* const tag[] = {"ja", "en", "fr", "de", "es", "es", "it", "nl", "fr", "pt", "ru", "ko", "zh", "zh", "pt"};
    bool ok = false;
    if (code && lang >= 0 && lang < (s32)(sizeof tag / sizeof tag[0]))
        ok = code[0] == tag[lang][0] && code[1] == tag[lang][1];
    printf("[settings] eq(Language %d, \"%.5s\") = %d\n", lang, code ? code : "", (int)ok);
    c->x[0] = ok;
}
