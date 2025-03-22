#ifndef KILO_HIGHLIGHT_H
#define KILO_HIGHLIGHT_H

#include "defs.h"

/* 语法高亮函数 */
int is_separator(int c);
void editorUpdateSyntax(erow *row);
int editorSyntaxToColor(int hl);
void editorSelectSyntaxHighlight();

/* 文件类型数据 */
extern char *C_HL_extensions[];
extern char *C_HL_keywords[];
extern struct editorSyntax HLDB[];
extern int HLDB_ENTRIES;

#endif 