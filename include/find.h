#ifndef KILO_FIND_H
#define KILO_FIND_H

#include "defs.h"

/* 查找操作函数 */
void editorFindCallback(char *query, int key);
void editorFind();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

#endif 