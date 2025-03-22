#ifndef KILO_FILEIO_H
#define KILO_FILEIO_H

#include "defs.h"

/* 文件操作函数 */
char *editorRowsToString(int *buflen);
void editorOpen(char *filename);
void editorSave();

#endif 