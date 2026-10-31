/* Upstream: unfuck.h -- Win32 helpers shared by hooks.dll and AltSnap.exe. */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Win32 helpers (no longer a compat layer)                                *
 * Written by Raymond Gillibert in 2021                                  *
 * THIS FILE IS NOT UNDER GPL but under the WTFPL.                       *
 * DO WHAT THE FUCK YOU WANT WITH THIS CODE!                             *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef ALTSNAP_COMMON_WINUTIL_H_
#define ALTSNAP_COMMON_WINUTIL_H_

#include <oleacc.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "common/strutil.h"

// Win32 helpers. Minimum supported OS: Windows 10 version 1607 (build 14393).
// All APIs are linked directly; there are no runtime fallbacks for older
// systems.
#include <dwmapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

// kDwmaUseImmersiveDarkMode had the value 19 on Windows 10 1809-1909.
inline constexpr DWORD kDwmaUseImmersiveDarkModePre20H1 = 19;
// Values from the Windows SDK that older MinGW headers do not define yet.
inline constexpr DWORD kDwmaUseImmersiveDarkMode = 20;

using DorQWORD = UINT_PTR;  // Pointer-sized unsigned integer.

// Pack/unpack two 32-bit values into a pointer-sized integer.
[[nodiscard]] constexpr DWORD HIWORDPTR(UINT_PTR v) {
  return static_cast<DWORD>(v >> 32);
}
[[nodiscard]] constexpr DWORD LOWORDPTR(UINT_PTR v) {
  return static_cast<DWORD>(v);
}
[[nodiscard]] constexpr UINT_PTR MAKELONGPTR(DWORD lo, DWORD hi) {
  return static_cast<UINT_PTR>(lo) | static_cast<UINT_PTR>(hi) << 32;
}

enum { IDAPPLY = 0x3021 };

// Flag set by Windows in KBDLLHOOKSTRUCT-scancodes of simulated input.
inline constexpr DWORD SCANCODE_SIMULATED = 0x0200;

// Exported by user32 but missing from the SDK headers.
extern "C" HWND WINAPI HungWindowFromGhostWindow(HWND ghost);

#ifndef MSAA_MENU_SIG
#define MSAA_MENU_SIG 0xAA0DF00D
typedef struct tagMSAAMENUINFO {
  DWORD dwMSAASignature;
  DWORD cchWText;
  LPWSTR pszWText;
} MSAAMENUINFO, *LPMSAAMENUINFO;
#endif

/*#define LOGA(X, ...) {DWORD err=GetLastError(); FILE *LOG=fopen("ad.log",
 * "a"); fprintf(LOG, X, ##__VA_ARGS__); fprintf(LOG,", LastError=%lu\n",err);
 * fclose(LOG); SetLastError(0); }*/
inline void LOGSimple(const char* str, size_t len) {
  DWORD dummy = 0;
  HANDLE h = nullptr;
  h = CreateFileA("ad.log", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                  OPEN_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  WriteFile(h, str, len, &dummy, nullptr);
  CloseHandle(h);
  SetLastError(0);
}
// printf-style logging to "ad.log". The output is truncated to fit the buffer;
// the compiler checks the format string against the arguments.
[[gnu::format(printf, 1, 2)]] static void LOGfunk(const char* fmt, ...) {
  const DWORD last_error = GetLastError();
  char line[512], error_text[kUintDigits + 1];

  va_list args = nullptr;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  AppendString(line, std::size(line), " (");
  AppendString(line, std::size(line), Uint2lStr(error_text, last_error));
  AppendString(line, std::size(line), ")\n");
  LOGSimple(line, lstrlenA(line));
}
#ifdef LOG_STUFF
#define LOG LOGfunk
#else
#define LOG \
  if (0) LOGdummy
inline void LOGdummy(const char* fmt, ...) {}
#endif

/* Helper function to pop a message bow with error code*/
inline void ErrorBox(const wchar_t* const title) {
  LPVOID lpMsgBuf = nullptr;
  FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, GetLastError(),
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), /* Default language */
      reinterpret_cast<wchar_t*>(&lpMsgBuf), 0, nullptr);
  MessageBox(nullptr, static_cast<const wchar_t*>(lpMsgBuf), title,
             MB_OK | MB_ICONWARNING);
  /* Free the buffer using LocalFree. */
  LocalFree(lpMsgBuf);
}

/* Helper to be able to enable/disable dialog items
 * easily while ensuring we move keyboard focus to
 * the next control if it was selected */
inline BOOL EnableDlgItem(HWND hdlg, UINT id, BOOL enable) {
  HWND hwndControl = GetDlgItem(hdlg, id);
  if (!enable && hwndControl == GetFocus()) {
    SendMessage(hdlg, WM_NEXTDLGCTL, 0, FALSE);
  }
  return EnableWindow(hwndControl, enable);
}

