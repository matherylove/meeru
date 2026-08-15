#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QStringList>

#include "rendezvous.h"

#include "login_window.h"
#include "rendezvous_window.h"

int main(int argc, char *argv[]) {
    // Meeru lays itself out in whole pixels, the way the machines it targets
    // do. Letting Qt rescale per screen made the text change size and reflow
    // when the window was dragged to a monitor with a different DPI, so the
    // scaling is switched off and every size in the app is given in pixels.
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    // Anyone who does want Meeru enlarged on a very dense screen can still say
    // so with the standard Qt environment variables; only the automatic,
    // per-monitor scaling is switched off.
    if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")
        && qEnvironmentVariableIsEmpty("QT_AUTO_SCREEN_SCALE_FACTOR")) {
        QApplication::setAttribute(Qt::AA_DisableHighDpiScaling, true);
    }
#endif

    QApplication app(argc, argv);
    app.setOrganizationName(QString::fromLatin1("MTA Mathery Automation"));
    app.setApplicationName(QString::fromLatin1("Meeru"));
    app.setApplicationVersion(QString::fromLatin1("0.0.1"));

    QFont baseFont(QString::fromLatin1("Segoe UI"));
    baseFont.setPixelSize(11);
    app.setFont(baseFont);

    const QIcon meeruIcon(QStringLiteral(":/assets/Meeru Trans.png"));
    app.setWindowIcon(meeruIcon);

    // A rendezvous node is normally MeeruServer, but keeping the mode here too
    // means somebody can host one from a desktop without a second download.
    const QStringList arguments = app.arguments();
    if (arguments.contains(QString::fromLatin1("--rendezvous"))) {
        quint16 port = Rendezvous::defaultPort();
        const int at = arguments.indexOf(QString::fromLatin1("--rendezvous"));
        if (at + 1 < arguments.size()) {
            bool ok = false;
            const int number = arguments.at(at + 1).toInt(&ok);
            if (ok && number > 0 && number < 65536)
                port = static_cast<quint16>(number);
        }

        RendezvousWindow *window = new RendezvousWindow(port);
        window->setWindowIcon(meeruIcon);
        window->show();
        return app.exec();
    }

    LoginWindow login;
    login.setWindowIcon(meeruIcon);
    login.show();

    return app.exec();
}
