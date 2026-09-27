// ============================================================================
//  MovingBackground.cpp  ——  Windows 桌面动态背景层
//  ---------------------------------------------------------------------------
//  功能：一张图片铺满桌面，鼠标移动时"整张图片"反向平移（不是分层视差），
//        鼠标回到屏幕中心时图片回到正中；带弹簧加减速缓冲，边缘自动约束不出黑边。
//        托盘图标 + 设置对话框，可开开机自启。无 ESC 退出。
//
//  技术：纯 Win32 API + GDI+，零第三方库。
//        自己作为顶层窗口插在"桌面(Progman)"正上方：盖住桌面壁纸和桌面图标，
//        但永远在所有普通窗口和任务栏下面，并且鼠标点击可以穿透到桌面，
//        所以不干扰正常使用（详见下面"层级方案"那段注释里的实测结论）。
//        注意：网上常见的"挂到 WorkerW 壁纸层"做法在 Windows 11 24H2+ 上
//        完全不会被渲染，实测无效，所以没有采用。
//
//  退出：托盘图标右键 -> 退出（或 Ctrl+Alt+Q，可在配置区关闭）
//
//  编译（MSVC，源码不需要 .rc 文件；/utf-8 必须加，否则中文界面文字会乱码）：
//      cl /nologo /utf-8 /EHsc /O2 /DUNICODE /D_UNICODE MovingBackground.cpp ^
//         /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib gdiplus.lib shell32.lib ^
//               comdlg32.lib advapi32.lib winmm.lib
//
//  编译（MinGW-w64）：
//      g++ -O2 -municode -mwindows MovingBackground.cpp -o MovingBackground.exe ^
//          -lgdiplus -lgdi32 -luser32 -lshell32 -lcomdlg32 -lwinmm -static
// ============================================================================

// ============================================================================
//  ===========================  配置区（改这里）  =============================
//  说明：这些只是"出厂默认值"。程序第一次运行会生成 MovingBackground.ini，
//        之后以 INI 为准（托盘 -> 打开设置 里也能实时改）。
//        想恢复默认：设置对话框点"恢复默认"，或直接删掉 INI 文件。
// ============================================================================

// [1] 图片路径（支持 GDI+ 自带解码的格式：jpg / png / bmp / gif / tif）
//     两种写法都行：
//       - 绝对路径：L"D:\\pics\\a.jpg"
//       - 相对路径：L"bg.jpg" —— 相对【exe 所在目录】解析，不依赖当前工作目录。
//         注意：开机自启启动时的工作目录是 C:\Windows\System32，
//         所以相对路径必须按 exe 目录解析（见 ResolveImagePath）。
//     默认指向仓库里自带的 bg.jpg，clone 下来编译完就能直接看到效果。
#define CONFIG_IMAGE_PATH       L"bg.jpg"

// [2] 移动强度：鼠标位移 -> 图片位移 的比例。
//     鼠标离屏幕中心 100px，图片就反向移动 100*强度 px。
//     【怕晕就调小这个，或直接设 0】0.05~0.10 是很轻微、几乎不打扰的手感；
//     0.2 以上是全屏明显晃动，容易晕。
#define CONFIG_MOVE_STRENGTH    0.08f

// [3] 图片放大倍数（必须 >= 1.0）。它决定"能移动多少"：
//     可移动余量 = (倍数-1)/2 x 屏幕尺寸。1.08 = 左右各能移动屏幕宽度的 4%。
//     倍数越大余量越大，但画面被裁掉的也越多。
#define CONFIG_IMAGE_ZOOM       1.08f

// [4] 弹簧速度（角频率 ω，rad/s）。越大越跟手、回中越快；越大越"跳"，越容易晕。
//     想要安静缓慢建议 2.0 ~ 4.0
#define CONFIG_SPRING_OMEGA     3.0f

// [5] 弹簧阻尼比 ζ（弹性）。
//     ★ 眩晕感的主要来源之一是"过冲回弹"（图片冲过目标位置再弹回来、来回晃动），
//       把这里设成 1.0 就是临界阻尼、完全不回弹，画面最稳。
//     <1 有回弹（0.6 左右弹得最明显），>1 发黏。建议 0.9 ~ 1.2
#define CONFIG_SPRING_ZETA      1.00f

// [6] 余量使用率 0.1~1.0：只用掉全部可平移余量的百分之多少。
//     留一点余量可以保证图片永远不贴边（绝对不可能露黑边）。1.0 = 用满。
#define CONFIG_MOVE_MARGIN_USE  0.90f

// [7] 覆盖范围：1 = 所有显示器拼成的虚拟桌面，0 = 只覆盖主显示器
#define CONFIG_VIRTUAL_DESKTOP  1

// [8] 帧率档位（fps）：30 / 60 / 75 / 90 / 120 / 144 / 180 / 240
//     越高动画越细腻（尤其鼠标快速移动时），也越费 CPU；超过显示器刷新率的部分
//     屏幕上看不出来，但时间精度仍然更好。设 0 表示跟随系统默认（60）。
//     档位可以在托盘菜单"帧率"里快捷切换，也可以在设置对话框里选。
#define CONFIG_FPS              60

// [9] 是否写入开机自启（HKCU\Software\Microsoft\Windows\CurrentVersion\Run）
//     默认 0（关闭）：程序不会自作主张改你的注册表。
//     想要开机自启，在托盘菜单里勾"开机自动启动"即可。
#define CONFIG_AUTOSTART        0

// [10] 是否启用兜底退出热键 Ctrl+Alt+Q（托盘异常时也能退出；0 = 关闭）
#define CONFIG_EXIT_HOTKEY      1

// [11] 层级模式：
//      0 = 插在桌面正上方（默认，实测可用）：盖住桌面壁纸和桌面图标，
//          但仍在所有普通窗口下面，不会挡住你正在用的程序；鼠标可穿透点到图标
//      1 = 全程置顶：图片盖住所有窗口（包括你正在用的程序），鼠标同样穿透
#define CONFIG_LAYER_MODE       0

// [12] 中心死区（像素）：鼠标离屏幕中心这么近的时候，图片完全不动。
//      能挡掉手抖、小幅移动带来的全屏晃动 —— 这是最有效的防晕开关之一。
//      0 = 关闭死区。建议 80 ~ 200
#define CONFIG_DEAD_ZONE        120

// [13] 静止回归（毫秒）：鼠标停下这么久之后，图片自动缓缓回到正中。
//      ★ 默认 0（关闭）：关闭时"鼠标不动 = 画面完全静止"，这是最不晕的状态。
//        打开它会在你没碰鼠标的时候也让画面自己滑动，容易加重眩晕感，
//        而且如果设得太短（比弹簧稳定时间还短），图片会在没走到位时就往回漂。
//        确实想要"用一会儿就自动归位"再打开，建议 3000 以上。
#define CONFIG_IDLE_RETURN_MS   0

// [14] 启动/退出缩放动画（毫秒）：
//      启动时图片从"刚好铺满屏幕(100%)"平滑放大到 [3] 里的放大倍数；
//      退出时反向演一遍，演完才真正退出。
//      0 = 关闭（启动即最终倍数，点退出立刻退出）
#define CONFIG_INTRO_MS         700

// ============================================================================
//  ==========================  配置区结束  ====================================
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <objidl.h>     // GDI+ 头文件需要（IStream / PROPID / byte），
#include <propidl.h>    // 配合 WIN32_LEAN_AND_MEAN 时必须显式包含
#include <shellapi.h>
#include <commdlg.h>
#define GDIPVER 0x0110  // 启用 GDI+ 1.1（Bitmap::GetHICON 需要）
#include <gdiplus.h>
#include <mmsystem.h>
#include <math.h>
#include <wchar.h>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winmm.lib")
#endif

// ---------------------------------------------------------------------------
//  常量
// ---------------------------------------------------------------------------
static const wchar_t* const kAppName      = L"MovingBackground";
static const wchar_t* const kOverlayClass = L"MovingBackgroundOverlayWnd";
static const wchar_t* const kTrayClass    = L"MovingBackgroundTrayWnd";
static const wchar_t* const kTrayTip      = L"桌面动态背景 - 双击/右键打开设置";
static const wchar_t* const kIniFileName  = L"MovingBackground.ini";
static const wchar_t* const kRunKeyPath   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* const kRunValueName = L"MovingBackground";

// 自定义消息
#define WM_APP_TRAY             (WM_APP + 1)   // 托盘图标回调
#define WM_APP_SHOW_SETTINGS    (WM_APP + 2)   // 唤起设置对话框
#define WM_APP_RELOAD_ALL       (WM_APP + 3)   // 重新加载配置/图片
#define WM_APP_DO_EXIT          (WM_APP + 4)   // 退出动画演完了，真正退出

// 菜单项 / 控件 ID
#define IDM_SETTINGS    2001
#define IDM_RELOAD      2002
#define IDM_AUTOSTART   2003
#define IDM_EXIT        2004
#define IDM_FPS_BASE    2100    // 2100 + 档位序号

