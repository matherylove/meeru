#include "firewall_helper.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QStringList>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

const char kTcpRule[] = "Meeru Messenger (peer connections)";
const char kUdpRule[] = "Meeru Messenger (local network discovery)";

#ifdef Q_OS_WIN
// Checks the rule exists and still mentions the port in use. The output of
// netsh is translated into the user's language, so nothing is matched against
// English words here; only the port number, which is the same everywhere.
bool ruleCoversPort(const QString &name, quint16 port)
{
    QProcess netsh;
    netsh.setProcessChannelMode(QProcess::MergedChannels);

    QStringList arguments;
    arguments << QString::fromLatin1("advfirewall") << QString::fromLatin1("firewall")
              << QString::fromLatin1("show") << QString::fromLatin1("rule")
              << (QString::fromLatin1("name=") + name)
              << QString::fromLatin1("verbose");

    netsh.start(QString::fromLatin1("netsh"), arguments);
    if (!netsh.waitForFinished(5000))
        return false;
    if (netsh.exitCode() != 0)
        return false;   // netsh reports "no rules match" with a non-zero code

    const QString output = QString::fromLocal8Bit(netsh.readAll());
    return output.contains(QString::number(port));
}
#endif

}

bool FirewallHelper::isSupported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

QString FirewallHelper::ruleName(bool tcp)
{
    return QString::fromLatin1(tcp ? kTcpRule : kUdpRule);
}

QString FirewallHelper::defaultProfiles()
{
    return QString::fromLatin1("private,domain,public");
}

bool FirewallHelper::rulesPresent(quint16 tcpPort, quint16 udpPort)
{
#ifdef Q_OS_WIN
    return ruleCoversPort(ruleName(true), tcpPort) && ruleCoversPort(ruleName(false), udpPort);
#else
    Q_UNUSED(tcpPort);
    Q_UNUSED(udpPort);
    return true;
#endif
}

bool FirewallHelper::installRules(quint16 tcpPort, quint16 udpPort,
                                  const QString &profiles, QString *error)
{
#ifdef Q_OS_WIN
    const QString program = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());

    QString scopes = profiles.trimmed();
    if (scopes.isEmpty())
        scopes = defaultProfiles();

    // Rules are bound to this executable as well as the port, so they do not
    // leave a hole open for anything else that happens to use the number.
    QString commands;
    commands += QString::fromLatin1(
        "netsh advfirewall firewall delete rule name=\"%1\" >nul 2>&1 & "
        "netsh advfirewall firewall delete rule name=\"%2\" >nul 2>&1 & ")
        .arg(ruleName(true)).arg(ruleName(false));
    commands += QString::fromLatin1(
        "netsh advfirewall firewall add rule name=\"%1\" dir=in action=allow "
        "program=\"%2\" protocol=TCP localport=%3 profile=%4 & ")
        .arg(ruleName(true)).arg(program).arg(tcpPort).arg(scopes);
    commands += QString::fromLatin1(
        "netsh advfirewall firewall add rule name=\"%1\" dir=in action=allow "
        "program=\"%2\" protocol=UDP localport=%3 profile=%4")
        .arg(ruleName(false)).arg(program).arg(udpPort).arg(scopes);

    const QString parameters = QString::fromLatin1("/c ") + commands;

    SHELLEXECUTEINFOW info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";          // this is what asks Windows for elevation
    info.lpFile = L"cmd.exe";
    info.lpParameters = reinterpret_cast<LPCWSTR>(parameters.utf16());
    info.nShow = SW_HIDE;

    if (!ShellExecuteExW(&info)) {
        if (error) {
            *error = (GetLastError() == ERROR_CANCELLED)
                ? QString::fromLatin1("Administrator permission was declined.")
                : QString::fromLatin1("Windows would not run the firewall command.");
        }
        return false;
    }

    if (info.hProcess) {
        WaitForSingleObject(info.hProcess, 20000);
        DWORD code = 1;
        GetExitCodeProcess(info.hProcess, &code);
        CloseHandle(info.hProcess);
        if (code != 0) {
            if (error)
                *error = QString::fromLatin1("The firewall rules could not be created.");
            return false;
        }
    }
    return true;
#else
    Q_UNUSED(tcpPort);
    Q_UNUSED(udpPort);
    Q_UNUSED(profiles);
    if (error)
        *error = QString::fromLatin1("Only Windows has this kind of firewall rule.");
    return false;
#endif
}
