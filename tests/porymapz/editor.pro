QT = core gui widgets testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TARGET = tst_dialogueeditor
INCLUDEPATH += ../../include
SOURCES += tst_dialogueeditor.cpp \
           ../../src/porymapz/dialoguedocument.cpp \
           ../../src/porymapz/dialogueeditor.cpp
HEADERS += ../../include/porymapz/dialoguedocument.h \
           ../../include/porymapz/dialogueeditor.h