#define IDC_PATH        101
#define IDC_BROWSE      102
#define IDC_STRENGTH    103
#define IDC_ZOOM        104
#define IDC_OMEGA       105
#define IDC_ZETA        106
#define IDC_MARGIN      107
#define IDC_DEADZONE    108
#define IDC_IDLERETURN  109
#define IDC_AUTORUN     110
#define IDC_DEFAULT     111
#define IDC_TOPMOST     112
#define IDC_FPS         113
#define IDC_INTRO       114

#define HOTKEY_EXIT_ID  1

// 可选帧率档位
static const int kFpsPresets[] = { 30, 60, 75, 90, 120, 144, 180, 240 };
static const int kFpsPresetCount = (int)(sizeof(kFpsPresets) / sizeof(kFpsPresets[0]));

// 把任意 fps 归到最接近的档位（INI 里手写别的值也不会让界面错乱）
static int NearestFpsPreset(int fps)
{
    int best = kFpsPresets[0], bestD = 100000;
    for (int i = 0; i < kFpsPresetCount; ++i) {
        int d = kFpsPresets[i] - fps;
        if (d < 0) d = -d;
        if (d < bestD) { bestD = d; best = kFpsPresets[i]; }
    }
    return best;
}

// ---------------------------------------------------------------------------
//  小工具
// ---------------------------------------------------------------------------
// 自己实现字符串拷贝，避免 wcscpy_s/wcsncpy_s 在部分 MinGW 上缺失
static void CopyStr(wchar_t* dst, size_t cch, const wchar_t* src)
{
    if (!dst || cch == 0) return;
    size_t i = 0;
    if (src) { for (; src[i] && i + 1 < cch; ++i) dst[i] = src[i]; }
    dst[i] = 0;
}

static void ShowError(const wchar_t* msg, const wchar_t* extra = nullptr)
{
    wchar_t buf[2048];
    if (extra) swprintf(buf, 2048, L"%ls\n\n%ls", msg, extra);
    else       CopyStr(buf, 2048, msg);
    MessageBoxW(nullptr, buf, kAppName, MB_ICONERROR | MB_OK | MB_TOPMOST);
}

static void ShowInfo(const wchar_t* msg)
{
    MessageBoxW(nullptr, msg, kAppName, MB_ICONINFORMATION | MB_OK | MB_TOPMOST);
}

static void GetExeDir(wchar_t* out, size_t cch)
{
    GetModuleFileNameW(nullptr, out, (DWORD)cch);
    wchar_t* p = wcsrchr(out, L'\\');
    if (p) *p = 0;
}

// DPI 感知：不设置的话系统缩放 125%/150% 时坐标会被虚拟化，图片会被系统拉伸变模糊
static void EnableDpiAwareness()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
        PFN_SetProcessDpiAwarenessContext setCtx =
            (PFN_SetProcessDpiAwarenessContext)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setCtx) {
            // -4 = PER_MONITOR_AWARE_V2，-3 = PER_MONITOR_AWARE，-2 = SYSTEM_AWARE
            if (setCtx((HANDLE)-4)) return;
            if (setCtx((HANDLE)-3)) return;
            if (setCtx((HANDLE)-2)) return;
        }

        typedef BOOL (WINAPI *PFN_SetProcessDPIAware)(void);
        PFN_SetProcessDPIAware setAware =
            (PFN_SetProcessDPIAware)GetProcAddress(user32, "SetProcessDPIAware");
        if (setAware && setAware()) return;
    }

    // 再退一步：Win8.1 的 shcore.SetProcessDpiAwareness（2 = per-monitor）
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI *PFN_SetProcessDpiAwareness)(int);
        PFN_SetProcessDpiAwareness spa =
            (PFN_SetProcessDpiAwareness)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (spa) spa(2);
    }
}

// ===========================================================================
//  配置数据结构 + INI 读写
// ===========================================================================
struct Config {
    wchar_t imagePath[MAX_PATH];
    float   moveStrength;   // [2]
    float   zoom;           // [3]
    float   omega;          // [4]
    float   zeta;           // [5]
    float   marginUse;      // [6]
    int     virtualDesktop; // [7]
    int     fps;            // [8]
    int     autostart;      // [9]
    int     exitHotkey;     // [10]
    int     layerMode;      // [11]
    int     deadZone;       // [12]
    int     idleReturnMs;   // [13]
    int     introMs;        // [14]
};

static Config  g_cfg;
static wchar_t g_iniPath[MAX_PATH] = L"";

static void LoadDefaults()
{
    CopyStr(g_cfg.imagePath, MAX_PATH, CONFIG_IMAGE_PATH);
    g_cfg.moveStrength   = CONFIG_MOVE_STRENGTH;
    g_cfg.zoom           = CONFIG_IMAGE_ZOOM;
    g_cfg.omega          = CONFIG_SPRING_OMEGA;
    g_cfg.zeta           = CONFIG_SPRING_ZETA;
    g_cfg.marginUse      = CONFIG_MOVE_MARGIN_USE;
    g_cfg.virtualDesktop = CONFIG_VIRTUAL_DESKTOP;
    g_cfg.fps            = CONFIG_FPS;
    g_cfg.autostart      = CONFIG_AUTOSTART;
    g_cfg.exitHotkey     = CONFIG_EXIT_HOTKEY;
    g_cfg.layerMode      = CONFIG_LAYER_MODE;
    g_cfg.deadZone       = CONFIG_DEAD_ZONE;
    g_cfg.idleReturnMs   = CONFIG_IDLE_RETURN_MS;
    g_cfg.introMs        = CONFIG_INTRO_MS;
}

// 取值范围限制（防止手改 INI 改出问题）
static void ClampConfig()
{
    if (g_cfg.moveStrength < 0.0f) g_cfg.moveStrength = 0.0f;
    if (g_cfg.moveStrength > 5.0f) g_cfg.moveStrength = 5.0f;
    if (g_cfg.zoom < 1.0f)         g_cfg.zoom = 1.0f;
    if (g_cfg.zoom > 5.0f)         g_cfg.zoom = 5.0f;
    if (g_cfg.omega < 0.5f)        g_cfg.omega = 0.5f;
    if (g_cfg.omega > 40.0f)       g_cfg.omega = 40.0f;
    if (g_cfg.zeta < 0.05f)        g_cfg.zeta = 0.05f;
    if (g_cfg.zeta > 3.0f)         g_cfg.zeta = 3.0f;
    if (g_cfg.marginUse < 0.05f)   g_cfg.marginUse = 0.05f;
    if (g_cfg.marginUse > 1.0f)    g_cfg.marginUse = 1.0f;
    if (g_cfg.fps < 1)             g_cfg.fps = CONFIG_FPS;   // 0 = 用默认
    if (g_cfg.fps < 10)            g_cfg.fps = 10;
    if (g_cfg.fps > 500)           g_cfg.fps = 500;
    if (g_cfg.virtualDesktop != 0) g_cfg.virtualDesktop = 1;
    if (g_cfg.autostart != 0)      g_cfg.autostart = 1;
    if (g_cfg.exitHotkey != 0)     g_cfg.exitHotkey = 1;
    if (g_cfg.layerMode != 1)      g_cfg.layerMode = 0;
    if (g_cfg.deadZone < 0)        g_cfg.deadZone = 0;
    if (g_cfg.deadZone > 1000)     g_cfg.deadZone = 1000;
    if (g_cfg.idleReturnMs < 0)    g_cfg.idleReturnMs = 0;
    if (g_cfg.idleReturnMs > 60000) g_cfg.idleReturnMs = 60000;
    if (g_cfg.introMs < 0)         g_cfg.introMs = 0;
    if (g_cfg.introMs > 10000)     g_cfg.introMs = 10000;
    if (g_cfg.imagePath[0] == 0)   CopyStr(g_cfg.imagePath, MAX_PATH, CONFIG_IMAGE_PATH);
}

static float IniReadFloat(const wchar_t* key, float def)
{
    wchar_t buf[64] = L"";
    GetPrivateProfileStringW(L"General", key, L"", buf, 64, g_iniPath);
    if (buf[0] == 0) return def;
    return (float)wcstod(buf, nullptr);
}

static void IniWriteFloat(const wchar_t* key, float v)
{
    wchar_t buf[64];
    swprintf(buf, 64, L"%.4f", v);
    WritePrivateProfileStringW(L"General", key, buf, g_iniPath);
}

static void IniWriteInt(const wchar_t* key, int v)
{
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", v);
    WritePrivateProfileStringW(L"General", key, buf, g_iniPath);
}

