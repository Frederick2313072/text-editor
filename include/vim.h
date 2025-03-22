#ifndef KILO_VIM_H
#define KILO_VIM_H

#include "defs.h"

/* Vim命令缓冲区 */
#define VIM_CMD_BUF_SIZE 128

// Vim命令类型
enum VimCmdType {
    CMD_NONE,
    CMD_MOVE,
    CMD_EDIT,
    CMD_FIND,
    CMD_YANK,
    CMD_PUT
};

// Vim状态
typedef struct {
    enum VimMode mode;          // 当前模式
    char cmd_buf[VIM_CMD_BUF_SIZE]; // 命令缓冲区
    int cmd_pos;               // 命令位置
    int count_buffer;          // 数字缓冲区
    char last_find_char;       // 上次查找字符
    int visual_start_x;        // 可视模式起始x
    int visual_start_y;        // 可视模式起始y
} VimState;

/* Vim相关函数 */
void vimInit(void);
void vimProcessKey(int c);
void vimSetMode(enum VimMode mode);
enum VimMode vimGetMode(void);
void vimDrawModeStatus(struct abuf *ab);
void vimHandleNormalMode(int c);
void vimHandleInsertMode(int c);
void vimHandleCommandMode(int c);
void vimExecuteCommand(const char *cmd);

#endif 