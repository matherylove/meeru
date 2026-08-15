#include "port_mapper.h"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegExp>
#include <QStringList>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>

namespace {

const int kDiscoveryTimeoutMs = 4000;
const int kLeaseSeconds = 3600;
const int kRenewMs = 30 * 60 * 1000;

const char kSearchTarget[] = "urn:schemas-upnp-org:device:InternetGatewayDevice:1";

QString headerValue(const QString &text, const QString &name)
{
    const QStringList lines = text.split(QRegExp(QString::fromLatin1("[\r\n]+")), QString::SkipEmptyParts);
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i);
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        if (line.left(colon).trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return line.mid(colon + 1).trimmed();
    }
    return QString();
}

// Which of our addresses the router will see us on.
QString localAddressFor(const QString &gatewayHost)
{
    QHostAddress gateway(gatewayHost);
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (int i = 0; i < interfaces.size(); ++i) {
        const QList<QNetworkAddressEntry> entries = interfaces.at(i).addressEntries();
        for (int j = 0; j < entries.size(); ++j) {
            const QNetworkAddressEntry &entry = entries.at(j);
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            if (entry.ip().isLoopback())
                continue;
            if (!gateway.isNull() && entry.netmask().toIPv4Address() != 0) {
                const quint32 mask = entry.netmask().toIPv4Address();
                if ((entry.ip().toIPv4Address() & mask) == (gateway.toIPv4Address() & mask))
                    return entry.ip().toString();
            }
        }
    }
    for (int i = 0; i < interfaces.size(); ++i) {
        const QList<QNetworkAddressEntry> entries = interfaces.at(i).addressEntries();
        for (int j = 0; j < entries.size(); ++j) {
            const QHostAddress ip = entries.at(j).ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol && !ip.isLoopback())
                return ip.toString();
        }
    }
    return QString();
}

}

PortMapper::PortMapper(QObject *parent)
    : QObject(parent),
      network_(0), discovery_(0), discoveryTimeout_(0), renew_(0),
      port_(0), mapped_(false), searching_(false)
{
    network_ = new QNetworkAccessManager(this);

    discoveryTimeout_ = new QTimer(this);
    discoveryTimeout_->setSingleShot(true);
    discoveryTimeout_->setInterval(kDiscoveryTimeoutMs);
    connect(discoveryTimeout_, SIGNAL(timeout()), this, SLOT(onDiscoveryTimeout()));

    renew_ = new QTimer(this);
    renew_->setInterval(kRenewMs);
    connect(renew_, SIGNAL(timeout()), this, SLOT(onRenewTick()));
}

void PortMapper::requestMapping(quint16 port)
{
    port_ = port;
    mapped_ = false;
    if (!controlUrl_.isEmpty()) {
        addMapping();
        return;
    }
    discover();
}

void PortMapper::release()
{
    renew_->stop();
    mapped_ = false;
    controlUrl_.clear();
    if (discovery_) {
        discovery_->close();
        discovery_->deleteLater();
        discovery_ = 0;
    }
}

void PortMapper::discover()
{
    if (searching_)
        return;
    searching_ = true;

    discovery_ = new QUdpSocket(this);
    // QHostAddress::AnyIPv4 is an enum value that converts to both QHostAddress
    // and quint16, so the two argument bind() has to be told which one it is.
    if (!discovery_->bind(QHostAddress(QHostAddress::AnyIPv4), quint16(0))) {
        searching_ = false;
        emit failed(QString::fromLatin1("Could not look for a router"));
        return;
    }
    connect(discovery_, SIGNAL(readyRead()), this, SLOT(onDiscoveryDatagram()));

    QByteArray search;
    search.append("M-SEARCH * HTTP/1.1\r\n");
    search.append("HOST: 239.255.255.250:1900\r\n");
    search.append("MAN: \"ssdp:discover\"\r\n");
    search.append("MX: 2\r\n");
    search.append("ST: ");
    search.append(kSearchTarget);
    search.append("\r\n\r\n");

    discovery_->writeDatagram(search, QHostAddress(QString::fromLatin1("239.255.255.250")), 1900);
    discoveryTimeout_->start();
}

