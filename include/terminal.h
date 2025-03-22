#ifndef KILO_TERMINAL_H
#define KILO_TERMINAL_H

#include "defs.h"

/* 终端操作函数 */
void die(const char *s);
void disableRawMode();
void enableRawMode();
int editorReadKey();
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);

#endif 