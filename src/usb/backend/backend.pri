INCLUDEPATH += $$PWD
VPATH += $$PWD

HEADERS += native_usb.h
SOURCES += native_usb.cpp

win32 {
    INCLUDEPATH += binary/win/libusb-1.0
    LIBS += binary/win/libusb-1.0/libusb-1.0.a
}

linux {
    INCLUDEPATH += /usr/include/libusb-1.0
    LIBS += -lusb-1.0 -ludev
}

macx {
    include(osx/osx.pri)
}
