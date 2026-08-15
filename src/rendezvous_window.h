#ifndef MEERU_RENDEZVOUS_WINDOW_H
#define MEERU_RENDEZVOUS_WINDOW_H

#include <QWidget>

class QLabel;
class QTextEdit;
class RendezvousServer;

// The console for an operator running Meeru as a meeting point.
class RendezvousWindow : public QWidget
{
    Q_OBJECT

public:
    explicit RendezvousWindow(quint16 port, QWidget *parent = 0);

private slots:
    void onLog(const QString &text);

private:
    RendezvousServer *server_;
    QLabel *headline_;
    QTextEdit *log_;
};

#endif
