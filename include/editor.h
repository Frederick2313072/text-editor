#ifndef KILO_EDITOR_H
#define KILO_EDITOR_H

#include "defs.h"

/* 编辑器操作函数 */
void editorInsertChar(int c);
void editorInsertNewline();
void editorDelChar();
void editorMoveCursor(int key);
void editorProcessKeypress();
void editorScroll();
void editorDrawRows(struct abuf *ab);
void editorDrawStatusBar(struct abuf *ab);
void editorDrawMessageBar(struct abuf *ab);
void editorRefreshScreen();
void editorSetStatusMessage(const char *fmt, ...);
void initEditor();

#endif 