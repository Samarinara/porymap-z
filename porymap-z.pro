# Keep the upstream build definition intact; fork additions live here.
include(porymap.pro)
TARGET = porymap-z
QMAKE_TARGET_BUNDLE_PREFIX = com.samarinara
DEFINES += PORYMAP_Z
SOURCES += src/porymapz/dialoguedocument.cpp \
           src/porymapz/dialogueeditor.cpp
HEADERS += include/porymapz/dialoguedocument.h \
           include/porymapz/dialogueeditor.h

# Reuse the application build to verify the main tab integration.
contains(CONFIG, porymap_z_tests) {
    QT += testlib
    CONFIG += testcase
    SOURCES -= src/main.cpp
    SOURCES += tests/porymapz/tst_mainwindow.cpp
    TARGET = tst_mainwindow
}
