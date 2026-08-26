#ifndef MEERU_FIREWALL_HELPER_H
#define MEERU_FIREWALL_HELPER_H

#include <QString>

// Adds the Windows Firewall rules Meeru needs, asking for administrator rights
// only when the rules are actually missing.
//
// Meeru listens on a TCP port for contacts and a UDP port for finding people on
// the local network. Windows blocks both by default, and the prompt it shows on
// first run is easy to dismiss by accident, which leaves the program looking
// broken with no explanation anywhere.
namespace FirewallHelper {

bool isSupported();

// True when both rules exist and still name the ports Meeru is using. A rule
// left over from an older port is treated as missing, since it lets nothing
// through and would otherwise hide the problem forever.
bool rulesPresent(quint16 tcpPort, quint16 udpPort);

// Runs an elevated command that creates the rules. Windows shows its own
// consent prompt; if the user says no, this returns false and Meeru carries on
// unchanged.
//
// profiles is any combination of "private", "domain" and "public", as netsh
// spells them. Public is the one to think twice about: it covers networks
// Windows does not consider trusted, such as a cafe or an airport.
bool installRules(quint16 tcpPort, quint16 udpPort, const QString &profiles, QString *error = 0);

QString defaultProfiles();

QString ruleName(bool tcp);

}

#endif