inline DWORD GetMsgPT(POINT* pt) {
  DWORD point = GetMessagePos();
  pt->x = GET_X_LPARAM(point);
  pt->y = GET_Y_LPARAM(point);

  return point;
}

// Returns the full path of the running executable (empty on failure). Unlike a
// MAX_PATH buffer this also works for long paths.
[[nodiscard]] inline std::wstring GetExecutablePath() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileName(nullptr, path.data(),
                                           static_cast<DWORD>(path.size()));
    if (length == 0) return {};
    if (length < path.size()) {  // It fit: `length` excludes the NUL.
      path.resize(length);
      return path;
    }
    path.assign(path.size() * 2, L'\0');
  }
}

// Returns the title text of a window, of any length (empty if it has none).
[[nodiscard]] inline std::wstring GetWindowTextString(HWND hwnd) {
  std::wstring text(static_cast<size_t>(GetWindowTextLength(hwnd)) + 1, L'\0');
  const int copied =
      GetWindowText(hwnd, text.data(), static_cast<int>(text.size()));
  text.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
  return text;
}

// Returns the window class name (class names are limited to 256 characters).
[[nodiscard]] inline std::wstring GetWindowClassName(HWND hwnd) {
  std::wstring name(256, L'\0');
  const int copied =
      GetClassName(hwnd, name.data(), static_cast<int>(name.size()));
  name.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
  return name;
}

// Returns the value of an environment variable, or nothing if it is not set.
[[nodiscard]] inline std::optional<std::wstring> GetEnvironmentString(
    const wchar_t* name) {
  const DWORD size = GetEnvironmentVariable(name, nullptr, 0);  // includes NUL
  if (size == 0) return std::nullopt;
  std::wstring value(size, L'\0');
  const DWORD length = GetEnvironmentVariable(name, value.data(), size);
  value.resize(length);
  return value;
}

// Returns the directory of the running executable, without trailing backslash.
[[nodiscard]] inline std::wstring GetExecutableDirectory() {
  std::wstring path = GetExecutablePath();
  const size_t slash = path.find_last_of(L'\\');
  if (slash == std::wstring::npos) return {};
  path.resize(slash);
  return path;
}

/* Removes the trailing file name from a path */
inline BOOL PathRemoveFileSpecL(wchar_t* p) {
  int i = 0;
  if (!p) return FALSE;

  while (p[++i] != '\0');
  while (i > 0 && p[i] != '\\') i--;
  p[i] = '\0';

  return TRUE;
}

// Sleeps for `duration_ms`, temporarily raising the system timer resolution
// for short sleeps so that Sleep() is accurate to ~1.5 ms instead of ~15 ms.
inline void ASleep(DWORD duration_ms) {
  TIMECAPS tc;
  if (duration_ms > 15 || timeGetDevCaps(&tc, sizeof(tc)) != TIMERR_NOERROR) {
    Sleep(duration_ms);  // No need for an accurate sleep (or not available).
    return;
  }
  timeBeginPeriod(tc.wPeriodMin);
  Sleep(duration_ms);
  timeEndPeriod(tc.wPeriodMin);
}

/* On Windows Vista we have to use this one that applies process wide. */

/* Available only on Windows 7+s */

/* Helper to set alpha and automatically add/remove WS_EX_LAYERED attrib */
inline void SetWindowAlpha(HWND hwnd, BYTE alpha) {
  /*LOGA("Setting ALPHA to %d", (int)alpha);*/
  LONG_PTR exstyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
  if (alpha == 255) {
    if (exstyle &
        WS_EX_LAYERED) /* Remove layered attribute if opacity is 100% */
      SetWindowLongPtr(hwnd, GWL_EXSTYLE, exstyle & ~WS_EX_LAYERED);
  } else {
    if (!(exstyle & WS_EX_LAYERED)) /* Add Layered attribute if needed */
      SetWindowLongPtr(hwnd, GWL_EXSTYLE, exstyle | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
  }
}

/* Supported wince Windows 10, version 1607 [desktop apps only] */

/* With an Windows 8 fallback */

/* Fallbacks for all windows */
inline UINT ReallyGetDpiForWindow(const HWND hwnd) {
  UINT dpi = GetDpiForWindow(hwnd);
  if (!dpi) {
    HDC hdc = GetDC(hwnd);
    if (hdc) {
      dpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSY));
      ReleaseDC(hwnd, hdc);
      if (!dpi) dpi = 96; /* Default to 96 dpi*/
    } else {
      dpi = 96; /* cannot et a DC, default to 96 dpi */
    }
  }
  return dpi;
}
/* https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enablenonclientdpiscaling
 * Available since Windows 10.0.14342 (~1607)
 * https://stackoverflow.com/questions/36864894 Useless since Windows 10.0.15063
 * (1703) So it is only useful in builds frim 1607 to 1703 To be called in the
 * WM_NCCREATE message handler.
 */
