# The rendezvous node, built as its own small executable so it can be dropped
# on a VPS without dragging the whole messenger along. Console only: no widgets,
# no window, nothing to click.

QT += core network
QT -= gui

TARGET = MeeruServer
TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle

INCLUDEPATH += \
    $$PWD/src \
    $$PWD/third_party/monocypher

SOURCES += \
    src/server_main.cpp \
    src/rendezvous.cpp \
    src/identity_crypto.cpp \
    third_party/monocypher/monocypher.c

HEADERS += \
    src/rendezvous.h \
    src/identity_crypto.h \
    third_party/monocypher/monocypher.h

win32 {
    LIBS += -ladvapi32 -lcrypt32
    DEFINES += _CRT_SECURE_NO_WARNINGS WINVER=0x0501 _WIN32_WINNT=0x0501
}

win32-msvc* {
    QMAKE_LFLAGS_CONSOLE += /SUBSYSTEM:CONSOLE,5.01
}
