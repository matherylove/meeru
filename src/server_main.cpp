// MeeruServer: the meeting point people can host themselves.
//
// Meeru tries to connect two people directly, asking their routers for a port
// with UPnP. When that is not available on either side, the two need somewhere
// to be introduced, and something to carry the bytes between them. That is all
// this does.
//
// It is deliberately dull to operate: no configuration file, no database, no
// state that survives a restart. Run it on any machine with a public address
// and give people the host name.
//
// It never sees the contents of a conversation. The two ends run Meeru's own
// handshake through this process, so what passes across is sealed: this
// machine cannot read a message, cannot pretend to be either person, and
// cannot join in. What it does know is which identities are online and who
// connects to whom.

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <stdio.h>

#include "rendezvous.h"

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QString stamp()
{
    return QDateTime::currentDateTime().toString(QString::fromLatin1("yyyy-MM-dd HH:mm:ss"));
}

class Console
{
public:
    Console(const QString &logPath, bool quiet)
        : quiet_(quiet)
    {
        if (!logPath.isEmpty()) {
            log_.setFileName(logPath);
            if (!log_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                write(QString::fromLatin1("Could not open the log file, carrying on without it"));
            }
        }
    }

    void write(const QString &text)
    {
        const QString line = stamp() + QString::fromLatin1("  ") + text;
        if (!quiet_) {
            out() << line << endl;
            out().flush();
        }
        if (log_.isOpen()) {
            log_.write(line.toUtf8());
            log_.write("\n");
            log_.flush();
        }
    }

private:
    bool quiet_;
    QFile log_;
};

void banner(quint16 port)
{
    out() << endl;
    out() << "  Meeru rendezvous node" << endl;
    out() << "  ---------------------" << endl;
    out() << "  Listening on port " << port << "." << endl;
    out() << endl;
    out() << "  Give people this machine's host name or address, with the port if you" << endl;
    out() << "  changed it, and they paste it into Meeru under Settings, Rendezvous" << endl;
    out() << "  nodes. For example:  meeru.example.org:" << port << endl;
    out() << endl;
    out() << "  Make sure this port is open in the firewall of the machine and of your" << endl;
    out() << "  provider. Nothing else is needed: there is no configuration file and" << endl;
    out() << "  nothing is stored on disk." << endl;
    out() << endl;
    out() << "  Conversations passing through are sealed between the two people talking." << endl;
    out() << "  This process cannot read them. It does see which identities are online" << endl;
    out() << "  and who connects to whom, so run it for people who trust you with that." << endl;
    out() << endl;
    out().flush();
}

void usage()
{
    out() << "Meeru rendezvous node" << endl << endl;
    out() << "  MeeruServer [--port N] [--log FILE] [--quiet]" << endl << endl;
    out() << "  --port N     port to listen on (default " << Rendezvous::defaultPort() << ")" << endl;
    out() << "  --log FILE   also append what happens to a file" << endl;
    out() << "  --quiet      say nothing on the console" << endl;
    out() << "  --help       show this" << endl << endl;
    out().flush();
}

}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QString::fromLatin1("MeeruServer"));
    app.setApplicationVersion(QString::fromLatin1("0.0.1"));

    const QStringList arguments = app.arguments();
    if (arguments.contains(QString::fromLatin1("--help"))
        || arguments.contains(QString::fromLatin1("-h"))
        || arguments.contains(QString::fromLatin1("/?"))) {
        usage();
        return 0;
    }

    quint16 port = Rendezvous::defaultPort();
    QString logPath;
    const bool quiet = arguments.contains(QString::fromLatin1("--quiet"));

    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QLatin1String("--port") && i + 1 < arguments.size()) {
            bool ok = false;
            const int number = arguments.at(i + 1).toInt(&ok);
            if (!ok || number <= 0 || number > 65535) {
                out() << "That is not a usable port number." << endl;
                out().flush();
                return 2;
            }
            port = static_cast<quint16>(number);
        } else if (arguments.at(i) == QLatin1String("--log") && i + 1 < arguments.size()) {
            logPath = arguments.at(i + 1);
        }
    }

    Console console(logPath, quiet);

    RendezvousServer server;
    QObject::connect(&server, &RendezvousServer::logMessage,
                     [&console](const QString &text) { console.write(text); });

    QString error;
    if (!server.listen(port, &error)) {
        out() << "Could not listen on port " << port << ": " << error << endl;
        out() << "Another program may already be using it, or the port may need"
              << " administrator rights." << endl;
        out().flush();
        return 1;
    }

    if (!quiet)
        banner(port);
    console.write(QString::fromLatin1("Started on port %1").arg(port));

    return app.exec();
}
