#ifndef LOGIN_WINDOW_H
#define LOGIN_WINDOW_H

#include <QColor>
#include <QList>
#include <QRect>
#include <QMainWindow>

#include "app_settings.h"
#include "identity_store.h"
#include "meeru_paths.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QWidget;
class QLabel;
class QPropertyAnimation;
class QMovie;

class AvatarFrame;
class MeeruTitleBar;

class PresenceFrame : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor presenceColor READ presenceColor WRITE setPresenceColorImmediate)

public:
    explicit PresenceFrame(QWidget *parent = nullptr);
    void setInnerWidget(QWidget *widget);
    void animateToColor(const QColor &color);
    void setPicture(const QPixmap &pixmap);   // replaces the artwork, keeps the frame
    void setAnimation(const QString &path, const QRect &crop);
    void clearAnimation();

    QColor presenceColor() const { return color_; }
    void setPresenceColorImmediate(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onMovieFrame();

private:
    void updateClip();
    void applyPicture(const QPixmap &pixmap);

    QColor color_;
    QWidget *inner_;
    QPixmap original_;
    QMovie *movie_;
    QRect crop_;
    QPropertyAnimation *animation_;
};

class LoginWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit LoginWindow(QWidget *parent = nullptr);

private slots:
    void onPresenceChanged(int index);
    void onContinue();
    void onImportBackup();
    void onCreateNewIdentity();
    void onLogOut();
    void onPreviousIdentity();
    void onNextIdentity();

private:
    void buildUi();
    void reloadIdentities();
    void showIdentity(int index);
    void applyModeTexts();
    void setBusy(bool busy);
    void openMainWindow(const LocalProfile &profile);
    void refreshIdentityArtwork();
    bool exportBackupFor(const LocalProfile &profile);

    bool hasStoredIdentity() const;
    LocalProfile currentIdentity() const;

    QWidget *root_;
    QLineEdit *displayName_;
    QComboBox *presenceSelector_;
    QCheckBox *autoStart_;
    QPushButton *createIdentity_;
    QPushButton *importBackup_;
    QPushButton *newIdentityButton_;
    QPushButton *logOutButton_;
    QPushButton *settingsButton_;
    QPushButton *previousButton_;
    QPushButton *nextButton_;
    QLabel *headline_;
    QLabel *subhead_;
    QLabel *cardTitle_;
    QLabel *cardBody_;
    QLabel *newIdentityHint_;
    QLabel *logOutHint_;
    PresenceFrame *logoFrame_;
    AvatarFrame *cardAvatar_;
    QLabel *cardLogo_;
    QLabel *presenceDot_;
    QLabel *presenceText_;
    MeeruTitleBar *titleBar_;

    MeeruPaths paths_;
    bool pathsReady_;
    QList<LocalProfile> identities_;
    int currentIndex_;
    bool creatingNew_;
};

#endif // LOGIN_WINDOW_H
