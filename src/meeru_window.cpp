#include "meeru_window.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "meeru_style.h"

TitleBarButton::TitleBarButton(Kind kind, QWidget *parent)
    : QAbstractButton(parent), kind_(kind), hovered_(false)
{
    setFixedSize(29, 22);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    setAttribute(Qt::WA_Hover, true);

    switch (kind_) {
    case Pinned:   setToolTip(QString::fromLatin1("Detach this window")); break;
    case Unpinned: setToolTip(QString::fromLatin1("Attach to the main window")); break;
    case Minimise: setToolTip(QString::fromLatin1("Minimise")); break;
    case Maximise: setToolTip(QString::fromLatin1("Maximise")); break;
    case Restore:  setToolTip(QString::fromLatin1("Restore")); break;
    case Close:    setToolTip(QString::fromLatin1("Close")); break;
    }
}

void TitleBarButton::setKind(Kind kind)
{
    kind_ = kind;
    switch (kind_) {
    case Pinned:   setToolTip(QString::fromLatin1("Detach this window")); break;
    case Unpinned: setToolTip(QString::fromLatin1("Attach to the main window")); break;
    case Restore:  setToolTip(QString::fromLatin1("Restore")); break;
    default:       setToolTip(QString::fromLatin1("Maximise")); break;
    }
    update();
}

void TitleBarButton::enterEvent(QEvent *event)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void TitleBarButton::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void TitleBarButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    if (hovered_ || isDown()) {
        const QColor background = (kind_ == Close)
            ? QColor(0xC0, 0x39, 0x2B, isDown() ? 255 : 220)
            : QColor(255, 255, 255, isDown() ? 55 : 34);
        painter.fillRect(rect(), background);
    }

    const QColor ink = (hovered_ && kind_ == Close) ? QColor(255, 255, 255) : QColor(0xEB, 0xD9, 0xF2);
    painter.setPen(QPen(ink, 1));

    // A 10x10 box in the middle of the button, on whole pixels.
    const int side = 10;
    const int left = (width() - side) / 2;
    const int top = (height() - side) / 2;

    switch (kind_) {
    case Minimise:
        painter.drawLine(left, top + side - 1, left + side - 1, top + side - 1);
        break;
    case Maximise:
        painter.drawRect(left, top, side - 1, side - 1);
        break;
    case Restore:
        painter.drawRect(left, top + 2, side - 3, side - 3);
        painter.drawLine(left + 2, top, left + side - 1, top);
        painter.drawLine(left + side - 1, top, left + side - 1, top + side - 3);
        break;
    case Close:
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.drawLine(QPointF(left + 0.5, top + 0.5),
                         QPointF(left + side - 0.5, top + side - 0.5));
        painter.drawLine(QPointF(left + side - 0.5, top + 0.5),
                         QPointF(left + 0.5, top + side - 0.5));
        break;

    case Pinned:
    case Unpinned: {
        // A pushpin: filled while attached, hollow once it floats free.
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal cx = left + side / 2.0;
        const QRectF head(cx - 3.5, top + 0.5, 7.0, 6.0);
        painter.setBrush(kind_ == Pinned ? QBrush(ink) : QBrush(Qt::NoBrush));
        painter.drawRoundedRect(head, 2.0, 2.0);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(cx, head.bottom()), QPointF(cx, top + side - 0.5));
        painter.drawLine(QPointF(cx - 4.5, head.bottom()), QPointF(cx + 4.5, head.bottom()));
        break;
    }
    }
}