void PortMapper::onDiscoveryDatagram()
{
    while (discovery_ && discovery_->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(discovery_->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        discovery_->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        const QString text = QString::fromLatin1(datagram);
        const QString location = headerValue(text, QString::fromLatin1("LOCATION"));
        if (location.isEmpty())
            continue;

        discoveryTimeout_->stop();
        searching_ = false;
        discovery_->close();
        discovery_->deleteLater();
        discovery_ = 0;

        localAddress_ = localAddressFor(QUrl(location).host());
        fetchDescription(location);
        return;
    }
}

void PortMapper::onDiscoveryTimeout()
{
    searching_ = false;
    if (discovery_) {
        discovery_->close();
        discovery_->deleteLater();
        discovery_ = 0;
    }
    emit failed(QString::fromLatin1("No router answered"));
}

void PortMapper::fetchDescription(const QString &location)
{
    QNetworkReply *reply = network_->get(QNetworkRequest(QUrl(location)));
    reply->setProperty("location", location);
    connect(reply, SIGNAL(finished()), this, SLOT(onDescriptionReady()));
}

void PortMapper::onDescriptionReady()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit failed(QString::fromLatin1("The router did not describe itself"));
        return;
    }

    const QString body = QString::fromUtf8(reply->readAll());
    const QString base = reply->property("location").toString();

    // Find the WAN connection service and the control URL that goes with it.
    const char *wanted[] = { "urn:schemas-upnp-org:service:WANIPConnection:1",
                             "urn:schemas-upnp-org:service:WANPPPConnection:1" };
    for (int i = 0; i < 2; ++i) {
        const QString service = QString::fromLatin1(wanted[i]);
        const int at = body.indexOf(service);
        if (at < 0)
            continue;

        const int controlAt = body.indexOf(QString::fromLatin1("<controlURL>"), at);
        if (controlAt < 0)
            continue;
        const int start = controlAt + 12;
        const int end = body.indexOf(QString::fromLatin1("</controlURL>"), start);
        if (end < 0)
            continue;

        serviceType_ = service;
        controlUrl_ = QUrl(base).resolved(QUrl(body.mid(start, end - start).trimmed())).toString();
        addMapping();
        return;
    }

    emit failed(QString::fromLatin1("The router does not offer port forwarding"));
}

QNetworkReply *PortMapper::soap(const QString &action, const QString &body)
{
    QNetworkRequest request((QUrl(controlUrl_)));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QString::fromLatin1("text/xml; charset=\"utf-8\""));
    request.setRawHeader("SOAPAction",
                         ('"' + serviceType_ + '#' + action + '"').toLatin1());

    QString envelope;
    envelope += QString::fromLatin1("<?xml version=\"1.0\"?>");
    envelope += QString::fromLatin1("<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" ");
    envelope += QString::fromLatin1("s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>");
    envelope += QString::fromLatin1("<u:") + action + QString::fromLatin1(" xmlns:u=\"") + serviceType_ + QString::fromLatin1("\">");
    envelope += body;
    envelope += QString::fromLatin1("</u:") + action + QString::fromLatin1("></s:Body></s:Envelope>");

    return network_->post(request, envelope.toUtf8());
}

void PortMapper::addMapping()
{
    if (controlUrl_.isEmpty() || port_ == 0 || localAddress_.isEmpty()) {
        emit failed(QString::fromLatin1("Nothing to map"));
        return;
    }

    QString body;
    body += QString::fromLatin1("<NewRemoteHost></NewRemoteHost>");
    body += QString::fromLatin1("<NewExternalPort>%1</NewExternalPort>").arg(port_);
    body += QString::fromLatin1("<NewProtocol>TCP</NewProtocol>");
    body += QString::fromLatin1("<NewInternalPort>%1</NewInternalPort>").arg(port_);
    body += QString::fromLatin1("<NewInternalClient>%1</NewInternalClient>").arg(localAddress_);
    body += QString::fromLatin1("<NewEnabled>1</NewEnabled>");
    body += QString::fromLatin1("<NewPortMappingDescription>Meeru Messenger</NewPortMappingDescription>");
    body += QString::fromLatin1("<NewLeaseDuration>%1</NewLeaseDuration>").arg(kLeaseSeconds);

    QNetworkReply *reply = soap(QString::fromLatin1("AddPortMapping"), body);
    connect(reply, SIGNAL(finished()), this, SLOT(onMappingReply()));
}

void PortMapper::onMappingReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit failed(QString::fromLatin1("The router refused to forward a port"));
        return;
    }

    mapped_ = true;
    renew_->start();
    queryExternalAddress();
}

void PortMapper::queryExternalAddress()
{
    QNetworkReply *reply = soap(QString::fromLatin1("GetExternalIPAddress"), QString());
    connect(reply, SIGNAL(finished()), this, SLOT(onExternalAddressReply()));
}

void PortMapper::onExternalAddressReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
        return;

    const QString body = QString::fromUtf8(reply->readAll());
    const int start = body.indexOf(QString::fromLatin1("<NewExternalIPAddress>"));
    if (start < 0)
        return;
    const int from = start + 22;
    const int end = body.indexOf(QString::fromLatin1("</NewExternalIPAddress>"), from);
    if (end < 0)
        return;

    const QString ip = body.mid(from, end - from).trimmed();
    if (ip.isEmpty())
        return;

    externalAddress_ = ip + QLatin1Char(':') + QString::number(port_);
    emit mapped(externalAddress_);
}

void PortMapper::onRenewTick()
{
    if (!controlUrl_.isEmpty() && port_ != 0)
        addMapping();
}
