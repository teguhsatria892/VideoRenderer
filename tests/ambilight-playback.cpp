// Integration test against a real DirectShow decoder and the modified renderer.
// Usage: ambilight-playback.exe renderer.ax clip.mp4 output-prefix [hdr]
#define NOMINMAX
#include <windows.h>
#include <dshow.h>
#include <d3d9.h>
#include <atlbase.h>
#include <VersionHelpers.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include "../Source/IVideoRenderer.h"
#include "../Include/FilterInterfaces.h"
#include "../Source/AmbilightSettings.h"
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "user32.lib")
static void check(HRESULT hr) { if (FAILED(hr)) { printf("HRESULT %08lx\n", hr); exit(1); } }
static void require(bool ok, const char* msg) { if (!ok) { printf("FAIL: %s\n", msg); exit(1); } }
static void pump(DWORD duration) {
    const ULONGLONG until = GetTickCount64() + duration;
    do { MSG msg; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); } Sleep(5); } while(GetTickCount64() < until);
}
static std::vector<BYTE> capture(IExFilterConfig* config, const std::wstring& path) {
    void* dib = nullptr; unsigned size = 0;
    check(config->Flt_GetBin("displayedImage", &dib, &size));
    auto* header = (BITMAPINFOHEADER*)dib;
    require(header->biBitCount == 32, "32-bit capture required");
    std::vector<BYTE> image((BYTE*)dib, (BYTE*)dib + size);
    BITMAPFILEHEADER bmp = {};
    bmp.bfType = 0x4d42; bmp.bfSize = sizeof(bmp) + size; bmp.bfOffBits = sizeof(bmp) + header->biSize;
    FILE* file; _wfopen_s(&file, path.c_str(), L"wb"); require(file != nullptr, "open output bitmap");
    fwrite(&bmp, sizeof(bmp), 1, file); fwrite(dib, size, 1, file); fclose(file); LocalFree(dib);
    return image;
}
int wmain(int argc, wchar_t** argv) {
    if(argc < 4) return 2;
    const bool hdr = argc > 4;
    check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    HMODULE dll = LoadLibraryW(argv[1]); require(dll != nullptr, "load renderer");
    auto getClass = (HRESULT(WINAPI*)(REFCLSID, REFIID, void**))GetProcAddress(dll, "DllGetClassObject");
    CLSID clsid; CLSIDFromString(L"{71F080AA-8661-4093-B15E-4F6903E77D0A}", &clsid);
    CComPtr<IClassFactory> factory; check(getClass(clsid, IID_PPV_ARGS(&factory)));
    CComPtr<IBaseFilter> renderer; check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&renderer)));
    CComQIPtr<IVideoRenderer> settingsApi(renderer);
    Settings_t settings; settingsApi->GetSettings(settings);
    settings.bUseD3D11 = true; settings.iTexFormat = TEXFMT_8INT;
    settings.iSwapEffect = SWAPEFFECT_Discard; settings.bVPRTXVideoHDR = false;
    settings.bHdrPassthrough = false; settings.bHdrLocalToneMapping = false; settings.bConvertToSdr = true;
    settingsApi->SetSettings(settings);
    CComQIPtr<IExFilterConfig> config(renderer);
    for (const auto& definition : AmbilightSettingDefinitions) check(config->Flt_SetInt(definition.key, definition.initial));
    check(config->Flt_SetBool("ambilightEnabled", false));
    check(config->Flt_SetInt("ambilightIntensity", 35));
    HWND owner = CreateWindowExW(0, L"STATIC", L"Ambilight integration test", WS_OVERLAPPEDWINDOW, 0, 0, 1000, 700, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    require(owner != nullptr, "create hidden render owner");
    CComQIPtr<IVideoWindow> videoWindow(renderer);
    check(videoWindow->put_Owner((OAHWND)owner));
    CComPtr<IGraphBuilder> graph; check(graph.CoCreateInstance(CLSID_FilterGraph));
    check(graph->AddFilter(renderer, L"Ambilight renderer under test"));
    check(graph->RenderFile(argv[2], nullptr));
    CComQIPtr<IMediaControl> control(graph);
    CComQIPtr<IMediaSeeking> seeking(graph);
    CComQIPtr<IBasicVideo> basic(renderer);
    long sourceWidth, sourceHeight;
    check(basic->GetVideoSize(&sourceWidth, &sourceHeight));
    auto size = [&](long width, long height) {
        check(videoWindow->SetWindowPosition(0, 0, width, height));
        const double scale = std::min((double)width / sourceWidth, (double)height / sourceHeight);
        long w = (long)(sourceWidth * scale), h = (long)(sourceHeight * scale);
        check(basic->SetDestinationPosition((width - w) / 2, (height - h) / 2, w, h));
        return RECT{(width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2};
    };
    RECT video = size(960, 540);
    check(control->Run()); pump(1500); check(control->Pause()); pump(300);
    OAFilterState state; check(control->GetState(3000, &state));
    require(settingsApi->GetActive(), "renderer input is connected");
    const std::wstring prefix(argv[3]);
    auto off = capture(config, prefix + L"-off.bmp");
    check(config->Flt_SetBool("ambilightEnabled", true)); pump(100);
    auto on = capture(config, prefix + L"-on.bmp");
    auto* header = (BITMAPINFOHEADER*)on.data();
    require(header->biWidth == 960 && abs(header->biHeight) == 540, "capture viewport dimensions");
    size_t changedInside = 0, changedOutside = 0;
    for(int y=0; y<540; ++y) for(int x=0; x<960; ++x) {
        size_t index = sizeof(BITMAPINFOHEADER) + (y * 960 + x) * 4;
        bool changed = memcmp(off.data()+index, on.data()+index, 3) != 0;
        if(x >= video.left && x < video.right && y >= video.top && y < video.bottom) changedInside += changed;
        else changedOutside += changed;
    }
    printf("%ldx%ld: changed video pixels=%zu, changed bar pixels=%zu\n", sourceWidth, sourceHeight, changedInside, changedOutside);
    require(changedInside == 0, "effect modified sharp video");
    bool hasBars = video.top > 0 || video.left > 0;
    require(hdr || !hasBars ? changedOutside == 0 : changedOutside > 500, "bar effect or HDR/exact-fit bypass");
    if (!hdr && video.top > 2) {
        check(config->Flt_SetInt("ambilightSpread", 400));
        check(config->Flt_SetInt("ambilightIntensity", 100));
        check(config->Flt_SetInt("ambilightBlur", 0));
        auto sharpLight = capture(config, prefix + L"-blur-0.bmp");
        check(config->Flt_SetInt("ambilightBlur", 100));
        auto softLight = capture(config, prefix + L"-blur-100.bmp");
        auto maximumColorStep = [&](const std::vector<BYTE>& bitmap) {
            int result = 0;
            int y = video.top / 2;
            for (int x = 1; x < 960; ++x) for (int channel = 0; channel < 3; ++channel) {
                size_t index = sizeof(BITMAPINFOHEADER) + (y * 960 + x) * 4 + channel;
                result = std::max(result, abs((int)bitmap[index] - bitmap[index - 4]));
            }
            return result;
        };
        int hardStep = maximumColorStep(sharpLight), softStep = maximumColorStep(softLight);
        printf("Blur quality: max adjacent color step %d -> %d (0 -> 100)\n", hardStep, softStep);
        require(hardStep > 20 && softStep <= 5 && softStep < hardStep / 4, "maximum blur still contains hard color blocks");
        check(config->Flt_SetInt("ambilightSpread", 17));
        check(config->Flt_SetInt("ambilightIntensity", 35));
        check(config->Flt_SetInt("ambilightBlur", 50));
    }
    if (!hdr && hasBars) {
        check(config->Flt_SetInt("ambilightSpread", 0));
        auto zeroSpread = capture(config, prefix + L"-spread-zero.bmp");
        require(zeroSpread == off, "spread zero did not immediately clear light while paused");
        check(config->Flt_SetInt("ambilightSpread", 17));
        check(config->Flt_SetInt("ambilightDirections", 0));
        auto zeroDirections = capture(config, prefix + L"-directions-zero.bmp");
        require(zeroDirections == off, "directions did not immediately update while paused");
        check(config->Flt_SetInt("ambilightDirections", 15));
        check(config->Flt_SetInt("ambilightBrightness", 0));
        auto zeroBrightness = capture(config, prefix + L"-brightness-zero.bmp");
        require(zeroBrightness == off, "brightness did not immediately update while paused");
        check(config->Flt_SetInt("ambilightBrightness", 100));
    }
    LONGLONG position = 50000000;
    check(seeking->SetPositions(&position, AM_SEEKING_AbsolutePositioning, nullptr, AM_SEEKING_NoPositioning));
    pump(300);
    LONGLONG actualPosition;
    check(seeking->GetCurrentPosition(&actualPosition));
    require(actualPosition >= position, "seek did not reach requested timestamp");
    capture(config, prefix + L"-seek.bmp");
    size(800, 600); pump(200); auto resized = capture(config, prefix + L"-resize.bmp");
    require(((BITMAPINFOHEADER*)resized.data())->biWidth == 800 && abs(((BITMAPINFOHEADER*)resized.data())->biHeight) == 600, "resize failed");
    check(control->Run()); pump(500); check(control->Stop());
    std::wstring info; settingsApi->GetVideoProcessorInfo(info);
    FILE* infoFile; _wfopen_s(&infoFile, (prefix + L"-info.txt").c_str(), L"w, ccs=UTF-8");
    if(infoFile) { fputws(info.c_str(), infoFile); fclose(infoFile); }
    puts("PASS: real decoder playback, pause, effect toggle, sharp-video equality, seek, resize, resume");
    return 0;
}
