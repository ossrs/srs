TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

HEADERS += \
    ../../core/*.hpp \
    ../../kernel/*.hpp \
    ../../app/*.hpp \
    ../../rtmp/*.hpp

SOURCES += \
    ../../core/*.cpp \
    ../../kernel/*.cpp \
    ../../app/*.cpp \
    ../../rtmp/*.cpp \
    ../../main/*.cpp

INCLUDEPATH += \
    ../../core \
    ../../kernel \
    ../../app \
    ../../rtmp \
    ../../../objs \
    ../../../objs/st \
    ../../../objs/hp \
    ../../../objs/openssl/include

LIBS += \
    ../../../objs/st/libst.a \
    ../../../objs/hp/libhttp_parser.a \
    ../../../objs/openssl/lib/libssl.a \
    ../../../objs/openssl/lib/libcrypto.a \
    -ldl
