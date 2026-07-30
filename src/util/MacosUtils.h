#pragma once

#ifdef Q_OS_MAC
void hideMacOSMenuBar();
int  macMainScreenWidth();
int  macMainScreenHeight();
void forceWindowFullScreen(void *nsViewHandle);
#endif
