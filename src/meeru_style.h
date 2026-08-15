#ifndef MEERU_STYLE_H
#define MEERU_STYLE_H

#include <QColor>
#include <QString>

// Palette and shared stylesheet, taken from design/meeru-ui-mockup.html.
namespace MeeruStyle {

inline QColor lavender()  { return QColor(0xDF, 0xB2, 0xF4); }
inline QColor pink()      { return QColor(0xF4, 0x90, 0x97); }
inline QColor plum()      { return QColor(0x4E, 0x35, 0x62); }
inline QColor surface()   { return QColor(0x24, 0x1B, 0x2E); }
inline QColor surface2()  { return QColor(0x30, 0x22, 0x3B); }
inline QColor surface3()  { return QColor(0x3B, 0x2B, 0x48); }
inline QColor line()      { return QColor(0x63, 0x4A, 0x70); }
inline QColor muted()     { return QColor(0xC9, 0xB9, 0xCF); }
inline QColor text()      { return QColor(0xFF, 0xF7, 0xFC); }
inline QColor avatarInk() { return QColor(0x4F, 0x36, 0x5B); }

inline QString sheet()
{
    return QString::fromLatin1(
        "QWidget { color: #FFF7FC; font-family: 'Segoe UI', Tahoma, Arial; font-size: 11px; }"
        "#meeruRoot { background: #30223B; border: 1px solid #87659a; }"
        "#dialogRoot { background: #241B2E; border: 1px solid #87659a; }"

        "#titleBar { background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "            stop:0 #72518A, stop:1 #4E3562); border-bottom: 1px solid #634A70; }"
        "#titleBarText { font-weight: bold; font-size: 11px; color: #FFF7FC; background: transparent; }"
        "#titleBarButton, #titleBarClose { background: transparent; border: 0; color: #EBD9F2;"
        "                                  font-size: 13px; font-weight: normal; padding: 0; }"
        "#titleBarButton:hover { background: rgba(255,255,255,0.16); }"
        "#titleBarClose:hover { background: #C0392B; color: #ffffff; }"

        // Pixel sizes rather than points, so the text sits on the same line
        // boxes as the mockup whatever the system font scaling is.
        "#profileName { font-size: 16px; font-weight: bold; background: transparent; }"
        "#profileState { color: #eee0f3; font-size: 11px; background: transparent; }"
        "#profileState:hover { color: #ffffff; }"
        "#personalMessage { color: #f5e9f8; font-size: 10px; background: transparent; }"
        "#personalMessage:hover { color: #ffffff; }"
        "#personalEdit { background: rgba(20,12,26,0.55); border: 1px solid #b98fc9; border-radius: 4px;"
        "                color: #ffffff; font-size: 10px; padding: 0 5px; }"

        "#searchRow { background: #211827; border-bottom: 1px solid #634A70; }"
        "QLineEdit { background: #19121f; border: 1px solid #634A70; border-radius: 6px;"
        "            color: #FFF7FC; font-size: 11px; padding: 0 8px; }"
        "QLineEdit:focus { border-color: #DFB2F4; }"
        "#glassButton { background: #33243d; border: 1px solid #634A70; border-radius: 6px;"
        "               color: #DFB2F4; font-size: 15px; padding: 0; }"
        "#glassButton:hover { background: #43304f; color: #ffffff; }"
        "#glassButton:pressed { background: #2a1e33; }"

        "#tabRow { background: #2a1e33; border-bottom: 1px solid #634A70; }"
        "#tabButton { background: transparent; border: 0; border-bottom: 2px solid transparent;"
        "             color: #C9B9CF; font-size: 11px; padding: 5px 9px; }"
        "#tabButton:hover { color: #FFF7FC; }"
        "#tabButton:checked { color: #DFB2F4; font-weight: bold; border-bottom: 2px solid #DFB2F4; }"

        "QListWidget { background: #30223B; border: 0; outline: 0; }"
        "QListWidget::item { border: 0; }"
        "QStackedWidget { background: transparent; border: 0; }"
        "QAbstractScrollArea { background: #30223B; border: 0; }"

        "#emptyState { color: #C9B9CF; font-size: 11px; background: #30223B; }"
        "#news { background: #2a1e33; border-top: 1px solid #634A70; color: #C9B9CF; font-size: 11px; }"
        "#newsTitle { color: #FFF7FC; font-weight: bold; font-size: 11px; background: transparent; }"
        "#footer { background: #1d1524; border-top: 1px solid #634A70; }"
        "#footerText { color: #C9B9CF; font-size: 9px; background: transparent; }"
        "#footerLink { color: #DFB2F4; font-size: 9px; background: transparent; }"
        "#footerLink:hover { color: #ffffff; }"
        "#settingsButton { color: #dbc8df; background: transparent; border: 0; padding: 0;"
        "                  font: 16px 'Segoe UI Symbol'; }"
        "#settingsButton:hover { color: #ffffff; background: #3a2843; border-radius: 4px; }"

        "QPushButton { background: #3a2843; border: 1px solid #634A70; border-radius: 4px;"
        "              color: #FFF7FC; font-size: 11px; font-weight: bold; padding: 6px 14px; }"
        "QPushButton:hover { background: #48334f; }"
        "QPushButton:pressed { background: #2f2139; }"
        "QPushButton:disabled { color: #7d6c86; background: #2b1f34; border-color: #4a3a55; }"
        "#primaryButton { background: #F49097; border: 1px solid #f7a8ad; color: #33233b; }"
        "#primaryButton:hover { background: #f7a3a9; }"
        "#primaryButton:pressed { background: #d97f86; }"
        "#primaryButton:disabled { background: #6a4a51; border-color: #6a4a51; color: #a58f93; }"

        // Left padding leaves a proper gutter for the icon instead of letting
        // the dot sit against the text, and every row gets room to breathe.
        "QMenu { background: #251C30; border: 1px solid #6f5480; padding: 5px; }"
        "QMenu::item { padding: 7px 26px 7px 32px; margin: 1px 2px; border-radius: 4px;"
        "              color: #F1E6F5; font-size: 11px; }"
        "QMenu::item:selected { background: #4a3454; color: #ffffff; }"
        "QMenu::item:disabled { color: #7d6c86; }"
        "QMenu::icon { left: 10px; }"
        "QMenu::separator { height: 1px; background: #4a3757; margin: 5px 10px; }"

        "QCheckBox { color: #E4D6EA; font-size: 11px; spacing: 7px; background: transparent; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border-radius: 3px;"
        "                       border: 1px solid #9b708e; background: #19121f; }"
        "QCheckBox::indicator:checked { background: #DFB2F4; border-color: #DFB2F4; }"
        "QRadioButton { color: #E4D6EA; font-size: 11px; spacing: 7px; background: transparent; }"
        "QRadioButton::indicator { width: 14px; height: 14px; border-radius: 7px;"
        "                          border: 1px solid #9b708e; background: #19121f; }"
        "QRadioButton::indicator:checked { background: #DFB2F4; border-color: #DFB2F4; }"

        "QScrollBar:vertical { background: transparent; width: 9px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #55406180; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: #6d5279; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"

        "QSlider::groove:horizontal { height: 4px; background: #19121f; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #DFB2F4; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; height: 12px; margin: -5px 0;"
        "                             border-radius: 6px; background: #F49097; }"

        "#dialogLabel { color: #E4D6EA; font-size: 11px; background: transparent; }"
        "#dialogHint { color: #A08FA8; font-size: 9px; background: transparent; }"
        "#sectionLabel { color: #C9B9CF; font-size: 9px; font-weight: bold; background: transparent; }"
    );
}

}

#endif
