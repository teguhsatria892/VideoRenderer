#include <windows.h>
#include <atlbase.h>
#include <cstdio>
#include "../Include/FilterInterfaces.h"
#include "../Source/AmbilightSettings.h"
#pragma comment(lib, "ole32.lib")
static int failures = 0;
static void expect(bool ok, const char* what) { if (!ok) { printf("FAIL: %s\n", what); ++failures; } }
// Redirect HKCU only inside this process to validate first-run defaults without
// reading, changing, or deleting the user's actual renderer preferences.
class FreshUserRegistry {
    HKEY key = nullptr;
    wchar_t path[128] = {};
public:
    bool Open() {
        swprintf_s(path, L"Software\\MPC-HC Ambilight Tests\\Defaults-%lu", GetCurrentProcessId());
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, REG_OPTION_VOLATILE,
                            KEY_ALL_ACCESS, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
        return RegOverridePredefKey(HKEY_CURRENT_USER, key) == ERROR_SUCCESS;
    }
    ~FreshUserRegistry() {
        if (key) {
            RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            RegCloseKey(key);
            RegDeleteTreeW(HKEY_CURRENT_USER, path);
        }
    }
};
#pragma comment(lib, "advapi32.lib")
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) return 2;
    const bool freshDefaults = argc == 3 && wcscmp(argv[2], L"--defaults") == 0;
    FreshUserRegistry freshRegistry;
    if (freshDefaults && !freshRegistry.Open()) return 1;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    auto getClass = (HRESULT(WINAPI*)(REFCLSID, REFIID, void**))GetProcAddress(dll, "DllGetClassObject");
    if (!getClass) return 1;
    CLSID clsid;
    CLSIDFromString(L"{71F080AA-8661-4093-B15E-4F6903E77D0A}", &clsid);
    {
        CComPtr<IClassFactory> factory;
        HRESULT hr = getClass(clsid, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return 1;
        CComPtr<IExFilterConfig> config;
        hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&config));
        if (FAILED(hr)) { printf("CreateInstance: %08lx\n", hr); return 1; }
        bool enabled;
        int intensity, blur;
        expect(config->Flt_GetBool("ambilightEnabled", &enabled) == S_OK, "read enable");
        expect(config->Flt_GetInt("ambilightIntensity", &intensity) == S_OK, "read intensity");
        expect(config->Flt_GetInt("ambilightBlur", &blur) == S_OK, "read blur");
        if (freshDefaults) {
            expect(enabled == AmbilightSettings{}.enabled, "first-run enabled default");
            for (const auto& definition : AmbilightSettingDefinitions) {
                int initial = -999;
                expect(config->Flt_GetInt(definition.key, &initial) == S_OK, "read first-run setting");
                expect(initial == definition.initial, "first-run preset matches screenshot");
                expect(initial == AmbilightSettings{}.*(definition.member), "protocol defaults agree");
            }
        }
        expect(config->Flt_GetInt("ambilightBlur", nullptr) == E_POINTER, "null result rejected");
        expect(config->Flt_SetInt("ambilightIntensity", -1) == E_INVALIDARG, "negative intensity rejected");
        expect(config->Flt_SetInt("ambilightBlur", 101) == E_INVALIDARG, "blur above 100 rejected");
        int readback;
        expect(config->Flt_SetInt("ambilightIntensity", 0) == S_OK, "zero intensity accepted");
        config->Flt_GetInt("ambilightIntensity", &readback);
        expect(readback == 0, "zero intensity readback");
        expect(config->Flt_SetInt("ambilightBlur", 100) == S_OK, "maximum blur accepted");
        config->Flt_GetInt("ambilightBlur", &readback);
        expect(readback == 100, "maximum blur readback");
        expect(config->Flt_SetBool("ambilightEnabled", !enabled) == S_OK, "toggle enable");
        bool toggled;
        config->Flt_GetBool("ambilightEnabled", &toggled);
        expect(toggled == !enabled, "enable readback");
        config->Flt_SetInt("ambilightIntensity", intensity);
        config->Flt_SetInt("ambilightBlur", blur);
        config->Flt_SetBool("ambilightEnabled", enabled);
        for (const auto& definition : AmbilightSettingDefinitions) {
            int original;
            expect(config->Flt_GetInt(definition.key, &original) == S_OK, "read extended setting");
            expect(config->Flt_SetInt(definition.key, definition.minimum - 1) == E_INVALIDARG, "reject extended setting below minimum");
            expect(config->Flt_SetInt(definition.key, definition.maximum + 1) == E_INVALIDARG, "reject extended setting above maximum");
            expect(config->Flt_SetInt(definition.key, definition.minimum) == S_OK, "accept extended setting minimum");
            config->Flt_GetInt(definition.key, &readback);
            expect(readback == definition.minimum, "readback extended setting minimum");
            config->Flt_SetInt(definition.key, original);
        }
    }
    // The renderer owns worker objects; leave module loaded until process exit.
    CoUninitialize();
    if (!failures) puts("PASS: local DLL activation without registration, configuration roundtrip and input limits");
    return failures ? 1 : 0;
}