static void LoadIni()
{
    LoadDefaults();
    if (g_iniPath[0] == 0) { ClampConfig(); return; }

    GetPrivateProfileStringW(L"General", L"ImagePath", g_cfg.imagePath,
                             g_cfg.imagePath, MAX_PATH, g_iniPath);
    g_cfg.moveStrength   = IniReadFloat(L"MoveStrength", g_cfg.moveStrength);
    g_cfg.zoom           = IniReadFloat(L"Zoom",         g_cfg.zoom);
    g_cfg.omega          = IniReadFloat(L"SpringOmega",  g_cfg.omega);
    g_cfg.zeta           = IniReadFloat(L"SpringZeta",   g_cfg.zeta);
    g_cfg.marginUse      = IniReadFloat(L"MarginUse",    g_cfg.marginUse);
    g_cfg.virtualDesktop = GetPrivateProfileIntW(L"General", L"VirtualDesktop", g_cfg.virtualDesktop, g_iniPath);

    // 帧率：优先读 Fps；老配置里只有 FrameMs 的话自动换算一次
    g_cfg.fps            = GetPrivateProfileIntW(L"General", L"Fps",             0,                    g_iniPath);
    if (g_cfg.fps <= 0) {
        const int oldMs = GetPrivateProfileIntW(L"General", L"FrameMs", 0, g_iniPath);
        g_cfg.fps = (oldMs > 0) ? NearestFpsPreset(1000 / oldMs) : CONFIG_FPS;
    }
    g_cfg.autostart      = GetPrivateProfileIntW(L"General", L"Autostart",      g_cfg.autostart,      g_iniPath);
    g_cfg.exitHotkey     = GetPrivateProfileIntW(L"General", L"ExitHotkey",     g_cfg.exitHotkey,     g_iniPath);
    g_cfg.layerMode      = GetPrivateProfileIntW(L"General", L"LayerMode",      g_cfg.layerMode,      g_iniPath);
    g_cfg.deadZone       = GetPrivateProfileIntW(L"General", L"DeadZone",       g_cfg.deadZone,       g_iniPath);
    g_cfg.idleReturnMs   = GetPrivateProfileIntW(L"General", L"IdleReturnMs",   g_cfg.idleReturnMs,   g_iniPath);
    g_cfg.introMs        = GetPrivateProfileIntW(L"General", L"IntroMs",        g_cfg.introMs,        g_iniPath);
    ClampConfig();
}

static void SaveIni()
{
    if (g_iniPath[0] == 0) return;

    WritePrivateProfileStringW(L"General", L"ImagePath", g_cfg.imagePath, g_iniPath);
    IniWriteFloat(L"MoveStrength", g_cfg.moveStrength);
    IniWriteFloat(L"Zoom",         g_cfg.zoom);
    IniWriteFloat(L"SpringOmega",  g_cfg.omega);
    IniWriteFloat(L"SpringZeta",   g_cfg.zeta);
    IniWriteFloat(L"MarginUse",    g_cfg.marginUse);
    IniWriteInt  (L"VirtualDesktop", g_cfg.virtualDesktop);
    IniWriteInt  (L"Fps",            g_cfg.fps);
    IniWriteInt  (L"Autostart",      g_cfg.autostart);
    IniWriteInt  (L"ExitHotkey",     g_cfg.exitHotkey);
    IniWriteInt  (L"LayerMode",      g_cfg.layerMode);
    IniWriteInt  (L"DeadZone",       g_cfg.deadZone);
    IniWriteInt  (L"IdleReturnMs",   g_cfg.idleReturnMs);
    IniWriteInt  (L"IntroMs",        g_cfg.introMs);
}

// ===========================================================================
//  全局状态
// ===========================================================================
static HINSTANCE g_hInst     = nullptr;
static HWND      g_hOverlay  = nullptr;   // 背景层窗口（顶层窗口，插在桌面正上方）
static HWND      g_hTray     = nullptr;   // 托盘宿主窗口（屏幕外 1x1）
static HICON     g_hTrayIcon = nullptr;
static bool      g_trayAdded = false;
static bool      g_running   = true;
static UINT      g_msgTaskbarCreated = 0;

// GDI+ / GDI 资源
static ULONG_PTR        g_gdiToken = 0;
static Gdiplus::Bitmap* g_srcImage = nullptr;   // 原始图片
static HBITMAP          g_hbScaled = nullptr;   // 预缩放位图（含四周余量）
static HDC              g_hdcScaled = nullptr;
static HGDIOBJ          g_hbScaledOld = nullptr;
static int              g_scaledW = 0, g_scaledH = 0;

// 布局
static RECT  g_areaScreen = {0, 0, 0, 0};       // 实际覆盖区域（虚拟桌面坐标）
static int   g_areaW = 0, g_areaH = 0;
static int   g_marginX = 0, g_marginY = 0;      // 左右 / 上下 可平移余量（像素）
static POINT g_center = {0, 0};                 // 覆盖区域中心 = "鼠标回中"的基准点

// 弹簧状态（图片位移，像素；正值 = 图片向右/向下）
static float g_posX = 0, g_posY = 0;
static float g_velX = 0, g_velY = 0;
static float g_tgtX = 0, g_tgtY = 0;

static int       g_lastSrcX = INT_MIN, g_lastSrcY = INT_MIN;
static int       g_lastSrcW = 0, g_lastSrcH = 0;   // 动画期间源矩形尺寸也会变，一起去重
static ULONGLONG g_lastTick = 0;

// 启动/退出缩放动画：
//   g_introP   0 = 图片正好铺满屏幕(100%)，1 = 配置的放大倍数
//   g_introDir +1 入场、-1 出场、0 不动
static float g_introP      = 1.0f;
static int   g_introDir    = 0;
static bool  g_exitPending = false;   // 出场动画演完后真正退出

// 缩放动画的缓动（smoothstep：两端都平滑，不会突然起步或急停）
static float IntroEase(float p)
{
    if (p <= 0.0f) return 0.0f;
    if (p >= 1.0f) return 1.0f;
    return p * p * (3.0f - 2.0f * p);
}

// 当前生效的放大倍数（动画期间在 1.0 和配置值之间过渡）
static float IntroZoom(float e)
{
    return 1.0f + (g_cfg.zoom - 1.0f) * e;
}

// 高精度动画驱动：可等待定时器 + 独立线程
// （WM_TIMER 会被系统合并/延迟，实测 16ms 只能跑到约 33fps，全屏平移会明显顿挫）
static HANDLE           g_hAnimTimer  = nullptr;
static HANDLE           g_hAnimThread = nullptr;
static volatile LONG    g_animStop    = 0;
static CRITICAL_SECTION g_drawLock;
static bool             g_drawLockReady = false;

// 前置声明
static void SyncAutostart(bool enable);
static void UpdateMotion(float dt);
static void DrawOverlay(bool force);
static void DrawOverlayTo(HDC hdc, bool force);

// ===========================================================================
//  层级方案：顶层窗口插在"桌面(Progman)"正上方
//  ---------------------------------------------------------------------------
//  【重要】为什么不用网上常见的"挂到 WorkerW 壁纸层"做法：
//  在 Windows 11（24H2 及以后）上实测，凡是 SetParent 进 Progman 子窗口里的
//  窗口（包括 Progman 下的 WorkerW 壁纸层、以及 Progman 本身）**完全不会被渲染**，
//  屏幕上一个像素都看不到 —— 新版桌面的壁纸是由更高的合成层绘制的，
//  Progman 的子窗口被它盖住且不参与合成。
//  实测有效的做法是：自己作为顶层窗口，用 SetWindowPos 插到 Progman 正上方。
//  这样层级为：桌面壁纸 < 本程序 < 所有普通窗口 < 任务栏，且永远不会跑到最前面。
// ===========================================================================

// 目标区域（虚拟桌面 或 主显示器）
static RECT DesiredScreenRect()
{
    RECT r;
    if (g_cfg.virtualDesktop) {
        int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        r.left   = x;
        r.top    = y;
        r.right  = x + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        r.bottom = y + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    } else {
        POINT pt = { 0, 0 };
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
        if (mon && GetMonitorInfoW(mon, &mi)) {
            r = mi.rcMonitor;
        } else {
            r.left = r.top = 0;
            r.right  = GetSystemMetrics(SM_CXSCREEN);
            r.bottom = GetSystemMetrics(SM_CYSCREEN);
        }
    }
    return r;
}

// ===========================================================================
//  GDI+ 图片 / 预缩放位图
// ===========================================================================
static void FreeScaledBitmap()
{
    if (g_hdcScaled) {
        if (g_hbScaledOld) SelectObject(g_hdcScaled, g_hbScaledOld);
        DeleteDC(g_hdcScaled);
        g_hdcScaled = nullptr;
        g_hbScaledOld = nullptr;
    }
    if (g_hbScaled) { DeleteObject(g_hbScaled); g_hbScaled = nullptr; }
    g_scaledW = g_scaledH = 0;
}

static void FreeImage()
{
    FreeScaledBitmap();
    if (g_srcImage) { delete g_srcImage; g_srcImage = nullptr; }
}

