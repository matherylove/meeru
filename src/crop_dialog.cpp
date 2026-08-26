#include "crop_dialog.h"

#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "avatar.h"
#include "meeru_paint.h"
#include "meeru_style.h"

namespace {
const int kCanvasSide = 320;
const int kPreviewSide = 84;
}

CropCanvas::CropCanvas(QWidget *parent)
    : QWidget(parent), movie_(0), scale_(1.0), aspect_(1.0), dragging_(false)
{
    setFixedSize(kCanvasSide, kCanvasSide);
    setCursor(Qt::OpenHandCursor);
}

CropCanvas::~CropCanvas()
{
    if (movie_)
        movie_->stop();
}

void CropCanvas::setAspect(qreal aspect)
{
    aspect_ = aspect > 0.0 ? aspect : 1.0;
    if (!sourceSize_.isEmpty()) {
        setSelectionSide(maximumSide());
    }
}

int CropCanvas::heightFor(int width) const
{
    return qMax(1, qRound(width / aspect_));
}

bool CropCanvas::loadFile(const QString &path, QString *error)
{
    if (movie_) {
        movie_->stop();
        delete movie_;
        movie_ = 0;
    }
    still_ = QImage();

    QImageReader probe(path);
    probe.setAutoTransform(true);
    const bool animated = probe.supportsAnimation() && probe.imageCount() > 1;

    if (animated) {
        // Buffered as well: the source file is copied into the Meeru folder as
        // soon as the user accepts, and Windows would not let us read a file we
        // are still animating from disk in every situation.
        QMovie *movie = MeeruImage::bufferedMovie(path, this);
        if (movie) {
            movie->setCacheMode(QMovie::CacheNone);
            movie->jumpToFrame(0);
            const QImage firstFrame = movie->currentImage();
            if (!firstFrame.isNull()) {
                sourceSize_ = firstFrame.size();
                movie_ = movie;
                connect(movie_, SIGNAL(frameChanged(int)), this, SLOT(onMovieFrame()));
                movie_->start();
            } else {
                delete movie;
            }
        }
    }

    if (!movie_) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        still_ = reader.read();
        if (still_.isNull()) {
            if (error)
                *error = QString::fromLatin1("Meeru could not read that picture");
            return false;
        }
        sourceSize_ = still_.size();
    }

    const int side = maximumSide();
    selection_ = QRect(0, 0, side, heightFor(side));
    selection_.moveCenter(QPoint(sourceSize_.width() / 2, sourceSize_.height() / 2));
    clampSelection();
    recomputeView();
    update();
    emit selectionChanged();
    return true;
}

int CropCanvas::maximumSide() const
{
    if (sourceSize_.isEmpty())
        return 0;
    // The widest crop whose matching height still fits inside the source.
    const int byHeight = qRound(sourceSize_.height() * aspect_);
    return qMax(1, qMin(sourceSize_.width(), byHeight));
}

int CropCanvas::minimumSide() const
{
    return qMax(16, qMin(48, maximumSide()));
}

void CropCanvas::recomputeView()
{
    if (sourceSize_.isEmpty()) {
        viewRect_ = QRectF();
        scale_ = 1.0;
        return;
    }

    const qreal available = qMin(qreal(width()), qreal(height()));
    scale_ = available / qMax(sourceSize_.width(), sourceSize_.height());
    const qreal drawWidth = sourceSize_.width() * scale_;
    const qreal drawHeight = sourceSize_.height() * scale_;
    viewRect_ = QRectF((width() - drawWidth) / 2.0, (height() - drawHeight) / 2.0, drawWidth, drawHeight);
}

void CropCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    recomputeView();
}

void CropCanvas::clampSelection()
{
    if (sourceSize_.isEmpty())
        return;

    const int width = qBound(minimumSide(), selection_.width(), maximumSide());
    const int height = heightFor(width);
    selection_.setWidth(width);
    selection_.setHeight(height);

    const int x = qBound(0, selection_.x(), qMax(0, sourceSize_.width() - width));
    const int y = qBound(0, selection_.y(), qMax(0, sourceSize_.height() - height));
    selection_.moveTo(x, y);
}

