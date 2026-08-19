QT += core gui widgets network

# Voice notes need the multimedia module. Some static Qt builds are made
# without it, so the feature compiles itself out rather than failing to link.
qtHaveModule(multimedia) {
    QT += multimedia
    DEFINES += MEERU_HAS_AUDIO
}

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = MeeruMessenger
TEMPLATE = app
CONFIG += c++11

INCLUDEPATH += \
    $$PWD/src \
    $$PWD/third_party/monocypher

SOURCES += \
    src/main.cpp \
    src/main_window.cpp \
    src/login_window.cpp \
    src/meeru_paint.cpp \
    src/meeru_window.cpp \
    src/meeru_dialogs.cpp \
    src/crop_dialog.cpp \
    src/avatar.cpp \
    src/voice_recorder.cpp \
    src/transfer_manager.cpp \
    src/emoji_store.cpp \
    src/message_store.cpp \
    src/server_model.cpp \
    src/server_window.cpp \
    src/platform_support.cpp \
    src/camera_source.cpp \
    src/call_engine.cpp \
    src/call_window.cpp \
    src/media_window.cpp \
    src/dm_window.cpp \
    src/roster.cpp \
    src/identity_backup.cpp \
    src/peer_session.cpp \
    src/peer_node.cpp \
    src/contact_card.cpp \
    src/firewall_helper.cpp \
    src/invite_code.cpp \
    src/port_mapper.cpp \
    src/meeru_paths.cpp \
    src/identity_crypto.cpp \
    src/secret_vault.cpp \
    src/identity_store.cpp \
    src/app_settings.cpp \
    third_party/monocypher/monocypher.c

HEADERS += \
    src/main_window.h \
    src/login_window.h \
    src/meeru_style.h \
    src/meeru_paint.h \
    src/meeru_window.h \
    src/meeru_dialogs.h \
    src/crop_dialog.h \
    src/avatar.h \
    src/voice_recorder.h \
    src/transfer_manager.h \
    src/emoji_store.h \
    src/message_store.h \
    src/server_model.h \
    src/server_window.h \
    src/platform_support.h \
    src/camera_source.h \
    src/call_engine.h \
    src/call_window.h \
    src/media_window.h \
    src/dm_window.h \
    src/roster.h \
    src/identity_backup.h \
    src/peer_session.h \
    src/peer_node.h \
    src/contact_card.h \
    src/firewall_helper.h \
    src/invite_code.h \
    src/port_mapper.h \
    src/meeru_paths.h \
    src/identity_crypto.h \
    src/secret_vault.h \
    src/identity_store.h \
    src/app_settings.h \
    src/presence.h \
    third_party/monocypher/monocypher.h

RESOURCES += \
    resources.qrc

win32 {
    # CryptGenRandom lives in advapi32; CryptProtectData in crypt32.
    LIBS += -ladvapi32 -lcrypt32 -lws2_32 -lshell32
    DEFINES += _CRT_SECURE_NO_WARNINGS WINVER=0x0501 _WIN32_WINNT=0x0501
}

win32-msvc* {
    # Windows XP minimum subsystem version for the v141_xp toolset.
    QMAKE_LFLAGS_WINDOWS += /SUBSYSTEM:WINDOWS,5.01
}

DISTFILES += \
    Meeru Black.png \
    Meeru Full.jpg \
    Meeru Trans.png \
    design/meeru-ui-mockup.html \
    THIRD_PARTY_NOTICES.md
