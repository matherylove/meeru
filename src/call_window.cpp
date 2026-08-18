#include "call_window.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "meeru_style.h"
#include "meeru_window.h"

namespace {
const int kWindowWidth = 480;
const int kStageHeight = 300;
const int kDockGap = 6;
}

CallWindow::CallWindow(const LocalProfile &profile,
                       const QString &title,
                       CallEngine *engine,
                       QWidget *anchor,
                       QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint),
      profile_(profile), engine_(engine), anchor_(anchor), pinned_(true),
      titleBar_(0), stage_(0), status_(0), muteButton_(0), screenButton_(0)
{
    setWindowTitle(title);
    setStyleSheet(MeeruStyle::sheet());
    setFixedWidth(kWindowWidth);

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

    titleBar_ = new MeeruTitleBar(title, true, false, root);
    titleBar_->addPinButton(true);
    connect(titleBar_, SIGNAL(pinToggled(bool)), this, SLOT(onPinToggled(bool)));
    layout->addWidget(titleBar_);

    stage_ = new QLabel(root);
    stage_->setObjectName(QString::fromLatin1("mediaStage"));
    stage_->setAlignment(Qt::AlignCenter);
    stage_->setFixedHeight(kStageHeight);
    stage_->setAttribute(Qt::WA_StyledBackground, true);
    stage_->setText(QString::fromLatin1("Connecting..."));
    layout->addWidget(stage_);

    status_ = new QLabel(root);
    status_->setObjectName(QString::fromLatin1("dialogLabel"));
    status_->setWordWrap(true);
    status_->setContentsMargins(12, 8, 12, 4);
    status_->setText(CallEngine::hardwareNote());
    layout->addWidget(status_);

    QHBoxLayout *controls = new QHBoxLayout();
    controls->setContentsMargins(12, 4, 12, 10);
    controls->setSpacing(6);

    muteButton_ = new QPushButton(QString::fromLatin1("Mute"), root);
    muteButton_->setCheckable(true);
    screenButton_ = new QPushButton(QString::fromLatin1("Share screen"), root);
    screenButton_->setCheckable(true);
    QPushButton *hangUp = new QPushButton(QString::fromLatin1("Hang up"), root);
    hangUp->setObjectName(QString::fromLatin1("dangerButton"));

    controls->addWidget(muteButton_);
    controls->addWidget(screenButton_);
    controls->addStretch();
    controls->addWidget(hangUp);
    layout->addLayout(controls);

    connect(muteButton_, SIGNAL(clicked()), this, SLOT(onMute()));
    connect(screenButton_, SIGNAL(clicked()), this, SLOT(onScreen()));
    connect(hangUp, SIGNAL(clicked()), this, SLOT(onHangUp()));

    if (engine_) {
        connect(engine_, SIGNAL(stateChanged(int)), this, SLOT(onStateChanged(int)));
        connect(engine_, SIGNAL(frameReceived(QString,QImage,int)),
                this, SLOT(onFrame(QString,QImage,int)));
        onStateChanged(engine_->state());
    }

    adjustSize();
    followAnchor();
}

void CallWindow::setPeerNames(const QHash<QString, QString> &names)
{
    names_ = names;
}

void CallWindow::onStateChanged(int state)
{
    switch (state) {
    case CallEngine::Calling:
        stage_->setText(QString::fromLatin1("Ringing..."));
        break;
    case CallEngine::Ringing:
        stage_->setText(QString::fromLatin1("Incoming call"));
        break;
    case CallEngine::Active:
        if (stage_->pixmap() == 0)
            stage_->setText(QString::fromLatin1("In a call.\nNobody is sharing a picture yet."));
        break;
    default:
        close();
        break;
    }
}

void CallWindow::onFrame(const QString &peerId, const QImage &frame, int source)
{
    if (frame.isNull())
        return;

    // One stage: whoever sent the most recent frame is what is shown, which is
    // the behaviour that matters when somebody is sharing a screen.
    stage_->setPixmap(QPixmap::fromImage(frame).scaled(
        kWindowWidth - 8, kStageHeight - 8, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    const QString who = peerId.isEmpty() ? QString::fromLatin1("You")
                                         : names_.value(peerId, peerId.left(12));
    status_->setText(source == CallEngine::SourceScreen
        ? QString::fromLatin1("%1 is sharing a screen").arg(who)
        : QString::fromLatin1("%1 is on camera").arg(who));
}

void CallWindow::onMute()
{
    if (!engine_)
        return;
    engine_->setMuted(muteButton_->isChecked());
    muteButton_->setText(muteButton_->isChecked() ? QString::fromLatin1("Unmute")
                                                  : QString::fromLatin1("Mute"));
}

void CallWindow::onScreen()
{
    if (!engine_)
        return;
    engine_->setScreenSharing(screenButton_->isChecked());
    screenButton_->setText(screenButton_->isChecked() ? QString::fromLatin1("Stop sharing")
                                                      : QString::fromLatin1("Share screen"));
}

void CallWindow::onHangUp()
{
    if (engine_)
        engine_->hangUp();
    close();
}

void CallWindow::onPinToggled(bool pinned)
{
    pinned_ = pinned;
    if (pinned_)
        followAnchor();
}

void CallWindow::followAnchor()
{
    if (!pinned_ || !anchor_ || !anchor_->isVisible())
        return;
    const QRect frame = anchor_->frameGeometry();
    move(frame.right() + kDockGap, frame.top());
}

void CallWindow::closeEvent(QCloseEvent *event)
{
    if (engine_ && engine_->state() != CallEngine::Idle)
        engine_->hangUp();
    emit closed();
    event->accept();
}
