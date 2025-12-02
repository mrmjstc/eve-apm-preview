QT       += core gui widgets network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17 static

# Static linking for MinGW runtime (fully static)
QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    updaterdialog.cpp \
    updatemanager.cpp

HEADERS += \
    updaterdialog.h \
    updatemanager.h

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

TARGET = EVEAPMUpdater
DESTDIR = ..

# Application icon
RC_ICONS = icon.ico

# Resources
RESOURCES += resources.qrc