/* Only applies to Windows NT for build number */
[[nodiscard]] inline BOOL OredredWinVer() {
  DWORD oVer = 0;
  DWORD ver = GetVersion();
  oVer = (ver & 0x000000FF) << 24    /* MAJOR */
         | (ver & 0x0000FF00) << 8   /* MINOR */
         | (ver & 0xFFFF0000) >> 16; /* BUILDID */

  /* On Windows 9x, no buildID is available in GetVer */
  if (ver & 0x80000000) {
    /* Only use minor/major ver. */
    oVer |= 0xFFFF0000;
  }
  return oVer;
}
inline BOOL IsDarkModeEnabled() {
  DWORD value = 0;
  if (OredredWinVer() >= 0x0A004563) {
    /* In case of Windows 10 build 10.0.17763 or 10.x (future) or 11+ */
    DWORD len = sizeof(value);
    HKEY key = nullptr;
    if (RegOpenKeyEx(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes"
                     "\\Personalize",
                     0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
      if (RegQueryValueEx(key, L"AppsUseLightTheme", nullptr, nullptr,
                          reinterpret_cast<LPBYTE>(&value),
                          &len) == ERROR_SUCCESS) {
        value = !value;
      }
      RegCloseKey(key);
    }
  }
  return value;
}
inline BOOL IsHighContrastEnabled() {
  HIGHCONTRAST hc = {sizeof(hc)};
  if (SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, FALSE))
    return hc.dwFlags & HCF_HIGHCONTRASTON;

  return FALSE;
}
/* We can use the kDwmaUseImmersiveDarkMode=20 DWM style
 * It is documented for Win11 build 22000 but it also appears to work for Win10
 * However from build 1809 to 2004 it the value was =19 and it is =20
 * Since Windows 10 build 20H1 and on Win11
 * kDwmaUseImmersiveDarkMode=19 up to Win10 2004
 * kDwmaUseImmersiveDarkMode=20 since Win10 20H1 */
inline BOOL AllowDarkTitlebar(HWND hwnd) {
  BOOL DarkMode = FALSE;
  if (IsHighContrastEnabled())
    return FALSE; /* Nothing to do and ignore DarkMode */

  DarkMode = IsDarkModeEnabled();
  if (OredredWinVer() >= 0x0A004A29) {
    /* Windows 10 build 10.0.18985 ie 20H1 */
    DwmSetWindowAttribute(hwnd, kDwmaUseImmersiveDarkMode, &DarkMode,
                          sizeof(DarkMode));
  } else if (OredredWinVer() >= 0x0A004563) {
    /* Windows 10 build 10.0.17763 ie: 1809 or later */

    if (OredredWinVer() < 0x0A0047BA) {
      /* Windows 10 build 10.0.18362 ie: before 1903 */
      SetProp(hwnd, L"UseImmersiveDarkModeColors",
              reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(DarkMode)));
    }
    DwmSetWindowAttribute(hwnd, kDwmaUseImmersiveDarkModePre20H1, &DarkMode,
                          sizeof(DarkMode));
  }
  /* We must return the actual state of the Dark mode */
  return DarkMode;
}

/* Helper function */
inline int GetSystemMetricsForWin(int nIndex, HWND hwnd) {
  return GetSystemMetricsForDpi(nIndex, GetDpiForWindow(hwnd));
}
using RGTICTAC = struct tagRgTicTac {
  LARGE_INTEGER sta;
  LARGE_INTEGER end;
  LARGE_INTEGER frq;
};

inline BOOL RGTic(RGTICTAC* tt) {
  QueryPerformanceFrequency(&tt->frq);
  return QueryPerformanceCounter(&tt->sta);
}

inline int RGTac(RGTICTAC* tt) {
  if (QueryPerformanceCounter(&tt->end)) {
    return MulDiv(tt->end.LowPart - tt->sta.LowPart, 1000000, tt->frq.LowPart);
  }
  return -1;
}

/* Needed on Windows 8+ to recive the WM_POWERBROADCAST mesage */

// Upstream: average
[[nodiscard]] inline long Average(long a, long b) {
  return (a >> 1) + (b >> 1) + (((a & 1) + (b & 1)) >> 1);
}

/* #define DwmGetWindowAttribute DwmGetWindowAttribute */

inline void SubRect(RECT* __restrict__ frame, const RECT* rect) {
  frame->left -= rect->left;
  frame->top -= rect->top;
  frame->right = rect->right - frame->right;
  frame->bottom = rect->bottom - frame->bottom;
}
inline void InflateRectBorder(RECT* __restrict__ rc, const RECT* bd) {
  rc->left -= bd->left;
  rc->top -= bd->top;
  rc->right += bd->right;
  rc->bottom += bd->bottom;
}

