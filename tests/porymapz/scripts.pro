QT = core widgets testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TARGET = tst_scripts
INCLUDEPATH += ../../include
SOURCES += tst_scripts.cpp ../../src/porymapz/scriptdocument.cpp ../../src/porymapz/scripteditor.cpp
HEADERS += ../../include/porymapz/scriptdocument.h ../../include/porymapz/scripteditor.h
