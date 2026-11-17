// Opens the built plug-in's editor in a window, plays a WAV through its processor in real time,
// and saves a PNG of the editor at a chosen moment. For checking the UI without a DAW.
//
//   fwyc_editor_preview <plugin.vst3> <input.wav> [--at SECONDS] [--click X,Y]... [--out FILE.png]
//
// --click clicks the editor at X,Y (editor pixels) once it is open, e.g. 191,108 for the
// Waterfall view or 692,516 for Detect only. Repeatable.
//
// Audio before the last 22 s up to --at is processed as fast as possible; the rest plays at real
// speed so the editor's meters and waterfall history fill as they would live. Windows only.

#include "wav_reader.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int kBlockSize = 256;
constexpr double kRealtimeSeconds = 22.0;

void pumpMessages ()
{
    MSG msg;
    while (PeekMessageW (&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage (&msg);
        DispatchMessageW (&msg);
    }
}

void pumpFor (std::chrono::milliseconds duration)
{
    const auto end = std::chrono::steady_clock::now () + duration;
    while (std::chrono::steady_clock::now () < end)
    {
        pumpMessages ();
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
}

LRESULT CALLBACK windowProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW (hwnd, msg, wParam, lParam);
}

HWND createHostWindow (int width, int height)
{
    WNDCLASSW wc {};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW (nullptr);
    wc.lpszClassName = L"FwycEditorPreview";
    wc.hCursor = LoadCursor (nullptr, IDC_ARROW);
    RegisterClassW (&wc);

    RECT r {0, 0, width, height};
    const DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
    AdjustWindowRect (&r, style, FALSE);
    return CreateWindowW (wc.lpszClassName, L"Feedback From Whence You Came - preview", style, CW_USEDEFAULT,
                          CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
}

HWND firstChild (HWND parent)
{
    HWND child = nullptr;
    EnumChildWindows (
        parent, [] (HWND hwnd, LPARAM out) -> BOOL {
            *reinterpret_cast<HWND*> (out) = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM> (&child));
    return child;
}

void click (HWND hwnd, int x, int y)
{
    const LPARAM pos = MAKELPARAM (x, y);
    SendMessageW (hwnd, WM_MOUSEMOVE, 0, pos);
    SendMessageW (hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
    SendMessageW (hwnd, WM_LBUTTONUP, 0, pos);
}

bool pngEncoder (CLSID& clsid)
{
    UINT count = 0, bytes = 0;
    Gdiplus::GetImageEncodersSize (&count, &bytes);
    if (bytes == 0)
        return false;
    std::vector<char> buffer (bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*> (buffer.data ());
    Gdiplus::GetImageEncoders (count, bytes, encoders);
    for (UINT i = 0; i < count; ++i)
        if (std::wcscmp (encoders[i].MimeType, L"image/png") == 0)
        {
            clsid = encoders[i].Clsid;
            return true;
        }
    return false;
}

bool saveWindowPng (HWND hwnd, const std::wstring& path)
{
    RECT r;
    GetClientRect (hwnd, &r);
    const int w = r.right - r.left, h = r.bottom - r.top;

    HDC screen = GetDC (nullptr);
    HDC mem = CreateCompatibleDC (screen);
    HBITMAP bitmap = CreateCompatibleBitmap (screen, w, h);
    HGDIOBJ old = SelectObject (mem, bitmap);
    constexpr UINT kRenderFullContent = 2; // PW_RENDERFULLCONTENT: includes Direct2D content
    const bool printed = PrintWindow (hwnd, mem, PW_CLIENTONLY | kRenderFullContent) != 0;
    SelectObject (mem, old);

    bool saved = false;
    CLSID clsid;
    if (printed && pngEncoder (clsid))
    {
        Gdiplus::Bitmap image (bitmap, nullptr);
        saved = image.Save (path.c_str (), &clsid, nullptr) == Gdiplus::Ok;
    }
    DeleteObject (bitmap);
    DeleteDC (mem);
    ReleaseDC (nullptr, screen);
    return saved;
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: fwyc_editor_preview <plugin.vst3> <input.wav> [--at SECONDS] [--click X,Y]... "
                              "[--out FILE.png]\n");
        return 2;
    }
    const std::string pluginPath = argv[1];
    const std::string wavPath = argv[2];
    double atSeconds = -1.0;
    std::vector<POINT> clicks;
    std::string outPath = "editor_preview.png";
    for (int i = 3; i < argc; ++i)
    {
        POINT p {};
        if (!std::strcmp (argv[i], "--at") && i + 1 < argc)
            atSeconds = std::atof (argv[++i]);
        else if (!std::strcmp (argv[i], "--click") && i + 1 < argc && std::sscanf (argv[++i], "%ld,%ld", &p.x, &p.y) == 2)
            clicks.push_back (p);
        else if (!std::strcmp (argv[i], "--out") && i + 1 < argc)
            outPath = argv[++i];
    }

    // Hosts run their UI thread with COM initialised, before loading plug-ins: VSTGUI creates its
    // WIC factory (for bitmaps) when the module loads.
    OleInitialize (nullptr);

    fwyc::tools::WavFile wav;
    std::string error;
    if (!fwyc::tools::readWav (wavPath, wav, error))
    {
        std::fprintf (stderr, "%s: %s\n", wavPath.c_str (), error.c_str ());
        return 1;
    }
    const auto totalSamples = static_cast<int64_t> (wav.samples.size ());
    const int64_t endSample = atSeconds < 0.0 ? totalSamples
                                              : std::min<int64_t> (totalSamples, static_cast<int64_t> (atSeconds * wav.sampleRate));

    auto module = VST3::Hosting::Module::create (pluginPath, error);
    if (!module)
    {
        std::fprintf (stderr, "%s: %s\n", pluginPath.c_str (), error.c_str ());
        return 1;
    }
    auto host = owned (new HostApplication ());
    PluginContextFactory::instance ().setPluginContext (host);

    IPtr<PlugProvider> provider;
    for (const auto& info : module->getFactory ().classInfos ())
        if (info.category () == kVstAudioEffectClass)
        {
            provider = owned (new PlugProvider (module->getFactory (), info, true));
            break;
        }
    if (!provider || !provider->initialize ())
    {
        std::fprintf (stderr, "could not instantiate the plug-in\n");
        return 1;
    }
    IPtr<IComponent> component = provider->getComponentPtr ();
    IPtr<IEditController> controller = provider->getControllerPtr ();
    FUnknownPtr<IAudioProcessor> processor (component);

    ProcessSetup setup {kRealtime, kSample32, kBlockSize, wav.sampleRate};
    component->activateBus (kAudio, kInput, 0, true);
    component->activateBus (kAudio, kOutput, 0, true);
    processor->setupProcessing (setup);
    component->setActive (true);
    processor->setProcessing (true);

    HostProcessData data;
    data.prepare (*component, kBlockSize, kSample32);

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup (&gdiplusToken, &gdiplusInput, nullptr);

    IPtr<IPlugView> view = owned (controller->createView (ViewType::kEditor));
    ViewRect size;
    if (!view || view->getSize (&size) != kResultOk)
    {
        std::fprintf (stderr, "the plug-in has no editor\n");
        return 1;
    }
    HWND hwnd = createHostWindow (size.getWidth (), size.getHeight ());
    view->attached (hwnd, kPlatformTypeHWND);
    ShowWindow (hwnd, SW_SHOWNOACTIVATE);
    pumpFor (std::chrono::milliseconds (300));
    HWND editorWindow = firstChild (hwnd);
    for (const auto& p : clicks)
        if (editorWindow)
            click (editorWindow, p.x, p.y);

    const int64_t realtimeFrom = std::max<int64_t> (0, endSample - static_cast<int64_t> (kRealtimeSeconds * wav.sampleRate));
    auto clock = std::chrono::steady_clock::now ();
    for (int64_t pos = 0; pos < endSample; pos += kBlockSize)
    {
        const int n = static_cast<int> (std::min<int64_t> (kBlockSize, endSample - pos));
        std::memcpy (data.inputs[0].channelBuffers32[0], wav.samples.data () + pos, sizeof (float) * static_cast<size_t> (n));
        data.numSamples = n;
        processor->process (data);

        pumpMessages ();
        if (pos >= realtimeFrom)
        {
            clock += std::chrono::nanoseconds (static_cast<int64_t> (n * 1e9 / wav.sampleRate));
            std::this_thread::sleep_until (clock);
        }
        else
        {
            clock = std::chrono::steady_clock::now ();
        }
    }
    pumpFor (std::chrono::milliseconds (150));

    const std::wstring wide (outPath.begin (), outPath.end ());
    const bool saved = saveWindowPng (hwnd, wide);
    std::printf ("%s %s at %.2f s\n", saved ? "saved" : "FAILED to save", outPath.c_str (),
                 static_cast<double> (endSample) / wav.sampleRate);

    view->removed ();
    view = nullptr;
    DestroyWindow (hwnd);
    processor->setProcessing (false);
    component->setActive (false);
    data.unprepare ();
    Gdiplus::GdiplusShutdown (gdiplusToken);
    OleUninitialize ();
    return saved ? 0 : 1;
}
