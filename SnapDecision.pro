QT += core gui widgets sql xml

CONFIG += c++20

INCLUDEPATH += include

# INCLUDEPATH += D:/programming/opencv/build/include


VERSION = 0.1.2.0


# INCLUDEPATH += D:/programming/onnxruntime-win-x64-1.16.3/include
# LIBS += -LD:/programming/onnxruntime-win-x64-1.16.3/lib \
#         -lonnxruntime -lonnxruntime_providers_shared
# QMAKE_POST_LINK += $$quote(PATH=$$quote(D:/programming/onnxruntime-win-x64-1.16.3/lib) $$escape_expand(\n\t) $$QMAKE_POST_LINK)


INCLUDEPATH += D:/programming/libjpeg-turbo64/include
LIBS += -LD:/programming/libjpeg-turbo64/lib \
        -lturbojpeg
QMAKE_POST_LINK += $$quote(PATH=$$quote(D:/programming/libjpeg-turbo64/bin) $$escape_expand(\n\t) $$QMAKE_POST_LINK)


SOURCES += \
        src/borderwidget.cpp \
        src/categorydisplaywidget.cpp \
        src/decision.cpp \
        src/dnn.cpp \
        src/enums.cpp \
        src/imagedescriptionnode.cpp \
        src/imagegroup.cpp \
        src/imagetreemodel.cpp \
        src/imagetreeview.cpp \
        src/libjpegturbo_loader.cpp \
        src/main.cpp \
        src/mainwindow.cpp \
        src/imagecache.cpp \
        src/diagnostics.cpp \
        src/mainmodel.cpp \
        src/maincontroller.cpp \
        src/databasemanager.cpp \
        src/TinyEXIF.cpp \
        src/snapdecisiongraphicsview.cpp \
        src/TinyXML2.cpp \
        src/program_settings.cpp

HEADERS += \
        include/snapdecision/borderwidget.h \
        include/snapdecision/categorydisplaywidget.h \
        include/snapdecision/decision.h \
        include/snapdecision/dnn.h \
        include/snapdecision/enums.h \
        include/snapdecision/imagedescriptionnode.h \
        include/snapdecision/imagegroup.h \
        include/snapdecision/imagetreemodel.h \
        include/snapdecision/imagetreeview.h \
        include/snapdecision/libjpegturbo_loader.h \
        include/snapdecision/mainwindow.h \
        include/snapdecision/imagecache.h \
        include/snapdecision/diagnostics.h \
        include/snapdecision/mainmodel.h \
        include/snapdecision/maincontroller.h \
        include/snapdecision/databasemanager.h \
        include/snapdecision/settings.h \
        include/snapdecision/taskqueue.h \
        include/snapdecision/types.h \
        include/snapdecision/TinyEXIF.h \
        include/snapdecision/utils.h \
        include/snapdecision/snapdecisiongraphicsview.h \
        include/snapdecision/TinyXML2.h \
        include/snapdecision/program_settings.h


FORMS += \
    ui/about.ui \
    ui/mainwindow.ui \
    ui/program_settings.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += res/res.qrc 

RC_ICONS = res/icon2.ico
