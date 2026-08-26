#ifndef MEERU_CONTACT_CARD_H
#define MEERU_CONTACT_CARD_H

#include <QString>
#include <QWidget>

#include "avatar.h"
#include "meeru_paths.h"
#include "roster.h"

class QLabel;

// The card that appears when the pointer rests on a contact: the same shape as
// the profile header at the top of the main window, but showing that contact's
// banner, picture, name, state and personal message.
class ContactCard : public QWidget
{
    Q_OBJECT

public:
    explicit ContactCard(QWidget *parent = 0);

    void showFor(const Roster::Contact &contact,
                 const MeeruPaths &paths,
                 const QString &ownerId,
                 bool online,
                 const QPoint &globalPosition);
    void hideCard();

private:
    BannerFrame *banner_;
    AvatarFrame *avatar_;
    QLabel *nameLabel_;
    QLabel *stateLabel_;
    QLabel *presenceDot_;
    QLabel *statusLabel_;
    QLabel *footerLabel_;
};

#endif
