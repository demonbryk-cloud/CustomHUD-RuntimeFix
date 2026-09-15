#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace {
    using u8 = std::uint8_t;
    using uptr = std::uintptr_t;

    HMODULE g_self = nullptr;
    void** g_shadowVtable = nullptr;

    void Debug(const char* text) {
        OutputDebugStringA(text);
    }

    bool IsReadableAddress(const void* p, std::size_t bytes = sizeof(void*)) {
        if (!p || bytes == 0) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & PAGE_NOACCESS) return false;
        if (mbi.Protect & PAGE_GUARD) return false;
        const uptr start = reinterpret_cast<uptr>(p);
        const uptr regionEnd = reinterpret_cast<uptr>(mbi.BaseAddress) + mbi.RegionSize;
        return start <= regionEnd && bytes <= regionEnd - start;
    }

    bool IsExecutableAddress(const u8* p, HMODULE module) {
        if (!module || !p) return false;
        const auto base = reinterpret_cast<const u8*>(module);
        if (!IsReadableAddress(base, sizeof(IMAGE_DOS_HEADER))) return false;
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        if (dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return false;
        if (!IsReadableAddress(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS32))) return false;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

        const auto sections = IMAGE_FIRST_SECTION(nt);
        const uptr addr = reinterpret_cast<uptr>(p);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto& s = sections[i];
            if ((s.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
            const uptr begin = reinterpret_cast<uptr>(base) + s.VirtualAddress;
            const uptr size = s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData;
            const uptr end = begin + size;
            if (addr >= begin && addr < end) return true;
        }
        return false;
    }

    int FindUnitsGlobals(HMODULE hud, uptr* outGlobals, int maxGlobals) {
        if (!hud || !outGlobals || maxGlobals <= 0) return 0;
        const auto base = reinterpret_cast<const u8*>(hud);
        if (!IsReadableAddress(base, sizeof(IMAGE_DOS_HEADER))) return 0;
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        if (dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return 0;
        if (!IsReadableAddress(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS32))) return 0;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

        const auto sections = IMAGE_FIRST_SECTION(nt);
        int found = 0;
        for (WORD si = 0; si < nt->FileHeader.NumberOfSections; ++si) {
            const auto& s = sections[si];
            if ((s.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
            const u8* begin = base + s.VirtualAddress;
            const std::size_t size = s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData;

            for (std::size_t i = 0; i + 12 <= size; ++i) {
                const u8* p = begin + i;
                if (p[0] != 0x8B || p[1] != 0x0D) continue; // mov ecx,[global]

                bool hasUnitsCall = false;
                for (std::size_t j = 6; j + 7 <= 40 && i + j + 7 <= size; ++j) {
                    if (p[j] == 0x8B && p[j + 1] == 0x01 &&
                        p[j + 2] == 0x8B && p[j + 3] == 0x40 &&
                        p[j + 4] == 0x40 && p[j + 5] == 0xFF &&
                        p[j + 6] == 0xD0) {
                        hasUnitsCall = true;
                        break;
                    }
                }
                if (!hasUnitsCall) continue;

                uptr globalAddress = 0;
                std::memcpy(&globalAddress, p + 2, sizeof(globalAddress));
                if (!globalAddress) continue;

                bool duplicate = false;
                for (int k = 0; k < found && k < maxGlobals; ++k) {
                    if (outGlobals[k] == globalAddress) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;

                if (found < maxGlobals) outGlobals[found] = globalAddress;
                ++found;
                if (found >= maxGlobals) return found;
            }
        }
        return found;
    }

    // CustomHUD's units check is a member-style x86 call. Returning true forces
    // CustomHUD's own metric path, which also selects its existing km/h artwork.
    bool __fastcall ForceMetricUnits(void*, void*) {
        return true;
    }

    bool PatchUnits(HMODULE hud) {
        uptr globals[8]{};
        const int count = FindUnitsGlobals(hud, globals, 8);
        if (count < 1) return false;

        // All known CustomHUD 1.8.3 unit call sites point at the same global.
        for (int i = 1; i < count; ++i) {
            if (globals[i] != globals[0]) return false;
        }

        auto objectPtr = reinterpret_cast<void**>(globals[0]);
        if (!IsReadableAddress(objectPtr, sizeof(void*))) return false;

        void* object = nullptr;
        for (int attempt = 0; attempt < 300; ++attempt) {
            if (!IsReadableAddress(objectPtr, sizeof(void*))) return false;
            object = *objectPtr;
            if (object && IsReadableAddress(object, sizeof(void*))) break;
            object = nullptr;
            Sleep(100);
        }
        if (!object) return false;

        auto vtable = *reinterpret_cast<void***>(object);
        if (!vtable || !IsReadableAddress(vtable, 17 * sizeof(void*))) return false;

        // +0x40 is the unit-check virtual method used by CustomHUD's speed conversions.
        void* original = vtable[16];
        if (!IsExecutableAddress(reinterpret_cast<const u8*>(original), hud)) return false;

        // Validate a conservative run of the existing vtable before copying it.
        constexpr std::size_t kMaxEntries = 32;
        std::size_t entries = 0;
        for (std::size_t i = 0; i < kMaxEntries; ++i) {
            if (!IsReadableAddress(&vtable[i], sizeof(void*))) break;
            void* fn = vtable[i];
            if (!fn || !IsExecutableAddress(reinterpret_cast<const u8*>(fn), hud)) break;
            ++entries;
        }
        if (entries < 17) return false;

        const SIZE_T bytes = entries * sizeof(void*);
        auto shadow = static_cast<void**>(VirtualAlloc(
            nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!shadow) return false;

        std::memcpy(shadow, vtable, bytes);
        shadow[16] = reinterpret_cast<void*>(&ForceMetricUnits);
        g_shadowVtable = shadow;

        DWORD oldProtect = 0;
        if (!VirtualProtect(object, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
            VirtualFree(shadow, 0, MEM_RELEASE);
            g_shadowVtable = nullptr;
            return false;
        }

        *reinterpret_cast<void***>(object) = shadow;

        DWORD ignored = 0;
        VirtualProtect(object, sizeof(void*), oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), object, sizeof(void*));
        return true;
    }

    DWORD WINAPI Worker(LPVOID) {
        // Give the game and ASI loader time to finish startup.
        Sleep(2000);

        HMODULE hud = nullptr;
        for (int attempt = 0; attempt < 300; ++attempt) {
            hud = GetModuleHandleA("CustomHud.asi");
            if (hud) break;
            Sleep(100);
        }
        if (!hud) {
            Debug("[SpeedKMH] CustomHud.asi not found.\r\n");
            return 0;
        }

        if (PatchUnits(hud)) {
            Debug("[SpeedKMH] Metric units forced through CustomHUD unit check.\r\n");
        } else {
            Debug("[SpeedKMH] Patch not applied.\r\n");
        }
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