MeeruTitleBar::MeeruTitleBar(const QString &title, bool withMinimise, bool withMaximise, QWidget *parent)
    : QWidget(parent),
      titleLabel_(0),
      maximiseButton_(0),
      pinButton_(0),
      layout_(0),
      pinned_(true),
      dragging_(false),
      canMaximise_(withMaximise)
{
    setObjectName(QString::fromLatin1("titleBar"));
    setFixedHeight(30);
    setAttribute(Qt::WA_StyledBackground, true);

    layout_ = new QHBoxLayout(this);
    QHBoxLayout *layout = layout_;
    layout->setContentsMargins(12, 0, 6, 0);
    layout->setSpacing(2);

    titleLabel_ = new QLabel(title, this);
    titleLabel_->setObjectName(QString::fromLatin1("titleBarText"));
    layout->addWidget(titleLabel_);
    layout->addStretch();

    if (withMinimise) {
        TitleBarButton *minimise = new TitleBarButton(TitleBarButton::Minimise, this);
        connect(minimise, SIGNAL(clicked()), this, SLOT(onMinimise()));
        layout->addWidget(minimise);
    }
    if (withMaximise) {
        maximiseButton_ = new TitleBarButton(TitleBarButton::Maximise, this);
        connect(maximiseButton_, SIGNAL(clicked()), this, SLOT(onMaximise()));
        layout->addWidget(maximiseButton_);
    }

    TitleBarButton *closeButton = new TitleBarButton(TitleBarButton::Close, this);
    connect(closeButton, SIGNAL(clicked()), this, SLOT(onClose()));
    layout->addWidget(closeButton);
}

void MeeruTitleBar::setTitle(const QString &title)
{
    titleLabel_->setText(title);
}

void MeeruTitleBar::addPinButton(bool pinned)
{
    if (pinButton_ || !layout_)
        return;

    pinned_ = pinned;
    pinButton_ = new TitleBarButton(pinned ? TitleBarButton::Pinned : TitleBarButton::Unpinned, this);
    connect(pinButton_, SIGNAL(clicked()), this, SLOT(onPin()));

    // Left of minimise, so the destructive button stays on the far right where
    // people expect it.
    layout_->insertWidget(layout_->count() - (maximiseButton_ ? 3 : 2), pinButton_);
}

void MeeruTitleBar::setPinned(bool pinned)
{
    pinned_ = pinned;
    if (pinButton_)
        pinButton_->setKind(pinned ? TitleBarButton::Pinned : TitleBarButton::Unpinned);
}

void MeeruTitleBar::onPin()
{
    pinned_ = !pinned_;
    setPinned(pinned_);
    emit pinToggled(pinned_);
}

void MeeruTitleBar::onMinimise()
{
    if (window())
        window()->showMinimized();
}

void MeeruTitleBar::onMaximise()
{
    toggleMaximise();
}

void MeeruTitleBar::onClose()
{
    if (window())
        window()->close();
}

void MeeruTitleBar::toggleMaximise()
{
    QWidget *top = window();
    if (!top || !canMaximise_)
        return;

    if (top->isMaximized()) {
        top->showNormal();
        if (maximiseButton_)
            maximiseButton_->setKind(TitleBarButton::Maximise);
    } else {
        top->showMaximized();
        if (maximiseButton_)
            maximiseButton_->setKind(TitleBarButton::Restore);
    }
}

