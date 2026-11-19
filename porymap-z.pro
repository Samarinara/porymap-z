# Keep the upstream build definition intact; fork additions live here.
include(porymap.pro)
TARGET = porymap-z
QMAKE_TARGET_BUNDLE_PREFIX = com.samarinara
DEFINES += PORYMAP_Z
SOURCES += src/porymapz/battledocument.cpp \
           src/porymapz/battleeditor.cpp \
           src/porymapz/dialoguedocument.cpp \
           src/porymapz/dialogueeditor.cpp
HEADERS += include/porymapz/battledocument.h \
           include/porymapz/battleeditor.h \
           include/porymapz/dialoguedocument.h \
           include/porymapz/dialogueeditor.h

# Reuse the application build to verify the main tab integration.
contains(CONFIG, porymap_z_tests) {
    QT += testlib
    CONFIG += testcase
    SOURCES -= src/main.cpp
    SOURCES += tests/porymapz/tst_mainwindow.cpp
    TARGET = tst_mainwindow
}

SOURCES += src/porymapz/scriptdocument.cpp src/porymapz/scripteditor.cpp
HEADERS += include/porymapz/scriptdocument.h include/porymapz/scripteditor.h
SOURCES += src/porymapz/trainerproject.cpp src/porymapz/trainermanager.cpp
HEADERS += include/porymapz/trainerproject.h include/porymapz/trainermanager.h