// 把配置里的图片路径解析成绝对路径：
//   - 绝对路径（D:\.. 或 \\server\share\..）原样返回；
//   - 相对路径按【exe 所在目录】解析，而不是当前工作目录。
//     这一步很关键：开机自启时进程的工作目录是 C:\Windows\System32，
//     若按 CWD 解析，配置成 "bg.jpg" 会永远找不到图片。
static void ResolveImagePath(const wchar_t* in, wchar_t* out, size_t cch)
{
    if (!out || cch == 0) return;
    out[0] = 0;
    if (!in || in[0] == 0) return;

    // 绝对路径：盘符开头（D:\...）或 UNC 开头（\\server\share）
    if (in[1] == L':' || (in[0] == L'\\' && in[1] == L'\\')) {
        CopyStr(out, cch, in);
        return;
    }

    wchar_t dir[MAX_PATH] = L"";
    GetExeDir(dir, MAX_PATH);
    const size_t n = wcslen(dir);
    if (n > 0 && dir[n - 1] != L'\\' && n + 1 < cch) {
        dir[n]     = L'\\';
        dir[n + 1] = 0;
    }
    swprintf(out, cch, L"%ls%ls", dir, in);
}

// 加载图片；失败时保留原图（重新加载场景下不会把画面弄没）
static bool LoadImageFile()
{
    wchar_t path[MAX_PATH] = L"";
    ResolveImagePath(g_cfg.imagePath, path, MAX_PATH);

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(path, FALSE);
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok ||
        bmp->GetWidth() == 0 || bmp->GetHeight() == 0) {
        delete bmp;
        wchar_t msg[1200];
        swprintf(msg, 1200,
                 L"无法加载图片：\n%ls\n\n请确认文件存在，且格式受支持（jpg/png/bmp/gif/tif）。",
                 path);
        ShowError(msg);
        return false;
    }

    FreeImage();
    g_srcImage = bmp;
    return true;
}

// 把图片按"铺满 + 放大倍数"一次性预缩放到位图，之后每帧只做整数像素 BitBlt。
// 这样即使 4K 大图也能 60fps 流畅平移（每帧不做重采样，CPU 占用极低）。
static bool BuildScaledBitmap()
{
    FreeScaledBitmap();
    if (!g_srcImage || g_areaW <= 0 || g_areaH <= 0) return false;

    const int iw = (int)g_srcImage->GetWidth();
    const int ih = (int)g_srcImage->GetHeight();
    if (iw <= 0 || ih <= 0) return false;

    // 1) cover：等比缩放到刚好铺满覆盖区域
    double cover = (double)g_areaW / iw;
    double cy    = (double)g_areaH / ih;
    if (cy > cover) cover = cy;

    // 2) 再乘放大倍数 -> 这部分多出来的尺寸就是"可平移余量"
    const double total = cover * (double)g_cfg.zoom;

    int sw = (int)ceil(iw * total);
    int sh = (int)ceil(ih * total);
    if (sw < g_areaW) sw = g_areaW;      // 保险：永远不小于覆盖区域
    if (sh < g_areaH) sh = g_areaH;
    if ((sw - g_areaW) & 1) sw++;        // 让左右余量完全对称
    if ((sh - g_areaH) & 1) sh++;

    // 3) 建 32bpp 顶向下 DIBSection
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = sw;
    bi.bmiHeader.biHeight      = -sh;    // 负值 = 顶向下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screenDC = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP hb  = CreateDIBSection(screenDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC     hdc = CreateCompatibleDC(screenDC);
    ReleaseDC(nullptr, screenDC);

    if (!hb || !bits || !hdc) {
        if (hb)  DeleteObject(hb);
        if (hdc) DeleteDC(hdc);
        ShowError(L"创建位图失败（内存不足？试试调小放大倍数）。");
        return false;
    }

    // 4) 用 GDI+ 高质量缩放到这块位图
    {
        Gdiplus::Bitmap target(sw, sh, sw * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        Gdiplus::Graphics g(&target);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);

        // 先铺黑底：PNG 的透明区域不会变成雪花
        Gdiplus::SolidBrush bg(Gdiplus::Color(255, 0, 0, 0));
        g.FillRectangle(&bg, 0, 0, sw, sh);
        g.DrawImage(g_srcImage, Gdiplus::Rect(0, 0, sw, sh), 0, 0, iw, ih, Gdiplus::UnitPixel);
    }

    g_hbScaled    = hb;
    g_hdcScaled   = hdc;
    g_hbScaledOld = SelectObject(g_hdcScaled, g_hbScaled);
    g_scaledW = sw;
    g_scaledH = sh;

    // 5) 可平移余量
    g_marginX = (sw - g_areaW) / 2;
    g_marginY = (sh - g_areaH) / 2;
    return true;
}

// ===========================================================================
//  布局：覆盖区域 = 虚拟桌面 或 主显示器
// ===========================================================================
static bool LayoutOverlay()
{
    const RECT area = DesiredScreenRect();

    g_areaScreen = area;
    g_areaW = area.right - area.left;
    g_areaH = area.bottom - area.top;
    g_center.x = (area.left + area.right) / 2;
    g_center.y = (area.top + area.bottom) / 2;

    return (g_areaW > 0 && g_areaH > 0);
}