void MeeruTitleBar::mousePressEvent(QMouseEvent *event)
{
    QWidget *top = window();
    if (event->button() == Qt::LeftButton && top && !top->isMaximized()) {
        dragging_ = true;
        dragOffset_ = event->globalPos() - top->frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void MeeruTitleBar::mouseMoveEvent(QMouseEvent *event)
{
    QWidget *top = window();
    if (dragging_ && top && (event->buttons() & Qt::LeftButton)) {
        top->move(event->globalPos() - dragOffset_);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void MeeruTitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    dragging_ = false;
    QWidget::mouseReleaseEvent(event);
}

void MeeruTitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && canMaximise_) {
        toggleMaximise();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

// -------------------------------------------------------------- ClickableLabel

ClickableLabel::ClickableLabel(QWidget *parent)
    : QLabel(parent)
{
    setCursor(Qt::PointingHandCursor);
}

void ClickableLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        emit clicked();
        event->accept();
        return;
    }
    QLabel::mouseReleaseEvent(event);
}

void ClickableLabel::enterEvent(QEvent *event)
{
    QLabel::enterEvent(event);
}

// ----------------------------------------------------------------- MeeruDialog

MeeruDialog::MeeruDialog(const QString &title, QWidget *parent)
    : QDialog(parent), contentLayout_(0)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowTitle(title);
    setStyleSheet(MeeruStyle::sheet());

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    QWidget *root = new QWidget(this);
    root->setObjectName(QString::fromLatin1("dialogRoot"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(root);

    QVBoxLayout *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(new MeeruTitleBar(title, false, false, root));

    QWidget *content = new QWidget(root);
    rootLayout->addWidget(content, 1);

    contentLayout_ = new QVBoxLayout(content);
    contentLayout_->setContentsMargins(16, 14, 16, 14);
    contentLayout_->setSpacing(10);
}

void MeeruDialog::setDialogWidth(int width)
{
    setFixedWidth(width);
}

void MeeruDialog::showMessage(QWidget *parent, const QString &title, const QString &message)
{
    MeeruDialog dialog(title, parent);
    dialog.setDialogWidth(360);

    QLabel *label = new QLabel(message);
    label->setObjectName(QString::fromLatin1("dialogLabel"));
    label->setWordWrap(true);
    dialog.contentLayout()->addWidget(label);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *ok = new QPushButton(QString::fromLatin1("OK"));
    ok->setObjectName(QString::fromLatin1("primaryButton"));
    ok->setDefault(true);
    buttons->addWidget(ok);
    dialog.contentLayout()->addLayout(buttons);

    QObject::connect(ok, SIGNAL(clicked()), &dialog, SLOT(accept()));
    dialog.exec();
}

bool MeeruDialog::promptText(QWidget *parent, const QString &title, const QString &caption,
                             QString *value)
{
    if (!value)
        return false;

    MeeruDialog dialog(title, parent);
    dialog.setDialogWidth(340);

    QLabel *label = new QLabel(caption);
    label->setObjectName(QString::fromLatin1("dialogLabel"));
    label->setWordWrap(true);
    dialog.contentLayout()->addWidget(label);

    QLineEdit *edit = new QLineEdit();
    edit->setFixedHeight(30);
    edit->setText(*value);
    dialog.contentLayout()->addWidget(edit);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = new QPushButton(QString::fromLatin1("Cancel"));
    QPushButton *accept = new QPushButton(QString::fromLatin1("Save"));
    accept->setObjectName(QString::fromLatin1("primaryButton"));
    accept->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(accept);
    dialog.contentLayout()->addLayout(buttons);

    QObject::connect(cancel, SIGNAL(clicked()), &dialog, SLOT(reject()));
    QObject::connect(accept, SIGNAL(clicked()), &dialog, SLOT(accept()));
    QObject::connect(edit, SIGNAL(returnPressed()), &dialog, SLOT(accept()));

    edit->setFocus();
    edit->selectAll();
    if (dialog.exec() != QDialog::Accepted)
        return false;

    *value = edit->text().trimmed();
    return !value->isEmpty();
}

bool MeeruDialog::confirm(QWidget *parent, const QString &title, const QString &message, const QString &acceptText)
{
    MeeruDialog dialog(title, parent);
    dialog.setDialogWidth(360);

    QLabel *label = new QLabel(message);
    label->setObjectName(QString::fromLatin1("dialogLabel"));
    label->setWordWrap(true);
    dialog.contentLayout()->addWidget(label);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = new QPushButton(QString::fromLatin1("Cancel"));
    QPushButton *accept = new QPushButton(acceptText);
    accept->setObjectName(QString::fromLatin1("primaryButton"));
    accept->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(accept);
    dialog.contentLayout()->addLayout(buttons);

    QObject::connect(cancel, SIGNAL(clicked()), &dialog, SLOT(reject()));
    QObject::connect(accept, SIGNAL(clicked()), &dialog, SLOT(accept()));
    return dialog.exec() == QDialog::Accepted;
}
