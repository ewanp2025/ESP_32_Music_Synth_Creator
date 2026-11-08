QT += core gui widgets multimedia
CONFIG += c++17 release

TARGET = Esp32SynthStudio
TEMPLATE = app

msvc {
    QMAKE_CXXFLAGS += /utf-8
}

unix:!macx {
    QMAKE_CXXFLAGS_RELEASE += -O3 -ffast-math
    LIBS += -lasound -lpthread
}

win32 {
    LIBS += -lwinmm
}

SOURCES += \
    main_studio.cpp \
    midireader.cpp

HEADERS += \
    midireader.h \
    synth_sdk_modules.h \
    synth_studio_gui.h \
    xpressive_parser.h \
    effect.h