QT = core gui widgets testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TARGET = tst_battle
INCLUDEPATH += ../../include
SOURCES += tst_battle.cpp ../../src/porymapz/battledocument.cpp \
    ../../src/porymapz/battleeditor.cpp ../../src/porymapz/dialoguedocument.cpp \
    ../../src/porymapz/dialogueeditor.cpp
HEADERS += ../../include/porymapz/battleeditor.h ../../include/porymapz/dialogueeditor.h