// 把覆盖窗口插到桌面正上方（或按配置全程置顶）
static void PlaceOverlayWindow(bool show)
{
    if (!g_hOverlay) return;

    HWND after;
    if (g_cfg.layerMode == 1) {
        after = HWND_TOPMOST;                      // 置顶模式：盖住所有窗口
    } else {
        SetWindowPos(g_hOverlay, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // 从置顶切回来时先取消置顶
        HWND progman = FindWindowW(L"Progman", nullptr);
        after = progman ? progman : HWND_BOTTOM;
    }

    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    if (show) flags |= SWP_SHOWWINDOW;
    SetWindowPos(g_hOverlay, after, g_areaScreen.left, g_areaScreen.top,
                 g_areaW, g_areaH, flags);
}

// 保持"紧贴桌面之上"的层级。
// 点一下桌面、按 Win+D、切换窗口等操作都可能把桌面顶上来，这里发现被顶掉就重申一次。
static void KeepZOrder()
{
    if (!g_hOverlay || g_cfg.layerMode == 1) return;

    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return;

    // GW_HWNDNEXT = z 序中紧挨在我们下面的那个窗口；它若不是桌面，说明层级被顶掉了
    if (GetWindow(g_hOverlay, GW_HWNDNEXT) != progman) {
        SetWindowPos(g_hOverlay, progman, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

// ===========================================================================
//  动画驱动：高精度可等待定时器 + 独立线程
//  为什么不用 WM_TIMER：系统会把它合并延迟（实测 16ms 只能跑 ~33fps），
//  全屏平移在 33fps 下顿挫非常明显、看久了会晕。可等待定时器能稳定跑到设定帧率。
//  调度用 QPC 绝对时间累加，所以 75/144/240 这种"非整数毫秒"的档位也准确。
//  用独立线程还有个好处：打开设置对话框（模态）时动画不会停，改参数能立刻看到效果。
// ===========================================================================
static DWORD WINAPI AnimThreadProc(LPVOID)
{
    LARGE_INTEGER freq, t0, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    LONGLONG lastQpc = t0.QuadPart;
    double nextDue = 0.0;                    // 下一次该出帧的时刻（相对 t0 的秒数）
    int lastFps = 0;

    while (InterlockedCompareExchange(&g_animStop, 0, 0) == 0) {
        int fps = g_cfg.fps;
        if (fps < 10)  fps = 10;
        if (fps > 500) fps = 500;
        const double interval = 1.0 / (double)fps;
        if (fps != lastFps) { lastFps = fps; nextDue = 0.0; }   // 换了档位就重新对齐

        QueryPerformanceCounter(&now);
        const double elapsed = (double)(now.QuadPart - t0.QuadPart) / (double)freq.QuadPart;

        if (nextDue <= 0.0) nextDue = elapsed;
        nextDue += interval;

        double wait = nextDue - elapsed;
        if (wait < 0.0) {
            // 落后了：落后超过 3 帧就直接对齐，不要疯狂追帧
            if (-wait > interval * 3.0) nextDue = elapsed + interval;
            wait = 0.0;
        }
        if (wait > 1.0) { nextDue = elapsed + interval; wait = interval; }

        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(wait * 10000000.0);      // 负 = 相对时间，单位 100ns
        SetWaitableTimer(g_hAnimTimer, &due, 0, nullptr, nullptr, FALSE);
        if (WaitForSingleObject(g_hAnimTimer, 100) != WAIT_OBJECT_0) continue;  // 超时以便响应退出

        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - lastQpc) / (double)freq.QuadPart);
        lastQpc = now.QuadPart;
        if (dt < 0.0f)  dt = 0.0f;
        if (dt > 0.10f) dt = 0.10f;      // 系统卡顿后不让画面跳变

        EnterCriticalSection(&g_drawLock);
        if (g_hOverlay && IsWindow(g_hOverlay)) {
            UpdateMotion(dt);
            DrawOverlay(false);
        }
        LeaveCriticalSection(&g_drawLock);
    }
    return 0;
}

static void AnimStart()
{
    if (!g_drawLockReady) { InitializeCriticalSection(&g_drawLock); g_drawLockReady = true; }
    if (g_hAnimThread) return;

    // 高精度可等待定时器（Win10 1803+；老系统上自动退化为普通定时器，配合 timeBeginPeriod(1) 也够用）
    g_hAnimTimer = CreateWaitableTimerExW(nullptr, nullptr,
                                          CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
    if (!g_hAnimTimer) g_hAnimTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!g_hAnimTimer) return;

    InterlockedExchange(&g_animStop, 0);
    g_hAnimThread = CreateThread(nullptr, 0, AnimThreadProc, nullptr, 0, nullptr);
    if (!g_hAnimThread) { CloseHandle(g_hAnimTimer); g_hAnimTimer = nullptr; }
}

static void AnimStop()
{
    if (g_hAnimThread) {
        InterlockedExchange(&g_animStop, 1);
        if (g_hAnimTimer) CancelWaitableTimer(g_hAnimTimer);
        WaitForSingleObject(g_hAnimThread, 1000);
        CloseHandle(g_hAnimThread);
        g_hAnimThread = nullptr;
    }
    if (g_hAnimTimer) { CloseHandle(g_hAnimTimer); g_hAnimTimer = nullptr; }
}

// 重置动效到"正中静止"
static void ResetMotion()
{
    g_posX = g_posY = 0.0f;
    g_velX = g_velY = 0.0f;
    g_tgtX = g_tgtY = 0.0f;
    g_lastSrcX = g_lastSrcY = INT_MIN;   // 强制重绘
    g_lastSrcW = g_lastSrcH = 0;
}

// ===========================================================================
//  物理：目标位移 + 弹簧缓冲 + 边缘约束
// ===========================================================================
static void UpdateMotion(float dt)
{
    // ---- 0. 推进启动/退出缩放动画 ----
    if (g_introDir != 0 && g_cfg.introMs > 0) {
        g_introP += (float)g_introDir * dt * 1000.0f / (float)g_cfg.introMs;
        if (g_introP >= 1.0f) {
            g_introP = 1.0f;
            g_introDir = 0;
        } else if (g_introP <= 0.0f) {
            g_introP = 0.0f;
            g_introDir = 0;
            if (g_exitPending) {            // 出场演完了，通知主线程真正退出
                g_exitPending = false;
                PostMessageW(g_hTray, WM_APP_DO_EXIT, 0, 0);
            }
        }
    }
    // ---- 1. 目标位移：与"鼠标相对屏幕中心的偏移"反向 ----
    POINT cur;
    GetCursorPos(&cur);

    // 静止回归：鼠标一段时间没动之后，目标自动回到正中（避免画面长期歪着、一动就晃）
    static POINT     s_lastPos  = { 0, 0 };
    static ULONGLONG s_lastMove = 0;
    static bool      s_inited   = false;
    bool idleReturn = false;
    {
        const ULONGLONG now = GetTickCount64();
        if (!s_inited) { s_lastPos = cur; s_lastMove = now; s_inited = true; }
        if (cur.x != s_lastPos.x || cur.y != s_lastPos.y) { s_lastPos = cur; s_lastMove = now; }
        else if (g_cfg.idleReturnMs > 0 &&
                 now - s_lastMove >= (ULONGLONG)g_cfg.idleReturnMs) {
            idleReturn = true;
        }
    }

    float dx = (float)(cur.x - g_center.x);
    float dy = (float)(cur.y - g_center.y);

    // 中心死区：离中心很近时完全不动。按"距离"做平滑缩放而不是硬切，
    // 所以越过死区边界时不会突然跳一下。
    if (g_cfg.deadZone > 0) {
        const float dist = sqrtf(dx * dx + dy * dy);
        if (dist <= (float)g_cfg.deadZone) {
            dx = dy = 0.0f;
        } else {
            const float k = (dist - (float)g_cfg.deadZone) / dist;
            dx *= k;
            dy *= k;
        }
    }

    if (idleReturn) { dx = 0.0f; dy = 0.0f; }

    // 鼠标右移(dx>0) -> 图片左移 -> 位移取负
    float tx = -dx * g_cfg.moveStrength;
    float ty = -dy * g_cfg.moveStrength;

    // ---- 2. 边缘约束：位移不超过可用余量（余量再乘使用率，留安全边）----
    const float limX = (float)g_marginX * g_cfg.marginUse;
    const float limY = (float)g_marginY * g_cfg.marginUse;
    if (tx >  limX) tx =  limX;
    if (tx < -limX) tx = -limX;
    if (ty >  limY) ty =  limY;
    if (ty < -limY) ty = -limY;
    g_tgtX = tx;
    g_tgtY = ty;

    // ---- 3. 弹簧积分：a = ω²·(target - pos) - 2ζω·v ----
    //     静止回归时用更慢的速度，让"回到正中"这一步非常轻，不突兀
    const float w = g_cfg.omega * (idleReturn ? 0.45f : 1.0f);
    const float z = g_cfg.zeta;

    int steps = 1;
    if (dt > 1.0f / 120.0f) steps = (int)(dt / (1.0f / 120.0f)) + 1;
    if (steps > 16) steps = 16;          // 子步长，保证大 dt 也不发散
    const float h = dt / (float)steps;

    for (int i = 0; i < steps; ++i) {
        const float ax = w * w * (g_tgtX - g_posX) - 2.0f * z * w * g_velX;
        const float ay = w * w * (g_tgtY - g_posY) - 2.0f * z * w * g_velY;
        g_velX += ax * h;
        g_velY += ay * h;
        g_posX += g_velX * h;
        g_posY += g_velY * h;
    }

    // ---- 4. 过冲也不能越过余量（否则会露黑边）----
    if (g_posX >  limX) { g_posX =  limX; if (g_velX > 0.0f) g_velX = 0.0f; }
    if (g_posX < -limX) { g_posX = -limX; if (g_velX < 0.0f) g_velX = 0.0f; }
    if (g_posY >  limY) { g_posY =  limY; if (g_velY > 0.0f) g_velY = 0.0f; }
    if (g_posY < -limY) { g_posY = -limY; if (g_velY < 0.0f) g_velY = 0.0f; }

    // ---- 5. 停稳就吸附：避免无意义的持续重绘，省 CPU ----
    if (fabsf(g_posX - g_tgtX) < 0.02f && fabsf(g_velX) < 0.08f) { g_posX = g_tgtX; g_velX = 0.0f; }
    if (fabsf(g_posY - g_tgtY) < 0.02f && fabsf(g_velY) < 0.08f) { g_posY = g_tgtY; g_velY = 0.0f; }
}

// 绘制到指定 DC：从预缩放位图里取一块贴到窗口。
// 普通情况：1:1 整数像素 BitBlt（最清晰）。
// 入场/出场动画期间：有效倍数小于配置值，需要按比例缩放后再拉伸到窗口。
static void DrawOverlayTo(HDC hdc, bool force)
{
    if (!hdc || !g_hdcScaled || g_areaW <= 0 || g_areaH <= 0) return;

    const float e = IntroEase(g_introP);

    // ---- 动画期间：带缩放的路径 ----
    if (e < 0.999f) {
        const float zf = (g_cfg.zoom > 0.01f) ? g_cfg.zoom : 1.0f;
        const float s  = IntroZoom(e) / zf;          // 相对预缩放位图的缩放比（<=1）

        int srcW = (int)lroundf((float)g_areaW / s);
        int srcH = (int)lroundf((float)g_areaH / s);
        if (srcW > g_scaledW) srcW = g_scaledW;
        if (srcH > g_scaledH) srcH = g_scaledH;
        if (srcW < 1) srcW = 1;
        if (srcH < 1) srcH = 1;

        // 位移同样按动画进度缩放（动画结束时正好接上正常路径）
        float panX = g_posX * e, panY = g_posY * e;
        int srcX = (g_scaledW - srcW) / 2 - (int)lroundf(panX / s);
        int srcY = (g_scaledH - srcH) / 2 - (int)lroundf(panY / s);

        const int maxSrcX = g_scaledW - srcW;
        const int maxSrcY = g_scaledH - srcH;
        if (srcX < 0) srcX = 0;
        if (srcY < 0) srcY = 0;
        if (srcX > maxSrcX) srcX = maxSrcX;
        if (srcY > maxSrcY) srcY = maxSrcY;

        if (!force && srcX == g_lastSrcX && srcY == g_lastSrcY &&
            srcW == g_lastSrcW && srcH == g_lastSrcH) return;
        g_lastSrcX = srcX; g_lastSrcY = srcY;
        g_lastSrcW = srcW; g_lastSrcH = srcH;

        // 缩放幅度很小（通常 <10%）时用最快的 COLORONCOLOR；幅度大才用 HALFTONE 保质量
        const float dev = (s > 1.0f) ? (s - 1.0f) : (1.0f - s);
        SetStretchBltMode(hdc, (dev > 0.15f) ? HALFTONE : COLORONCOLOR);
        SetBrushOrgEx(hdc, 0, 0, nullptr);
        StretchBlt(hdc, 0, 0, g_areaW, g_areaH, g_hdcScaled, srcX, srcY, srcW, srcH, SRCCOPY);
        return;
    }

    // ---- 正常路径：1:1 精准贴图 ----
    int srcX = g_marginX - (int)lroundf(g_posX);
    int srcY = g_marginY - (int)lroundf(g_posY);

    const int maxSrcX = g_scaledW - g_areaW;
    const int maxSrcY = g_scaledH - g_areaH;
    if (srcX < 0) srcX = 0;
    if (srcY < 0) srcY = 0;
    if (srcX > maxSrcX) srcX = maxSrcX;
    if (srcY > maxSrcY) srcY = maxSrcY;

    if (!force && srcX == g_lastSrcX && srcY == g_lastSrcY &&
        g_lastSrcW == g_areaW && g_lastSrcH == g_areaH) return;
    g_lastSrcX = srcX; g_lastSrcY = srcY;
    g_lastSrcW = g_areaW; g_lastSrcH = g_areaH;

    BitBlt(hdc, 0, 0, g_areaW, g_areaH, g_hdcScaled, srcX, srcY, SRCCOPY);
}

static void DrawOverlay(bool force)
{
    if (!g_hOverlay) return;
    HDC hdc = GetDC(g_hOverlay);
    if (hdc) {
        DrawOverlayTo(hdc, force);
        ReleaseDC(g_hOverlay, hdc);
    }
}

// ===========================================================================
//  托盘图标
// ===========================================================================
static void TraySetIcon(HICON ico)
{
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hTray;
    nid.uID    = 1;
    nid.uFlags = NIF_ICON;
    nid.hIcon  = ico ? ico : LoadIconW(nullptr, IDI_APPLICATION);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void TrayAdd()
{
    if (g_trayAdded || !g_hTray) return;

    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hTray;
    nid.uID    = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon  = g_hTrayIcon ? g_hTrayIcon : LoadIconW(nullptr, IDI_APPLICATION);
    CopyStr(nid.szTip, 128, kTrayTip);

    g_trayAdded = (Shell_NotifyIconW(NIM_ADD, &nid) != FALSE);
}

static void TrayRemove()
{
    if (!g_trayAdded || !g_hTray) return;
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hTray;
    nid.uID    = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
}

// 用背景图生成托盘图标，辨识度更高
static void BuildTrayIconFromImage()
{
    if (g_hTrayIcon) { DestroyIcon(g_hTrayIcon); g_hTrayIcon = nullptr; }
    if (!g_srcImage) return;

    const int iw = (int)g_srcImage->GetWidth();
    const int ih = (int)g_srcImage->GetHeight();
    if (iw <= 0 || ih <= 0) return;

    // 从图片正中裁一块正方形，避免缩略图被拉伸变形
    const int side = (iw < ih) ? iw : ih;
    const int sx   = (iw - side) / 2;
    const int sy   = (ih - side) / 2;

    Gdiplus::Bitmap thumb(32, 32, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&thumb);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.DrawImage(g_srcImage, Gdiplus::Rect(0, 0, 32, 32), sx, sy, side, side, Gdiplus::UnitPixel);
    }

    HICON ico = nullptr;
    if (thumb.GetHICON(&ico) == Gdiplus::Ok && ico) g_hTrayIcon = ico;
}

static void ShowTrayMenu()
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"打开设置(&S)...");
    AppendMenuW(menu, MF_STRING, IDM_RELOAD,   L"重新加载图片(&R)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // 帧率档位子菜单（单选项）
    {
        HMENU fpsMenu = CreatePopupMenu();
        for (int i = 0; i < kFpsPresetCount; ++i) {
            wchar_t label[32];
            swprintf(label, 32, L"%d fps", kFpsPresets[i]);
            AppendMenuW(fpsMenu, MF_STRING | (g_cfg.fps == kFpsPresets[i] ? MF_CHECKED : 0),
                        (UINT_PTR)(IDM_FPS_BASE + i), label);
        }
        AppendMenuW(menu, MF_POPUP | MF_STRING, (UINT_PTR)fpsMenu, L"帧率(&F)");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_cfg.autostart ? MF_CHECKED : 0), IDM_AUTOSTART,
                L"开机自动启动(&A)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT,     L"退出(&X)");

    SetMenuDefaultItem(menu, IDM_SETTINGS, FALSE);

    // 托盘菜单必须由"前台窗口"弹出，否则点菜单外面它不会消失
    SetForegroundWindow(g_hTray);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hTray, nullptr);
    PostMessageW(g_hTray, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

// 真正的退出动作（只在主线程调用）
static void DoExitNow()
{
    g_running = false;
    TrayRemove();
    if (g_hOverlay) DestroyWindow(g_hOverlay);
    if (g_hTray)    DestroyWindow(g_hTray);
    PostQuitMessage(0);
}

// 请求退出：如果开了退出动画，先反向演一遍（图片缩回 100%）再退
static void RequestExit()
{
    if (g_cfg.introMs > 0 && g_introP > 0.0f && !g_exitPending) {
        g_exitPending = true;
        g_introDir = -1;
        return;                 // 出场动画演完会 PostMessage(WM_APP_DO_EXIT)
    }
    DoExitNow();
}

// ===========================================================================
//  设置对话框（用内存里的 DLGTEMPLATE 构造，所以不需要 .rc 文件）
// ===========================================================================
class DlgBuilder {
public:
    std::vector<BYTE> b;
    WORD count;

    DlgBuilder() : count(0) {}

    void Align4() { while (b.size() % 4) b.push_back(0); }
    void PutW(WORD v) { b.push_back((BYTE)(v & 0xFF)); b.push_back((BYTE)((v >> 8) & 0xFF)); }
    void PutD(DWORD v) { for (int i = 0; i < 4; ++i) b.push_back((BYTE)((v >> (8 * i)) & 0xFF)); }
    void PutS(const wchar_t* s) { while (s && *s) PutW((WORD)*s++); PutW(0); }

    void Begin(const wchar_t* title, short cx, short cy)
    {
        b.clear();
        count = 0;
        Align4();
        PutD(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER);
        PutD(0);                       // 扩展样式
        PutW(0);                       // 控件数，最后回填
        PutW(0); PutW(0); PutW((WORD)cx); PutW((WORD)cy);
        PutW(0);                       // 无菜单
        PutW(0);                       // 默认窗口类
        PutS(title);
        PutW(9);                       // 字号 9
        PutS(L"MS Shell Dlg");         // 字体
    }

    void Item(DWORD style, short x, short y, short cx, short cy, WORD id,
              const wchar_t* cls, const wchar_t* text)
    {
        Align4();
        PutD(style);
        PutD(0);                       // 扩展样式
        PutW((WORD)x); PutW((WORD)y); PutW((WORD)cx); PutW((WORD)cy);
        PutW(id);
        PutS(cls);
        PutS(text);
        PutW(0);                       // 无创建数据
        ++count;
    }

    void Finish() { Align4(); *(WORD*)(b.data() + 8) = count; }
};

static void SetEditFloat(HWND hDlg, int id, float v)
{
    wchar_t buf[64];
    swprintf(buf, 64, L"%.4g", v);
    SetDlgItemTextW(hDlg, id, buf);
}

static bool GetEditFloat(HWND hDlg, int id, float* out)
{
    wchar_t buf[64] = L"";
    GetDlgItemTextW(hDlg, id, buf, 64);
    if (buf[0] == 0) return false;

    wchar_t* end = nullptr;
    const double v = wcstod(buf, &end);
    while (end && (*end == L' ' || *end == L'\t')) ++end;
    if (end && *end != 0) return false;    // 有非数字尾巴
    *out = (float)v;
    return true;
}

static void FillDialogValues(HWND hDlg)
{
    SetDlgItemTextW(hDlg, IDC_PATH, g_cfg.imagePath);
    SetEditFloat(hDlg, IDC_STRENGTH, g_cfg.moveStrength);
    SetEditFloat(hDlg, IDC_ZOOM,     g_cfg.zoom);
    SetEditFloat(hDlg, IDC_OMEGA,    g_cfg.omega);
    SetEditFloat(hDlg, IDC_ZETA,     g_cfg.zeta);
    SetEditFloat(hDlg, IDC_MARGIN,   g_cfg.marginUse);
    SetEditFloat(hDlg, IDC_DEADZONE, (float)g_cfg.deadZone);
    SetEditFloat(hDlg, IDC_IDLERETURN, (float)g_cfg.idleReturnMs);
    SetEditFloat(hDlg, IDC_INTRO,      (float)g_cfg.introMs);
    CheckDlgButton(hDlg, IDC_AUTORUN, g_cfg.autostart ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_TOPMOST, (g_cfg.layerMode == 1) ? BST_CHECKED : BST_UNCHECKED);

    // 帧率档位下拉框
    {
        HWND cb = GetDlgItem(hDlg, IDC_FPS);
        int sel = 0;
        for (int i = 0; i < kFpsPresetCount; ++i) {
            wchar_t label[32];
            swprintf(label, 32, L"%d fps", kFpsPresets[i]);
            SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)label);
            if (kFpsPresets[i] == g_cfg.fps) sel = i;
        }
        SendMessageW(cb, CB_SETCURSEL, (WPARAM)sel, 0);
    }
}