inline void DeflateRectBorder(RECT* __restrict__ rc, const RECT* bd) {
  rc->left += bd->left;
  rc->top += bd->top;
  rc->right -= bd->right;
  rc->bottom -= bd->bottom;
}

inline void OffsetPoints(POINT* pts, long dx, long dy, unsigned count) {
  while (count--) {
    pts->x += dx;
    pts->y += dy;
    pts++;
  }
}

inline void FixDWMRectLL(HWND hwnd, RECT* bbb, const int SnapGap) {
  RECT rect, frame;

  if (S_OK == DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame,
                                    sizeof(frame)) &&
      GetWindowRect(hwnd, &rect)) {
    SubRect(&frame, &rect);
    CopyRect(bbb, &frame);
  } else {
    SetRectEmpty(bbb);
    /*SetRect(bbb, 10, 10, 10, 10);*/
  }
  if (SnapGap) OffsetRect(bbb, -SnapGap, -SnapGap);
  /*    if (IsZoomed(hwnd)) OffsetRect(bbb, 10, 10); // Test for Zoomed stuff */
}

/* This function is here because under Windows 10, the GetWindowRect function
 * includes invisible borders, if those borders were visible it would make
 * sense, this is the case on Windows 7 and 8.x for example
 * We use DWM api when available in order to get the REAL client area
 */
inline BOOL GetWindowRectLL(HWND hwnd, RECT* rect, const int SnapGap) {
  HRESULT ret = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, rect,
                                      sizeof(*rect));
  if (ret == S_OK) {
    ret = TRUE;
  } else {
    ret = GetWindowRect(hwnd, rect); /* Fallback to normal */
  }
  if (SnapGap) InflateRect(rect, SnapGap, SnapGap);
  return ret;
}
/* Under Win8 and later a window can be cloaked
 * This falg can be obtained with this function
 * 1 The window was cloaked by its owner application.
 * 2 The window was cloaked by the Shell.
 * 4 The cloak value was inherited from its owner window.
 * For windows that are supposed to be logically "visible", in addition to
 * WS_VISIBLE. EDIT: Now Raymond Chen did an article about this:
 * https://devblogs.microsoft.com/oldnewthing/20200302-00/?p=103507
 * I had to figure it out the Hard way...
 */
inline int IsWindowCloaked(HWND hwnd) {
  int cloaked = 0;
  return S_OK == DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked,
                                       sizeof(cloaked)) &&
         cloaked;
}
/* Some windows might have a size of 0x0 (e.g., a 'Setup' window in Windows 11
 * that appears to be visible despite having no size). It is assumed that this
 * is an issue specific to a system update window in Windows 11 (and maybe other
 * versions as well), although this has not been confirmed.
 */
inline BOOL HasWindowSizeZero(HWND hwnd) {
  RECT rc;
  return GetWindowRect(hwnd, &rc) &&
         (rc.right == rc.left || rc.bottom == rc.top);
}
inline BOOL IsVisible(HWND hwnd) {
  return IsWindowVisible(hwnd) && !IsWindowCloaked(hwnd) &&
         !HasWindowSizeZero(hwnd);
}

/* Gets the original owner of hwnd.
 * stops going back the owner chain if invisible. */
inline HWND GetRootOwner(HWND hwnd) {
  HWND parent = nullptr;
  int i = 0;
  while ((parent = (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CHILD)
                       ? GetParent(hwnd)
                       : GetWindow(hwnd, GW_OWNER))) {
    RECT prc;
    if (parent == hwnd || i++ > 2048 || !IsVisible(parent) ||
        !GetWindowRect(parent, &prc) || IsRectEmpty(&prc)) {
      /* Stop if in a loop or if parent is not visible
       * or if the parent rect is empty */
      break;
    }
    hwnd = parent;
  }

  return hwnd;
}

/* Use the DWM api to obtain the rectangel that *should* contain all
 * caption buttons. This is usefull to ensure we are not in one of them.
 */
inline BOOL GetCaptionButtonsRect(HWND hwnd, RECT* rc) {
  int ret =
      DwmGetWindowAttribute(hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, rc, sizeof(*rc));
  /* Convert rectangle to to screen coordinate. */
  if (ret == S_OK) {
    RECT wrc;
    GetWindowRect(hwnd, &wrc);
    OffsetRect(rc, wrc.left, wrc.top);
    return 1;
  }
  return 0;
}

