######################################################################
# Communi
######################################################################

TEMPLATE = app
win32|mac:TARGET = Communi
else:TARGET = communi
!mac:DESTDIR = bin
QT += network

if(!lessThan(QT_MAJOR_VERSION, 5)) {
   QT += widgets
}


RESOURCES += communi.qrc
win32:RC_FILE = communi.rc
mac:ICON = resources/icons/communi.icns

include(src/src.pri)
include(../examples.pri)
include(../shared/shared.pri)