static void BrowseForImage(HWND hDlg)
{
    wchar_t file[MAX_PATH] = L"";
    GetDlgItemTextW(hDlg, IDC_PATH, file, MAX_PATH);

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hDlg;
    ofn.lpstrFilter = L"图片文件\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff\0所有文件\0*.*\0\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"选择背景图片";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) SetDlgItemTextW(hDlg, IDC_PATH, file);
}

static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowTextW(hDlg, L"桌面动态背景 - 设置");
        FillDialogValues(hDlg);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE:
            BrowseForImage(hDlg);
            return TRUE;

        case IDC_DEFAULT:
            LoadDefaults();
            FillDialogValues(hDlg);
            return TRUE;

        case IDOK: {
            wchar_t path[MAX_PATH] = L"";
            GetDlgItemTextW(hDlg, IDC_PATH, path, MAX_PATH);
            if (path[0] == 0) { ShowInfo(L"请先选择一张图片。"); return TRUE; }

            float strength = 0.0f, zoom = 0.0f, omega = 0.0f, zeta = 0.0f, margin = 0.0f;
            float deadZone = 0.0f, idleReturn = 0.0f, introMs = 0.0f;
            struct Field { int id; const wchar_t* name; float* out; float lo; float hi; };
            const Field fields[8] = {
                { IDC_STRENGTH,   L"移动强度",   &strength,   0.0f,  5.0f   },
                { IDC_ZOOM,       L"放大倍数",   &zoom,       1.0f,  5.0f   },
                { IDC_OMEGA,      L"弹簧速度",   &omega,      0.5f,  40.0f  },
                { IDC_ZETA,       L"阻尼比",     &zeta,       0.05f, 3.0f   },
                { IDC_MARGIN,     L"余量使用率", &margin,     0.05f, 1.0f   },
                { IDC_DEADZONE,   L"中心死区",   &deadZone,   0.0f,  1000.0f},
                { IDC_IDLERETURN, L"静止回归",   &idleReturn, 0.0f,  60000.0f},
                { IDC_INTRO,      L"缩放动画",   &introMs,    0.0f,  10000.0f},
            };
            for (int i = 0; i < 8; ++i) {
                if (!GetEditFloat(hDlg, fields[i].id, fields[i].out)) {
                    wchar_t m[256];
                    swprintf(m, 256, L"%ls 必须是一个数字。", fields[i].name);
                    ShowInfo(m);
                    SetFocus(GetDlgItem(hDlg, fields[i].id));
                    return TRUE;
                }
                if (*fields[i].out < fields[i].lo || *fields[i].out > fields[i].hi) {
                    wchar_t m[256];
                    swprintf(m, 256, L"%ls 的取值范围是 %.2f ~ %.2f。",
                             fields[i].name, (double)fields[i].lo, (double)fields[i].hi);
                    ShowInfo(m);
                    SetFocus(GetDlgItem(hDlg, fields[i].id));
                    return TRUE;
                }
            }

            CopyStr(g_cfg.imagePath, MAX_PATH, path);
            g_cfg.moveStrength = strength;
            g_cfg.zoom         = zoom;
            g_cfg.omega        = omega;
            g_cfg.zeta         = zeta;
            g_cfg.marginUse    = margin;
            g_cfg.deadZone     = (int)(deadZone + 0.5f);
            g_cfg.idleReturnMs = (int)(idleReturn + 0.5f);
            g_cfg.introMs      = (int)(introMs + 0.5f);
            g_cfg.autostart    = (IsDlgButtonChecked(hDlg, IDC_AUTORUN) == BST_CHECKED) ? 1 : 0;
            g_cfg.layerMode    = (IsDlgButtonChecked(hDlg, IDC_TOPMOST) == BST_CHECKED) ? 1 : 0;

            // 帧率档位
            {
                const LRESULT sel = SendDlgItemMessageW(hDlg, IDC_FPS, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR && sel >= 0 && sel < kFpsPresetCount)
                    g_cfg.fps = kFpsPresets[sel];
            }
            ClampConfig();

            SaveIni();
            SyncAutostart(g_cfg.autostart != 0);

            if (g_hOverlay) PostMessageW(g_hOverlay, WM_APP_RELOAD_ALL, 0, 0);

            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        return FALSE;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void ShowSettingsDialog()
{
    DlgBuilder d;
    d.Begin(L"桌面动态背景 - 设置", 268, 252);

    const DWORD LS = WS_CHILD | WS_VISIBLE | SS_LEFT;
    const DWORD LE = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    const DWORD LB = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
    const DWORD LC = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX;
    const DWORD LH = WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE;

    short y = 8;
    d.Item(LS, 7, y, 44, 10, (WORD)-1, L"STATIC", L"图片路径：");
    d.Item(LE, 7, (short)(y + 12), 196, 13, IDC_PATH, L"EDIT", L"");
    d.Item(LB, 208, (short)(y + 12), 52, 13, IDC_BROWSE, L"BUTTON", L"浏览...");
    y = (short)(y + 32);

    struct Row { const wchar_t* label; WORD id; const wchar_t* hint; };
    const Row rows[8] = {
        { L"移动强度",   IDC_STRENGTH,   L"0~5，鼠标位移 -> 图片位移的比例（怕晕就调小）"   },
        { L"放大倍数",   IDC_ZOOM,       L"1~5，越大可移动余量越多（也是启动动画的目标）"   },
        { L"弹簧速度",   IDC_OMEGA,      L"0.5~40 rad/s，越大回中越快、越跳"               },
        { L"阻尼比",     IDC_ZETA,       L"0.05~3，=1 不回弹最稳，<1 会来回晃"             },
        { L"余量使用率", IDC_MARGIN,     L"0.05~1，留一点边防露黑边"                       },
        { L"中心死区",   IDC_DEADZONE,   L"0~1000px，离中心这么近就不动（防手抖晃动）"     },
        { L"静止回归",   IDC_IDLERETURN, L"0~60000ms，鼠标停下后自动回中，0=关"            },
        { L"缩放动画",   IDC_INTRO,      L"0~10000ms，启动/退出时从100%长到放大倍数"       },
    };
    for (int i = 0; i < 8; ++i) {
        d.Item(LS, 7,  (short)(y + 2), 44, 10, (WORD)-1, L"STATIC", rows[i].label);
        d.Item(LE, 54, y,              34, 13, rows[i].id, L"EDIT", L"");
        d.Item(LH, 92, y,              168, 13, (WORD)-1, L"STATIC", rows[i].hint);
        y = (short)(y + 17);
    }

    // 帧率档位（下拉框，高度字段是下拉展开的高度）
    d.Item(LS, 7,  (short)(y + 2), 44, 10, (WORD)-1, L"STATIC", L"帧率档位");
    d.Item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
           54, y, 62, 108, IDC_FPS, L"COMBOBOX", L"");
    d.Item(LH, 122, y, 138, 13, (WORD)-1, L"STATIC", L"30/60/75/90/120/144/180/240");
    y = (short)(y + 20);

    d.Item(LC, 7, (short)(y + 4), 104, 12, IDC_AUTORUN, L"BUTTON", L"开机自动启动");
    d.Item(LC, 116, (short)(y + 4), 146, 12, IDC_TOPMOST, L"BUTTON", L"置顶(盖住所有程序)");
    y = (short)(y + 22);
    d.Item(LB, 7,   y, 62, 15, IDC_DEFAULT, L"BUTTON", L"恢复默认");
    d.Item(LB, 148, y, 54, 15, IDOK,        L"BUTTON", L"确定");
    d.Item(LB, 206, y, 54, 15, IDCANCEL,    L"BUTTON", L"取消");
    d.Finish();

    DialogBoxIndirectParamW(g_hInst, (LPCDLGTEMPLATEW)d.b.data(), g_hTray,
                            SettingsDlgProc, 0);
}