// NtSuspendProcess/NtResumeProcess are undocumented ntdll exports that have no
// import library, so they are resolved once at first use.
inline LONG CallNtProcessFunction(const char* name, HANDLE process) {
  using NtProcessFn = LONG(NTAPI*)(HANDLE);
  constexpr LONG kFailure = 666;
  const HMODULE ntdll = GetModuleHandle(L"ntdll.dll");
  if (!ntdll) return kFailure;
  const auto fn = reinterpret_cast<NtProcessFn>(
      reinterpret_cast<void*>(GetProcAddress(ntdll, name)));
  return fn ? fn(process) : kFailure;
}
inline LONG NtSuspendProcess(HANDLE process) {
  return CallNtProcessFunction("NtSuspendProcess", process);
}
inline LONG NtResumeProcess(HANDLE process) {
  return CallNtProcessFunction("NtResumeProcess", process);
}

struct WindowProcessInfo {
  DWORD pid;
  std::wstring executable_name;
};

class UniqueProcessHandle {
 public:
  explicit UniqueProcessHandle(HANDLE handle) noexcept : handle_(handle) {}

  ~UniqueProcessHandle() {
    if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
  }

  UniqueProcessHandle(const UniqueProcessHandle&) = delete;
  UniqueProcessHandle& operator=(const UniqueProcessHandle&) = delete;

  [[nodiscard]] HANDLE get() const noexcept { return handle_; }

 private:
  HANDLE handle_;
};

// Upstream: PathStripPathL
[[nodiscard]] inline std::wstring_view FileNameFromPath(
    std::wstring_view path) noexcept {
  const auto separator = path.find_last_of('\\');
  return separator == std::wstring_view::npos ? path
                                              : path.substr(separator + 1);
}

// Replacement for `lstrcmpi`
[[nodiscard]] inline bool EqualsIgnoreCase(std::wstring_view lhs,
                                           std::wstring_view rhs) {
  if (lhs.size() != rhs.size() ||
      lhs.size() > static_cast<size_t>((std::numeric_limits<int>::max()))) {
    return false;
  }
  return CompareStringOrdinal(lhs.data(), static_cast<int>(lhs.size()),
                              rhs.data(), static_cast<int>(rhs.size()),
                              TRUE) == CSTR_EQUAL;
}

// Upstream: GetWindowProgNameFromPid
[[nodiscard]] inline std::optional<std::wstring> GetExecutablePathFromPid(
    DWORD pid) {
  if (pid == 0) {
    return std::nullopt;
  }

  UniqueProcessHandle process(
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
  if (!process.get()) {
    return std::nullopt;
  }

  // 32767 is the maximum extended-length path size in wchar_t units.
  constexpr size_t kMaxPathChars = 32768;

  std::wstring buffer(MAX_PATH, L'\0');
  while (true) {
    DWORD size = static_cast<DWORD>(buffer.size());
    if (QueryFullProcessImageName(process.get(), 0, buffer.data(), &size)) {
      buffer.resize(size);  // `size` = chars written, excluding the NUL
      return buffer;
    }
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        buffer.size() >= kMaxPathChars) {
      return std::nullopt;
    }
    buffer.resize(buffer.size() * 2);
  }
}

struct PidPair {
  DWORD parent_pid;
  DWORD child_pid;
};

// Upstream: GetWindowProgName_subwins_EnumProc
inline BOOL CALLBACK GetWindowProgNameSubwinsEnumProc(HWND hwnd, LPARAM lp) {
  auto* pids = reinterpret_cast<struct PidPair*>(lp);
  DWORD hwnd_pid = 0;
  if (GetWindowThreadProcessId(hwnd, &hwnd_pid) && hwnd_pid &&
      hwnd_pid != pids->parent_pid) {
    /* We found a child window with a pid different from its parent! */
    pids->child_pid = hwnd_pid;
    return FALSE; /* DONE! */
  }
  return TRUE; /* Next window */
}

// UWP applications hosted by ApplicationFrameHost.exe can have child windows
// belonging to a different process. Find that process so we can identify the
// actual application rather than its host.
// Upstream: GetWindowProgNameSubWindow
[[nodiscard]] inline std::optional<WindowProcessInfo> GetHostedProcessInfo(
    DWORD parent_pid, HWND parent_hwnd) {
  PidPair pids{
      .parent_pid = parent_pid,  // PID of ApplicationFrameHost or similar.
  };

  // Look for a child window with a PID different from its parent.
  EnumChildWindows(parent_hwnd, GetWindowProgNameSubwinsEnumProc,
                   reinterpret_cast<LPARAM>(&pids));

  // If child_pid is set, we found a child window belonging to another
  // process hosted by ApplicationFrameHost.exe.
  if (pids.child_pid == 0) {
    return std::nullopt;
  }

  auto path = GetExecutablePathFromPid(pids.child_pid);
  if (!path) {
    return std::nullopt;
  }

  return WindowProcessInfo{
      .pid = pids.child_pid,
      .executable_name = std::wstring(FileNameFromPath(*path)),
  };
}

