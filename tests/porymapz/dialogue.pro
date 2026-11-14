QT = core testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TARGET = tst_dialogue
INCLUDEPATH += ../../include
SOURCES += tst_dialogue.cpp ../../src/porymapz/dialoguedocument.cpp
HEADERS += ../../include/porymapz/dialoguedocument.h