// ===========================================================================
//  开机自启（HKCU\...\Run）
// ===========================================================================
static void SyncAutostart(bool enable)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) return;

    if (enable) {
        wchar_t exe[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wchar_t val[MAX_PATH + 4];
        swprintf(val, MAX_PATH + 4, L"\"%ls\"", exe);   // 带引号，防路径含空格
        RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                       (const BYTE*)val,
                       (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kRunValueName);
    }
    RegCloseKey(key);
}

// ===========================================================================
//  背景层窗口过程
// ===========================================================================
static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;                        // 自己全画，不擦背景（防闪烁）

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (g_drawLockReady) EnterCriticalSection(&g_drawLock);
        DrawOverlay(true);
        if (g_drawLockReady) LeaveCriticalSection(&g_drawLock);
        EndPaint(hwnd, &ps);
        return 0;
    }

    // 支持 PrintWindow / 截图工具抓取本窗口内容（也便于自动化验证）
    case WM_PRINTCLIENT:
        if (g_drawLockReady) EnterCriticalSection(&g_drawLock);
        DrawOverlayTo((HDC)wp, true);
        if (g_drawLockReady) LeaveCriticalSection(&g_drawLock);
        return 0;

    case WM_NCHITTEST:
        return HTTRANSPARENT;            // 鼠标穿透

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;            // 永不抢焦点

    case WM_TIMER: {                     // 低频心跳：只负责维持层级（动画由独立线程驱动）
        if (wp == 2) KeepZOrder();
        return 0;
    }

    case WM_DISPLAYCHANGE:               // 改分辨率 / 插拔显示器
    case WM_DPICHANGED:
        PostMessageW(hwnd, WM_APP_RELOAD_ALL, 0, 0);
        return 0;

    case WM_APP_RELOAD_ALL: {
        if (g_drawLockReady) EnterCriticalSection(&g_drawLock);
        LayoutOverlay();
        PlaceOverlayWindow(false);
        if (LoadImageFile()) {
            BuildTrayIconFromImage();
            TraySetIcon(g_hTrayIcon);
            BuildScaledBitmap();
            ResetMotion();
            DrawOverlay(true);
        }
        if (g_drawLockReady) LeaveCriticalSection(&g_drawLock);
        return 0;   // 帧率档位由动画线程每帧自读 g_cfg.fps，无需通知
    }

    case WM_DESTROY:
        if (hwnd == g_hOverlay) AnimStop();
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        if (g_hOverlay == hwnd) g_hOverlay = nullptr;   // 桌面层被销毁(如 explorer 重启)时留个标记
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//  创建覆盖窗口（explorer 重启后也会用它重建）
// ---------------------------------------------------------------------------
static bool CreateOverlayWindow()
{
    if (g_hOverlay && IsWindow(g_hOverlay)) return true;

    if (!LayoutOverlay()) return false;

    // 顶层无边框窗口：
    //   WS_EX_TOOLWINDOW   -> 不进任务栏、不进 Alt+Tab
    //   WS_EX_NOACTIVATE   -> 永不抢焦点
    //   WS_EX_LAYERED + WS_EX_TRANSPARENT -> 鼠标点击穿透到桌面（跨进程时
    //                          WM_NCHITTEST 返回 HTTRANSPARENT 是无效的，必须用这个组合）
    g_hOverlay = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        kOverlayClass, kAppName, WS_POPUP,
        g_areaScreen.left, g_areaScreen.top, g_areaW, g_areaH,
        nullptr, nullptr, g_hInst, nullptr);
    if (!g_hOverlay) return false;

    // 注意：带 WS_EX_LAYERED 的窗口如果不调用这个函数，会变成"完全透明"什么都看不见
    SetLayeredWindowAttributes(g_hOverlay, 0, 255, LWA_ALPHA);

    // 先把画面准备好再显示，避免启动瞬间闪一下
    BuildScaledBitmap();
    ResetMotion();

    // 启动缩放动画：从"刚好铺满(100%)"开始，平滑长到配置的放大倍数
    g_exitPending = false;
    if (g_cfg.introMs > 0) { g_introP = 0.0f; g_introDir = 1; }
    else                   { g_introP = 1.0f; g_introDir = 0; }

    PlaceOverlayWindow(false);
    DrawOverlay(true);
    PlaceOverlayWindow(true);

    // 层级维护用低频定时器（250ms 一次，别的事都不干）
    SetTimer(g_hOverlay, 2, 250, nullptr);
    AnimStart();
    return true;
}

