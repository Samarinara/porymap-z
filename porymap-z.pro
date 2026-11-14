# Keep the upstream build definition intact; fork additions live here.
include(porymap.pro)
TARGET = porymap-z
QMAKE_TARGET_BUNDLE_PREFIX = com.samarinara
DEFINES += PORYMAP_Z
SOURCES += src/porymapz/dialoguedocument.cpp \
           src/porymapz/dialogueeditor.cpp
HEADERS += include/porymapz/dialoguedocument.h \
           include/porymapz/dialogueeditor.h
