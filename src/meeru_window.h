#ifndef MEERU_WINDOW_H
#define MEERU_WINDOW_H

#include <QAbstractButton>
#include <QDialog>
#include <QLabel>
#include <QPoint>
#include <QString>
#include <QWidget>

class QVBoxLayout;
class QHBoxLayout;

// Minimise / maximise / close, drawn with the painter instead of glyphs so
// they land on exact pixels at any DPI and do not depend on a font having the
// right symbols installed.
class TitleBarButton : public QAbstractButton
{
    Q_OBJECT

public:
    enum Kind {
        Minimise,
        Maximise,
        Restore,
        Close,
        Pinned,      // docked to the main window
        Unpinned     // free floating
    };

    TitleBarButton(Kind kind, QWidget *parent = 0);
    void setKind(Kind kind);

protected:
    void paintEvent(QPaintEvent *event);
    void enterEvent(QEvent *event);
    void leaveEvent(QEvent *event);

private:
    Kind kind_;
    bool hovered_;
};

// Draggable title bar for the frameless Meeru windows.
class MeeruTitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit MeeruTitleBar(const QString &title, bool withMinimise, bool withMaximise,
                           QWidget *parent = 0);
    void setTitle(const QString &title);

    // Adds the pin, left of the other buttons. Pressing it is what detaches a
    // window from the main one, and pressing it again brings it back.
    void addPinButton(bool pinned);
    void setPinned(bool pinned);

signals:
    void pinToggled(bool pinned);

protected:
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void mouseDoubleClickEvent(QMouseEvent *event);

private slots:
    void onPin();
    void onMinimise();
    void onMaximise();
    void onClose();

private:
    void toggleMaximise();

    QLabel *titleLabel_;
    TitleBarButton *maximiseButton_;
    TitleBarButton *pinButton_;
    QHBoxLayout *layout_;
    bool pinned_;
    QPoint dragOffset_;
    bool dragging_;
    bool canMaximise_;
};

// A label that reports clicks, used for the editable status lines.
class ClickableLabel : public QLabel
{
    Q_OBJECT

public:
    explicit ClickableLabel(QWidget *parent = 0);

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent *event);
    void enterEvent(QEvent *event);
};

// Frameless dialog sharing the main window's chrome and palette.
class MeeruDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MeeruDialog(const QString &title, QWidget *parent = 0);

    QVBoxLayout *contentLayout() const { return contentLayout_; }
    void setDialogWidth(int width);

    static void showMessage(QWidget *parent, const QString &title, const QString &message);
    static bool confirm(QWidget *parent, const QString &title, const QString &message,
                        const QString &acceptText = QString::fromLatin1("Continue"));

private:
    QVBoxLayout *contentLayout_;
};

#endif
