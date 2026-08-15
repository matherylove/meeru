#include "rendezvous_window.h"

#include <QDateTime>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>

#include "meeru_style.h"
#include "meeru_window.h"
#include "rendezvous.h"

RendezvousWindow::RendezvousWindow(quint16 port, QWidget *parent)
    : QWidget(parent), server_(0), headline_(0), log_(0)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowTitle(QString::fromLatin1("Meeru rendezvous node"));
    setStyleSheet(MeeruStyle::sheet());
    setMinimumSize(460, 340);
    resize(520, 400);

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    QWidget *root = new QWidget(this);
    root->setObjectName(QString::fromLatin1("meeruRoot"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(root);

    QVBoxLayout *layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new MeeruTitleBar(QString::fromLatin1("Meeru rendezvous node"), true, true, root));

    QWidget *body = new QWidget(root);
    QVBoxLayout *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(14, 12, 14, 12);
    bodyLayout->setSpacing(8);

    headline_ = new QLabel(body);
    headline_->setObjectName(QString::fromLatin1("newsTitle"));
    headline_->setWordWrap(true);
    bodyLayout->addWidget(headline_);

    QLabel *explanation = new QLabel(body);
    explanation->setObjectName(QString::fromLatin1("dialogLabel"));
    explanation->setWordWrap(true);
    explanation->setText(QString::fromLatin1(
        "People put this address into their Meeru settings. This node introduces contacts to each other "
        "and carries the traffic when their routers will not let them speak directly.\n\n"
        "Everything passing through is sealed between the two people talking, so this machine cannot read "
        "messages, cannot pretend to be anybody, and holds nothing once a connection ends. What it does "
        "know is which identities are online and who connects to whom."));
    bodyLayout->addWidget(explanation);

    log_ = new QTextEdit(body);
    log_->setReadOnly(true);
    log_->setStyleSheet(QString::fromLatin1(
        "QTextEdit { background: #19121f; border: 1px solid #634A70; border-radius: 6px;"
        "            color: #C9B9CF; font-family: 'Consolas', 'Courier New'; font-size: 11px; }"));
    bodyLayout->addWidget(log_, 1);

    layout->addWidget(body, 1);

    server_ = new RendezvousServer(this);
    connect(server_, SIGNAL(logMessage(QString)), this, SLOT(onLog(QString)));

    QString error;
    if (server_->listen(port, &error)) {
        headline_->setText(QString::fromLatin1("Running on port %1").arg(server_->port()));
    } else {
        headline_->setText(QString::fromLatin1("Could not listen on port %1").arg(port));
        onLog(error);
    }
}

void RendezvousWindow::onLog(const QString &text)
{
    log_->append(QDateTime::currentDateTime().toString(QString::fromLatin1("HH:mm:ss"))
                 + QString::fromLatin1("  ") + text);
}
