#include "platform_support.h"

#include <QSysInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

int detectVersion()
{
#ifdef Q_OS_WIN
    // GetVersionEx lies on Windows 8.1 and later unless the program declares
    // support in its manifest, so the registry is read instead: it is the one
    // place that keeps telling the truth on every version.
    HKEY key = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t buffer[64];
        DWORD size = sizeof(buffer);
        DWORD type = 0;

        // Windows 10 and 11 publish CurrentMajorVersionNumber; older ones do not.
        DWORD major = 0;
        size = sizeof(major);
        if (RegQueryValueExW(key, L"CurrentMajorVersionNumber", 0, &type,
                             reinterpret_cast<LPBYTE>(&major), &size) == ERROR_SUCCESS
            && major >= 10) {
            RegCloseKey(key);
            return Platform::Windows10;
        }

        size = sizeof(buffer);
        if (RegQueryValueExW(key, L"CurrentVersion", 0, &type,
                             reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS) {
            RegCloseKey(key);
            const QString value = QString::fromWCharArray(buffer);
            const int dot = value.indexOf(QLatin1Char('.'));
            if (dot > 0) {
                const int major2 = value.left(dot).toInt();
                const int minor = value.mid(dot + 1).toInt();
                return major2 * 10 + minor;
            }
        } else {
            RegCloseKey(key);
        }
    }

    // Last resort, accurate at least up to Windows 8.
    OSVERSIONINFOW info;
    ZeroMemory(&info, sizeof(info));
    info.dwOSVersionInfoSize = sizeof(info);
#pragma warning(push)
#pragma warning(disable: 4996)
    if (GetVersionExW(&info))
        return static_cast<int>(info.dwMajorVersion * 10 + info.dwMinorVersion);
#pragma warning(pop)
#endif
    return Platform::Unknown;
}

}

int Platform::version()
{
    static const int cached = detectVersion();
    return cached;
}

QString Platform::versionName()
{
    switch (version()) {
    case WindowsXP:    return QString::fromLatin1("Windows XP");
    case WindowsVista: return QString::fromLatin1("Windows Vista");
    case Windows7:     return QString::fromLatin1("Windows 7");
    case Windows8:     return QString::fromLatin1("Windows 8");
    case Windows81:    return QString::fromLatin1("Windows 8.1");
    case Windows10:    return QString::fromLatin1("Windows 10 or newer");
    default:           return QSysInfo::prettyProductName();
    }
}

bool Platform::hasDesktopDuplication()
{
    return version() >= Windows8;
}

bool Platform::hasMediaFoundation()
{
    return version() >= WindowsVista;
}

bool Platform::hasDirectShow()
{
    // Present on every Windows Meeru runs on, including XP.
    return version() >= WindowsXP;
}

bool Platform::hasHardwareVideoEncoding()
{
    // Nothing before Windows 7 can encode on the graphics card at all, and even
    // there it depends on the card. Treated as unavailable rather than probed,
    // because a wrong yes costs a failed call and a wrong no costs nothing.
    return version() >= Windows7;
}

int Platform::suggestedScreenWidth()
{
    if (version() >= Windows8)
        return 1024;      // desktop duplication keeps up comfortably
    if (version() >= Windows7)
        return 800;
    return 640;           // GDI on old hardware
}

int Platform::suggestedFrameInterval()
{
    if (version() >= Windows8)
        return 200;       // five frames a second
    if (version() >= Windows7)
        return 333;
    return 500;           // two a second is what a Pentium III will manage
}

QString Platform::capabilitySummary()
{
    QString summary = versionName() + QString::fromLatin1(".\n\n");

    summary += hasDesktopDuplication()
        ? QString::fromLatin1("Screen sharing uses Desktop Duplication, which is quick because the "
                              "graphics card sends only what changed.\n")
        : QString::fromLatin1("Screen sharing uses GDI, the only capture this version of Windows "
                              "offers. It works, but it costs more and runs slower.\n");

    summary += hasMediaFoundation()
        ? QString::fromLatin1("Cameras are read through Media Foundation.\n")
        : QString::fromLatin1("Cameras are read through DirectShow, the path Windows XP has.\n");

    summary += hasHardwareVideoEncoding()
        ? QString::fromLatin1("\nThis machine could encode video on the graphics card. Meeru still "
                              "sends JPEG frames, because the other end may be a machine that "
                              "cannot, and a call has to suit both sides.")
        : QString::fromLatin1("\nThis machine cannot encode video on the graphics card: nothing "
                              "before Windows 7 can, and DXVA here only decodes. Calls send JPEG "
                              "frames, which is what this hardware can do in real time.");

    return summary;
}