// ===========================================================================
//  托盘宿主窗口过程
// ===========================================================================
static LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // Explorer 重启后托盘图标会丢，这里重新注册
    if (g_msgTaskbarCreated && msg == g_msgTaskbarCreated) {
        g_trayAdded = false;
        TrayAdd();
        if (!g_hOverlay || !IsWindow(g_hOverlay)) {
            g_hOverlay = nullptr;
            CreateOverlayWindow();
        } else {
            PlaceOverlayWindow(true);
        }
        return 0;
    }

    switch (msg) {
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_RBUTTONUP:
            ShowTrayMenu();
            return 0;
        case WM_LBUTTONDBLCLK:
            ShowSettingsDialog();
            return 0;
        }
        return 0;

    case WM_APP_SHOW_SETTINGS:      // 第二次启动程序时，把设置窗口叫出来
        ShowSettingsDialog();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_SETTINGS:
            ShowSettingsDialog();
            return 0;

        case IDM_RELOAD:
            if (g_hOverlay) PostMessageW(g_hOverlay, WM_APP_RELOAD_ALL, 0, 0);
            return 0;

        case IDM_AUTOSTART:
            g_cfg.autostart = g_cfg.autostart ? 0 : 1;
            SaveIni();
            SyncAutostart(g_cfg.autostart != 0);
            return 0;

        case IDM_EXIT:
            RequestExit();
            return 0;
        }

        // 帧率档位
        if (LOWORD(wp) >= IDM_FPS_BASE && LOWORD(wp) < IDM_FPS_BASE + kFpsPresetCount) {
            g_cfg.fps = kFpsPresets[LOWORD(wp) - IDM_FPS_BASE];
            SaveIni();
            return 0;
        }
        return 0;

    case WM_APP_DO_EXIT:            // 退出动画演完了
        DoExitNow();
        return 0;

    case WM_HOTKEY:                 // Ctrl+Alt+Q 兜底退出
        if (wp == HOTKEY_EXIT_ID) RequestExit();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ===========================================================================
//  入口
// ===========================================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    g_hInst = hInst;

    // ---- 单实例：已经在跑就把它的设置窗口叫出来，然后退出 ----
    {
        HWND exist = FindWindowW(kTrayClass, nullptr);
        if (exist) {
            PostMessageW(exist, WM_APP_SHOW_SETTINGS, 0, 0);
            return 0;
        }
    }

    EnableDpiAwareness();

    // ---- INI 路径 = exe 同目录 ----
    {
        wchar_t dir[MAX_PATH] = L"";
        GetExeDir(dir, MAX_PATH);
        swprintf(g_iniPath, MAX_PATH, L"%ls\\%ls", dir, kIniFileName);
    }
    LoadIni();

    // ---- GDI+ ----
    Gdiplus::GdiplusStartupInput gsi;
    if (Gdiplus::GdiplusStartup(&g_gdiToken, &gsi, nullptr) != Gdiplus::Ok) {
        ShowError(L"GDI+ 初始化失败。");
        return 1;
    }
    timeBeginPeriod(1);     // 提高定时器精度，动画更顺

    // ---- 注册窗口类 ----
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = OverlayProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kOverlayClass;
    if (!RegisterClassExW(&wc)) { ShowError(L"注册背景层窗口类失败。"); return 1; }

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = TrayProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = kTrayClass;
    if (!RegisterClassExW(&wc)) { ShowError(L"注册托盘窗口类失败。"); return 1; }

    // ---- 托盘宿主窗口：屏幕外的 1x1 隐形窗口 ----
    // 不能用 WS_EX_NOACTIVATE，否则托盘菜单无法成为"前台菜单"、点外面不消失
    g_hTray = CreateWindowExW(WS_EX_TOOLWINDOW, kTrayClass, kAppName, WS_POPUP,
                              -32000, -32000, 1, 1,
                              nullptr, nullptr, hInst, nullptr);
    if (!g_hTray) { ShowError(L"创建托盘窗口失败。"); return 1; }
    ShowWindow(g_hTray, SW_SHOWNOACTIVATE);

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // ---- 加载图片 ----
    if (!LoadImageFile()) {
        FreeImage();
        Gdiplus::GdiplusShutdown(g_gdiToken);
        return 1;
    }

    // ---- 计算覆盖区域 + 创建并挂载覆盖窗口 ----
    if (!CreateOverlayWindow()) {
        ShowError(L"无法创建桌面覆盖层（找不到可用的桌面层，或屏幕区域无效）。");
        FreeImage();
        Gdiplus::GdiplusShutdown(g_gdiToken);
        return 1;
    }

    // ---- 托盘 ----
    BuildTrayIconFromImage();
    TrayAdd();

    // ---- 同步开机自启（每次启动都按当前配置重写一遍，exe 挪位置也能自愈）----
    SyncAutostart(g_cfg.autostart != 0);

    // ---- 首次运行生成 INI，方便用户直接看到可配置项 ----
    if (GetFileAttributesW(g_iniPath) == INVALID_FILE_ATTRIBUTES) SaveIni();

    // ---- 兜底退出热键 ----
    if (g_cfg.exitHotkey) {
        RegisterHotKey(g_hTray, HOTKEY_EXIT_ID, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q');
    }

    // ---- 消息循环（动画由 WM_TIMER 驱动，所以模态设置对话框打开时动画也不会停）----
    MSG msg;
    while (g_running && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // ---- 清理 ----
    if (g_cfg.exitHotkey) UnregisterHotKey(g_hTray, HOTKEY_EXIT_ID);
    TrayRemove();
    if (g_hOverlay && IsWindow(g_hOverlay)) KillTimer(g_hOverlay, 1);
    FreeImage();
    if (g_hTrayIcon) DestroyIcon(g_hTrayIcon);
    timeEndPeriod(1);
    Gdiplus::GdiplusShutdown(g_gdiToken);
    return 0;
}
