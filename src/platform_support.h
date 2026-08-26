#ifndef MEERU_PLATFORM_SUPPORT_H
#define MEERU_PLATFORM_SUPPORT_H

#include <QString>

// What this particular Windows can actually do.
//
// Meeru runs on machines separated by twenty years, so a feature is offered
// only where it works and explained where it does not, rather than being
// offered everywhere and failing on half of them.
namespace Platform {

enum Version {
    Unknown = 0,
    WindowsXP = 51,        // 5.1
    WindowsVista = 60,
    Windows7 = 61,
    Windows8 = 62,
    Windows81 = 63,
    Windows10 = 100        // and everything after; Microsoft stopped moving the number
};

int version();
QString versionName();

// Capture of the screen. GDI works everywhere and is slow; Desktop Duplication
// arrived with Windows 8 and is far cheaper, since the graphics card hands over
// only what changed.
bool hasDesktopDuplication();

// Camera capture. DirectShow is the path that reaches back to Windows XP;
// Media Foundation replaced it from Vista onwards and is what Qt prefers on
// modern systems. Either one gives Meeru frames.
bool hasMediaFoundation();
bool hasDirectShow();

// Encoding video on the graphics card. There is no such thing before Windows 7:
// DXVA on XP decodes only, and it is the reason calls here send JPEG frames.
bool hasHardwareVideoEncoding();

// A sentence for the interface explaining what this machine will and will not
// do, so a limitation reads as a fact about the computer rather than a fault.
QString capabilitySummary();

// Screen capture width that this machine can be expected to keep up with.
int suggestedScreenWidth();
int suggestedFrameInterval();

}

#endif