// Upstream: GetWindowProgName
[[nodiscard]] inline std::optional<WindowProcessInfo> GetWindowProcessInfo(
    HWND hwnd) {
  DWORD pid = 0;
  if (!GetWindowThreadProcessId(hwnd, &pid) || !pid) {
    return std::nullopt;
  }

  auto path = GetExecutablePathFromPid(pid);
  if (!path) {
    return std::nullopt;
  }

  const auto filename = FileNameFromPath(*path);

  if (EqualsIgnoreCase(filename, L"ApplicationFrameHost.exe")) {
    if (auto child = GetHostedProcessInfo(pid, hwnd)) {
      return child;
    }
    // If there is no child window belonging to another process,
    // keep ApplicationFrameHost.exe as the process name.
  }

  return WindowProcessInfo{
      .pid = pid,
      .executable_name = std::wstring(filename),
  };
}

/* Function to get the best possible small icon associated with a window
 * We always use SendMessageTimeout to ensure we do not get  locked
 * We start with ICON_SMALL, then ICON_SMALL2, then ICON_BIG and finally
 * we try to get the icon from the window class if everything failed or
 * if the first message timed out (freezing program).
 * Note that ICON_SMALL2 was introduced by Windows XP (I think) */
inline HICON GetWindowIcon(HWND hwnd) {
  constexpr UINT TIMEOUT = 64;  // milliseconds
  HICON icon = nullptr;
  if (SendMessageTimeout(hwnd, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG,
                         TIMEOUT, reinterpret_cast<DWORD_PTR*>(&icon))) {
    /* The message failed without timeout */
    if (icon) return icon; /* Sucess */

    if (SendMessageTimeout(hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG,
                           TIMEOUT, reinterpret_cast<DWORD_PTR*>(&icon)) &&
        icon)
      return icon;

    /* Try again with the big icon if we were unable to retreave the small one.
     */
    if (SendMessageTimeout(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG,
                           TIMEOUT, reinterpret_cast<DWORD_PTR*>(&icon)) &&
        icon)
      return icon;
  }
  /* Try the Class icon if nothing can be get */
  if ((icon = reinterpret_cast<HICON>(GetClassLongPtr(hwnd, GCLP_HICONSM))))
    return icon;
  if ((icon = reinterpret_cast<HICON>(GetClassLongPtr(hwnd, GCLP_HICON))))
    return icon;

  return LoadIcon(nullptr, IDI_WINLOGO); /* Default to generic window icon */
}
/* Helper function to get the current system menu font.
 * We need to use the newer NONCLIENTMETRICS for Window 8+ dpi aware
 * SystemParametersInfoForDpi() and We must ude the old NONCLIENTMETRICS
 * structure when using Windows XP or lower.
 * (vistal added the iPaddedBorderWidth int element */
struct OLDNONCLIENTMETRICSAW {
  UINT cbSize;
  int iBorderWidth;
  int iScrollWidth;
  int iScrollHeight;
  int iCaptionWidth;
  int iCaptionHeight;
  LOGFONT lfCaptionFont;
  int iSmCaptionWidth;
  int iSmCaptionHeight;
  LOGFONT lfSmCaptionFont;
  int iMenuWidth;
  int iMenuHeight;
  LOGFONT lfMenuFont;
  LOGFONT lfStatusFont;
  LOGFONT lfMessageFont;
};
struct NEWNONCLIENTMETRICSAW {
  UINT cbSize;
  int iBorderWidth;
  int iScrollWidth;
  int iScrollHeight;
  int iCaptionWidth;
  int iCaptionHeight;
  LOGFONT lfCaptionFont;
  int iSmCaptionWidth;
  int iSmCaptionHeight;
  LOGFONT lfSmCaptionFont;
  int iMenuWidth;
  int iMenuHeight;
  LOGFONT lfMenuFont;
  LOGFONT lfStatusFont;
  LOGFONT lfMessageFont;
  int iPaddedBorderWidth; /* New in Window Vista */
};
inline BOOL GetNonClientMetricsDpi(struct NEWNONCLIENTMETRICSAW* ncm,
                                   UINT dpi) {
  BOOL ret = 0;
  ZeroObject(*ncm);
  ncm->cbSize = sizeof(struct NEWNONCLIENTMETRICSAW);
  ret = SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,
                                   sizeof(struct NEWNONCLIENTMETRICSAW), ncm, 0,
                                   dpi);
  if (!ret) { /* Old Windows versions... XP and below */
    ncm->cbSize = sizeof(struct OLDNONCLIENTMETRICSAW);
    ret = SystemParametersInfo(SPI_GETNONCLIENTMETRICS,
                               sizeof(struct OLDNONCLIENTMETRICSAW), ncm, 0);
  }
  return ret;
}
inline HFONT CreateNCMenuFont(UINT dpi) {
  struct NEWNONCLIENTMETRICSAW ncm;
  GetNonClientMetricsDpi(&ncm, dpi);
  return CreateFontIndirect(&ncm.lfMenuFont);
}

