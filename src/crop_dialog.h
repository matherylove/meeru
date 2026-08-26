#ifndef MEERU_CROP_DIALOG_H
#define MEERU_CROP_DIALOG_H

#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

#include "meeru_window.h"

class QLabel;
class QMovie;
class QSlider;

// Square crop area over a still image or an animated GIF.
class CropCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit CropCanvas(QWidget *parent = 0);
    ~CropCanvas();

    void setAspect(qreal aspect);        // width / height of the crop area
    bool loadFile(const QString &path, QString *error);
    bool isAnimated() const { return movie_ != 0; }
    QRect selection() const { return selection_; }
    QSize sourceSize() const { return sourceSize_; }
    int maximumSide() const;             // widest crop the source allows
    int minimumSide() const;

    QImage croppedStill() const;      // still images only
    QPixmap previewPixmap(int size) const;

public slots:
    void setSelectionSide(int width);

signals:
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void wheelEvent(QWheelEvent *event);
    void resizeEvent(QResizeEvent *event);

private slots:
    void onMovieFrame();

private:
    void recomputeView();
    void clampSelection();
    int heightFor(int width) const;
    QPointF toImage(const QPoint &widgetPoint) const;
    QPixmap currentSourcePixmap() const;

    QImage still_;
    QMovie *movie_;
    QSize sourceSize_;
    QRect selection_;
    qreal aspect_;
    QRectF viewRect_;
    qreal scale_;
    bool dragging_;
    QPointF dragStartImage_;
    QPoint dragStartSelection_;
};

class CropDialog : public MeeruDialog
{
    Q_OBJECT

public:
    CropDialog(const QString &path, qreal aspect, const QString &title, QWidget *parent = 0);

    bool isReady() const { return ready_; }
    bool isAnimated() const;
    QRect cropRect() const;
    QImage croppedStill() const;

private slots:
    void onSelectionChanged();

private:
    bool ready_;
    CropCanvas *canvas_;
    QSlider *slider_;
    QLabel *preview_;
    QLabel *hint_;
};

#endif
