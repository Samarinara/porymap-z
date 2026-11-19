QT = core gui widgets testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TARGET = tst_trainers
INCLUDEPATH += ../../include
SOURCES += tst_trainers.cpp ../../src/porymapz/trainerproject.cpp ../../src/porymapz/trainermanager.cpp
HEADERS += ../../include/porymapz/trainerproject.h ../../include/porymapz/trainermanager.h
