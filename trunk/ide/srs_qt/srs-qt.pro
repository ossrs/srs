TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

HEADERS += \
    ../../src/core/*.hpp \
    ../../src/kernel/*.hpp \
    ../../src/app/*.hpp \
    ../../src/protocol/*.hpp

SOURCES += \
    ../../src/core/*.cpp \
    ../../src/kernel/*.cpp \
    ../../src/app/*.cpp \
    ../../src/protocol/*.cpp \
    ../../src/main/*.cpp

INCLUDEPATH += \
    ../../src/core \
    ../../src/kernel \
    ../../src/app \
    ../../src/protocol \
    ../../objs \
    ../../objs/st \
    ../../objs/hp \
    ../../objs/openssl/include

LIBS += \
    ../../objs/st/libst.a \
    ../../objs/hp/libhttp_parser.a \
    ../../objs/openssl/lib/libssl.a \
    ../../objs/openssl/lib/libcrypto.a \
    -ldl

