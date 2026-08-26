#ifndef MEERU_PORT_MAPPER_H
#define MEERU_PORT_MAPPER_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QUdpSocket;

// Asks the home router to forward a port, so contacts can reach this machine
// directly instead of going through a relay.
//
// This is best effort by design. Plenty of routers have UPnP switched off, and
// people behind carrier grade NAT have no router to ask at all, which is why
// Meeru always keeps the relay as a fallback rather than depending on this.
class PortMapper : public QObject
{
    Q_OBJECT

public:
    explicit PortMapper(QObject *parent = 0);

    void requestMapping(quint16 port);
    void release();
    QString externalAddress() const { return externalAddress_; }

signals:
    void mapped(const QString &externalAddress);
    void failed(const QString &reason);

private slots:
    void onDiscoveryDatagram();
    void onDiscoveryTimeout();
    void onDescriptionReady();
    void onMappingReply();
    void onExternalAddressReply();
    void onRenewTick();

private:
    void discover();
    void fetchDescription(const QString &location);
    void addMapping();
    void queryExternalAddress();
    QNetworkReply *soap(const QString &action, const QString &body);

    QNetworkAccessManager *network_;
    QUdpSocket *discovery_;
    QTimer *discoveryTimeout_;
    QTimer *renew_;

    QString controlUrl_;
    QString serviceType_;
    QString localAddress_;
    QString externalAddress_;
    quint16 port_;
    bool mapped_;
    bool searching_;
};

#endif