void CropCanvas::setSelectionSide(int width)
{
    if (sourceSize_.isEmpty())
        return;

    const QPoint centre = selection_.center();
    selection_.setWidth(width);
    selection_.setHeight(heightFor(width));
    selection_.moveCenter(centre);
    clampSelection();
    update();
    emit selectionChanged();
}

QPointF CropCanvas::toImage(const QPoint &widgetPoint) const
{
    if (scale_ <= 0.0)
        return QPointF();
    return QPointF((widgetPoint.x() - viewRect_.left()) / scale_,
                   (widgetPoint.y() - viewRect_.top()) / scale_);
}

QPixmap CropCanvas::currentSourcePixmap() const
{
    if (movie_)
        return movie_->currentPixmap();
    if (!still_.isNull())
        return QPixmap::fromImage(still_);
    return QPixmap();
}

void CropCanvas::onMovieFrame()
{
    update();
    emit selectionChanged();
}

void CropCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(rect(), QColor(0x19, 0x12, 0x1F));

    const QPixmap source = currentSourcePixmap();
    if (source.isNull() || viewRect_.isEmpty())
        return;

    painter.drawPixmap(viewRect_, source, QRectF(QPointF(0, 0), QSizeF(source.size())));

    const QRectF selectionView(viewRect_.left() + selection_.x() * scale_,
                               viewRect_.top() + selection_.y() * scale_,
                               selection_.width() * scale_,
                               selection_.height() * scale_);

    QPainterPath outside;
    outside.addRect(QRectF(rect()));
    QPainterPath inside;
    inside.addRoundedRect(selectionView, 8, 8);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(11, 7, 15, 165));
    painter.drawPath(outside.subtracted(inside));

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(MeeruStyle::lavender(), 2));
    painter.drawRoundedRect(selectionView, 8, 8);

    painter.setPen(QPen(QColor(255, 255, 255, 60), 1));
    for (int i = 1; i < 3; ++i) {
        const qreal x = selectionView.left() + selectionView.width() * i / 3.0;
        const qreal y = selectionView.top() + selectionView.height() * i / 3.0;
        painter.drawLine(QPointF(x, selectionView.top()), QPointF(x, selectionView.bottom()));
        painter.drawLine(QPointF(selectionView.left(), y), QPointF(selectionView.right(), y));
    }
}

void CropCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || sourceSize_.isEmpty()) {
        QWidget::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    dragStartImage_ = toImage(event->pos());
    dragStartSelection_ = selection_.topLeft();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void CropCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragging_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPointF current = toImage(event->pos());
    const QPointF delta = current - dragStartImage_;
    selection_.moveTo(dragStartSelection_.x() + static_cast<int>(delta.x()),
                      dragStartSelection_.y() + static_cast<int>(delta.y()));
    clampSelection();
    update();
    emit selectionChanged();
    event->accept();
}

void CropCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (dragging_) {
        dragging_ = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void CropCanvas::wheelEvent(QWheelEvent *event)
{
    if (sourceSize_.isEmpty()) {
        QWidget::wheelEvent(event);
        return;
    }
    const int steps = event->angleDelta().y() / 120;
    if (steps != 0) {
        const int step = qMax(4, maximumSide() / 24);
        setSelectionSide(qBound(minimumSide(), selection_.width() - steps * step, maximumSide()));
    }
    event->accept();
}

QImage CropCanvas::croppedStill() const
{
    if (still_.isNull() || !selection_.isValid())
        return QImage();
    return still_.copy(selection_);
}

QPixmap CropCanvas::previewPixmap(int size) const
{
    const QPixmap source = currentSourcePixmap();
    if (source.isNull() || !selection_.isValid())
        return QPixmap();

    const QRect area = selection_.intersected(QRect(QPoint(0, 0), source.size()));
    if (!area.isValid())
        return QPixmap();

    const QSize target(size, qMax(1, qRound(size / aspect_)));
    return MeeruPaint::roundedFromPixmap(source.copy(area), target, qMin(target.width(), target.height()) * 0.2);
}

// ------------------------------------------------------------------ CropDialog

CropDialog::CropDialog(const QString &path, qreal aspect, const QString &title, QWidget *parent)
    : MeeruDialog(title, parent),
      ready_(false), canvas_(0), slider_(0), preview_(0), hint_(0)
{
    setDialogWidth(kCanvasSide + 32);

    canvas_ = new CropCanvas(this);
    canvas_->setAspect(aspect);
    QString error;
    ready_ = canvas_->loadFile(path, &error);

    QLabel *intro = new QLabel(QString::fromLatin1(
        "Drag to move the square, and use the slider or the mouse wheel to resize it."));
    intro->setObjectName(QString::fromLatin1("dialogLabel"));
    intro->setWordWrap(true);
    contentLayout()->addWidget(intro);
    contentLayout()->addWidget(canvas_, 0, Qt::AlignHCenter);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setMinimum(canvas_->minimumSide());
    slider_->setMaximum(qMax(canvas_->minimumSide(), canvas_->maximumSide()));
    slider_->setValue(canvas_->selection().width());
    slider_->setInvertedAppearance(true);
    slider_->setToolTip(QString::fromLatin1("Zoom"));

    QHBoxLayout *sliderRow = new QHBoxLayout();
    sliderRow->setSpacing(10);

    const int previewHeight = qMax(24, qRound(kPreviewSide / (aspect > 0.0 ? aspect : 1.0)));
    preview_ = new QLabel(this);
    preview_->setFixedSize(kPreviewSide, previewHeight);
    preview_->setAlignment(Qt::AlignCenter);

    QVBoxLayout *sliderColumn = new QVBoxLayout();
    hint_ = new QLabel(this);
    hint_->setObjectName(QString::fromLatin1("dialogHint"));
    hint_->setWordWrap(true);
    sliderColumn->addWidget(hint_);
    sliderColumn->addWidget(slider_);

    sliderRow->addWidget(preview_);
    sliderRow->addLayout(sliderColumn, 1);
    contentLayout()->addLayout(sliderRow);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = new QPushButton(QString::fromLatin1("Cancel"), this);
    QPushButton *accept = new QPushButton(QString::fromLatin1("Use this picture"), this);
    accept->setObjectName(QString::fromLatin1("primaryButton"));
    accept->setDefault(true);
    accept->setEnabled(ready_);
    buttons->addWidget(cancel);
    buttons->addWidget(accept);
    contentLayout()->addLayout(buttons);

    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(accept, SIGNAL(clicked()), this, SLOT(accept()));
    connect(slider_, SIGNAL(valueChanged(int)), canvas_, SLOT(setSelectionSide(int)));
    connect(canvas_, SIGNAL(selectionChanged()), this, SLOT(onSelectionChanged()));

    if (!ready_) {
        intro->setText(error);
        canvas_->hide();
        slider_->setEnabled(false);
    }
    onSelectionChanged();
}

bool CropDialog::isAnimated() const
{
    return canvas_ && canvas_->isAnimated();
}

QRect CropDialog::cropRect() const
{
    return canvas_ ? canvas_->selection() : QRect();
}

QImage CropDialog::croppedStill() const
{
    return canvas_ ? canvas_->croppedStill() : QImage();
}

void CropDialog::onSelectionChanged()
{
    if (!canvas_ || !ready_)
        return;

    const QPixmap preview = canvas_->previewPixmap(preview_->width());
    if (!preview.isNull())
        preview_->setPixmap(preview);

    if (slider_->value() != canvas_->selection().width()) {
        const bool blocked = slider_->blockSignals(true);
        slider_->setValue(canvas_->selection().width());
        slider_->blockSignals(blocked);
    }

    hint_->setText(QString::fromLatin1("%1 x %2 taken from a %3 x %4 %5")
                       .arg(canvas_->selection().width())
                       .arg(canvas_->selection().height())
                       .arg(canvas_->sourceSize().width())
                       .arg(canvas_->sourceSize().height())
                       .arg(canvas_->isAnimated() ? QString::fromLatin1("animation")
                                                  : QString::fromLatin1("picture")));
}