inline void MaximizeWindow(HWND hwnd) {
  PostMessage(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
}
inline void RestoreWindow(HWND hwnd) {
  PostMessage(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
}
inline void ToggleMaxRestore(HWND hwnd) {
  PostMessage(hwnd, WM_SYSCOMMAND, IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE,
              0);
}
inline void MinimizeWindow(HWND hwnd) {
  PostMessage(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
}
/* Just changes the window Z-order */
inline BOOL SetWindowLevel(HWND hwnd, HWND hafter) {
  return SetWindowPos(
      hwnd, hafter, 0, 0, 0, 0,
      SWP_ASYNCWINDOWPOS | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
}
inline int HitTestTimeoutL(HWND hwnd, LPARAM lParam) {
  DorQWORD area = 0;

  /*
      if(DwmDefWindowProc(hwnd, WM_NCHITTEST, 0, lParam, (LRESULT*)&area))
          return area;
  */

  while (hwnd && SendMessageTimeout(hwnd, WM_NCHITTEST, 0, lParam, SMTO_NORMAL,
                                    255, &area)) {
    if (static_cast<int>(area) == HTTRANSPARENT)
      hwnd = GetParent(hwnd);
    else
      break;
  }
  return static_cast<int>(area);
}
inline int HitTestTimeout(HWND hwnd, int x, int y) {
  return HitTestTimeoutL(hwnd, MAKELPARAM(x, y));
}

/* This is used to detect is the window was snapped normally outside of
 * AltDrag, in this case the window appears as normal
 * ie: wndpl.showCmd=SW_SHOWNORMAL, but  its actual rect does not match with
 * its rcNormalPosition and if the WM_RESTORE command is sent, The window
 * will be restored. This is a non documented behaviour. */
inline int IsWindowSnapped(HWND hwnd) {
  RECT rect;
  int W = 0, H = 0, nW = 0, nH = 0;
  WINDOWPLACEMENT wndpl;
  wndpl.length = sizeof(wndpl);

  if (!GetWindowRect(hwnd, &rect)) return 0;
  W = rect.right - rect.left;
  H = rect.bottom - rect.top;

  GetWindowPlacement(hwnd, &wndpl);
  nW = wndpl.rcNormalPosition.right - wndpl.rcNormalPosition.left;
  nH = wndpl.rcNormalPosition.bottom - wndpl.rcNormalPosition.top;

  return wndpl.showCmd != SW_MAXIMIZE && (W != nW || H != nH);
}

/* Helper function to get the Min and Max tracking sizes */
inline void GetMinMaxInfoF(HWND hwnd, POINT* Min, POINT* Max, UCHAR flags) {
  MINMAXINFO mmi;
  UINT dpi = GetDpiForWindow(hwnd);
  ZeroObject(mmi);
  mmi.ptMinTrackSize.x = GetSystemMetricsForDpi(SM_CXMINTRACK, dpi);
  mmi.ptMinTrackSize.y = GetSystemMetricsForDpi(SM_CYMINTRACK, dpi);
  mmi.ptMaxTrackSize.x = GetSystemMetricsForDpi(SM_CXMAXTRACK, dpi);
  mmi.ptMaxTrackSize.y = GetSystemMetricsForDpi(SM_CYMAXTRACK, dpi);
  *Min = mmi.ptMinTrackSize;
  *Max = mmi.ptMaxTrackSize;
  if ((flags & 3) != 3) {
    DWORD_PTR ret = 0; /* 32ms timeout */
    SendMessageTimeout(hwnd, WM_GETMINMAXINFO, 0,
                       reinterpret_cast<LPARAM>(&mmi), SMTO_ABORTIFHUNG, 32,
                       &ret);
    if (!(flags & 1)) *Min = mmi.ptMinTrackSize;
    if (!(flags & 2)) *Max = mmi.ptMaxTrackSize;
  }
}

[[nodiscard]] inline int SamePt(const POINT a, const POINT b) {
  return (a.x == b.x && a.y == b.y);
}

/* Says if a rect is inside another one */
[[nodiscard]] inline BOOL RectInRect(const RECT* big, const RECT* wnd) {
  return wnd->left >= big->left && wnd->top >= big->top &&
         wnd->right <= big->right && wnd->bottom <= big->bottom;
}

[[nodiscard]] inline BOOL RectInRectT(const RECT* big, const RECT* wnd,
                                      const int T) {
  return wnd->left + T >= big->left && wnd->top + T >= big->top &&
         wnd->right <= big->right + T && wnd->bottom <= big->bottom + T;
}

[[nodiscard]] inline unsigned WhichSideRectInRect(const RECT* mon,
                                                  const RECT* wnd) {
  unsigned flag = 0;
  flag = ((wnd->left == mon->left) & (mon->right - wnd->right > 16)) << 2;
  flag |= ((wnd->right == mon->right) & (wnd->left - mon->left > 16)) << 3;
  flag |= ((wnd->top == mon->top) & (mon->bottom - wnd->bottom > 16)) << 4;
  flag |= ((wnd->bottom == mon->bottom) & (wnd->top - mon->top > 16)) << 5;

  return flag;
}

/* the seg rectangle must be zero height or zero width */
inline void CropOutRectFromSeg(RECT* seg, const RECT* cover) {
  POINT point1 = {seg->left, seg->top};
  POINT point2 = {seg->right, seg->bottom};
  if (seg->left == seg->right) {
    /* Vertical segment */
    if (PtInRect(cover, point1)) {
      seg->top = cover->bottom; /* top point is under the rect */
    }
    if (PtInRect(cover, point2)) {
      seg->bottom = cover->top; /* bottom point is under the rect */
    }
  } else /* if( seg->top == seg->bottom) */ {
    /* Horizontal segment */
    if (PtInRect(cover, point1)) {
      seg->left = cover->right; /* left point is under the rect */
    }
    if (PtInRect(cover, point2)) {
      seg->right = cover->left; /* right point is under the rect */
    }
  }
}

[[nodiscard]] inline int IsEqualT(int a, int b, int th) {
  return (b - th <= a) & (a <= b + th);
}
inline int IsInRangeT(int x, int a, int b, int T) {
  return (a - T <= x) & (x <= b + T);
}
[[nodiscard]] inline unsigned AreRectsAlignedOutT(const RECT* a, const RECT* b,
                                                  const int tol) {
  return IsEqualT(a->left, b->right, tol) << 2 |
         IsEqualT(a->top, b->bottom, tol) << 4 |
         IsEqualT(a->right, b->left, tol) << 3 |
         IsEqualT(a->bottom, b->top, tol) << 5;
}
[[nodiscard]] inline unsigned AreRectsAlignedInT(const RECT* a, const RECT* b,
                                                 const int tol) {
  return IsEqualT(a->left, b->left, tol) << 2 |
         IsEqualT(a->top, b->top, tol) << 4 |
         IsEqualT(a->right, b->right, tol) << 3 |
         IsEqualT(a->bottom, b->bottom, tol) << 5;
}

inline int InRange(int x, int a, int b) {
  return (x >= a) && (x <= b);
}
// True if the segments [ay1, ay2] and [by1, by2] overlap and the coordinates
// ax and bx (perpendicular to them) are within `tol` of each other.
[[nodiscard]] inline int SegT(long ax, long bx, long ay1, long ay2, long by1,
                              long by2, int tol) {
  return IsEqualT(ax, bx, tol) /* ax == bx */
         && (InRange(ay1, by1, by2) || InRange(by1, ay1, ay2) ||
             InRange(ay2, by1, by2) || InRange(by2, ay1, ay2));
}
/* Tells if rect b is touching rect a and on which side
 * 2^X, x=2 for Left, 3 for right, 4 for top and 5 for Bottom*/
[[nodiscard]] inline unsigned AreRectsTouchingT(const RECT* a, const RECT* b,
                                                const int tol) {
  return SegT(a->left, b->right, a->top, a->bottom, b->top, b->bottom, tol)
             << 2 /* Left */
         | SegT(a->right, b->left, a->top, a->bottom, b->top, b->bottom, tol)
               << 3 /* Right */
         | SegT(a->top, b->bottom, a->left, a->right, b->left, b->right, tol)
               << 4 /* Top */
         | SegT(a->bottom, b->top, a->left, a->right, b->left, b->right, tol)
               << 5; /* Bottom */
}
inline void CropRect(RECT* __restrict__ wnd, const RECT* crop) {
  wnd->left = std::max(wnd->left, crop->left);
  wnd->top = std::max(wnd->top, crop->top);
  wnd->right = std::min(wnd->right, crop->right);
  wnd->bottom = std::min(wnd->bottom, crop->bottom);
}

inline void CenterRectInRect(RECT* __restrict__ wnd, const RECT* mon) {
  int width = wnd->right - wnd->left;
  int height = wnd->bottom - wnd->top;
  wnd->left = mon->left + (mon->right - mon->left) / 2 - width / 2;
  wnd->top = mon->top + (mon->bottom - mon->top) / 2 - height / 2;
  wnd->right = wnd->left + width;
  wnd->bottom = wnd->top + height;
}

inline void ClampPointInRect(const RECT* rc, POINT* __restrict__ pt) {
  pt->x = std::clamp(pt->x, rc->left, rc->right - 1);
  pt->y = std::clamp(pt->y, rc->top, rc->bottom - 1);
}

/* DownlevelLCIDToLocaleName in NLSDL.DLL */
/* LCIDToLocaleName  in KERNEL32.DLL*/

#endif
