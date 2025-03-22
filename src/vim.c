#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "../include/defs.h"
#include "../include/vim.h"
#include "../include/editor.h"
#include "../include/terminal.h"
#include "../include/row.h"
#include "../include/fileio.h"

/* 当前vim模式 */
static enum VimMode current_mode = MODE_NORMAL;
static enum VimMode last_mode = MODE_NORMAL;

/* 初始化vim模块 */
void vimInit() {
    // 初始设置为NORMAL模式
    current_mode = MODE_NORMAL;
    last_mode = MODE_NORMAL;
    editorSetStatusMessage("-- NORMAL模式 --");
}

/* 获取当前Vim模式 */
enum VimMode vimGetMode() {
    return current_mode;
}

/* 设置Vim模式 */
void vimSetMode(enum VimMode mode) {
    current_mode = mode;
    
    // 更新状态栏显示当前模式
    switch (current_mode) {
        case MODE_NORMAL:
            editorSetStatusMessage("-- NORMAL模式 --");
            break;
        case MODE_INSERT:
            editorSetStatusMessage("-- INSERT模式 --");
            break;
        case MODE_COMMAND:
            editorSetStatusMessage(":");
            break;
    }
    
    // 切换到插入模式时重置计数缓冲区
    if (mode == MODE_INSERT) {
        E.vim_count_buffer = 0;
        
        // 确保文件至少有一行可编辑
        if (E.numrows == 0) {
            editorInsertRow(0, "", 0);
            E.cy = 0;
            E.cx = 0;
        }
    }
    
    // 切换到命令模式时初始化命令缓冲区
    if (mode == MODE_COMMAND) {
        E.vim_cmd_buf[0] = ':';
        E.vim_cmd_buf[1] = '\0';
        E.vim_cmd_pos = 1;
    }
}

/* 绘制Vim模式状态 */
void vimDrawModeStatus(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);
    
    char mode_info[16];
    switch (current_mode) {
        case MODE_NORMAL:
            snprintf(mode_info, sizeof(mode_info), " NORMAL ");
            break;
        case MODE_INSERT:
            snprintf(mode_info, sizeof(mode_info), " INSERT ");
            break;
        case MODE_VISUAL:
            snprintf(mode_info, sizeof(mode_info), " VISUAL ");
            break;
        case MODE_COMMAND:
            snprintf(mode_info, sizeof(mode_info), " COMMAND ");
            break;
    }
    
    abAppend(ab, mode_info, strlen(mode_info));
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, " ", 1);
}

/* 处理数字前缀 */
static void vimHandleNumber(int c) {
    if (isdigit(c) && (c != '0' || E.vim_count_buffer > 0)) {
        E.vim_count_buffer = E.vim_count_buffer * 10 + (c - '0');
        return;
    }
    
    // 如果数字缓冲区为0，默认设为1
    int count = E.vim_count_buffer > 0 ? E.vim_count_buffer : 1;
    
    // 根据命令执行count次操作
    for (int i = 0; i < count; i++) {
        vimHandleNormalMode(c);
    }
    
    // 执行完后重置数字缓冲区
    E.vim_count_buffer = 0;
}

/* 处理正常模式下的按键 */
void vimHandleNormalMode(int c) {
    // 光标移动
    switch (c) {
        case 'h':
        case ARROW_LEFT:
            if (E.cx > 0) {
                E.cx--;
            }
            break;
            
        case 'j':
        case ARROW_DOWN:
            if (E.cy < E.numrows - 1) {
                E.cy++;
            }
            break;
            
        case 'k':
        case ARROW_UP:
            if (E.cy > 0) {
                E.cy--;
            }
            break;
            
        case 'l':
        case ARROW_RIGHT:
            if (E.cy < E.numrows && E.cx < E.row[E.cy].size) {
                E.cx++;
            }
            break;
            
        case '0':
            E.cx = 0;
            break;
            
        case '$':
            if (E.cy < E.numrows) {
                E.cx = E.row[E.cy].size;
            }
            break;
    }
    
    // 如果移动到了新行，确保不超出行的尾部
    if (E.cy < E.numrows) {
        if (E.cx > E.row[E.cy].size) {
            E.cx = E.row[E.cy].size;
        }
    }
    
    // 更新rx值
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }
}

/* 处理插入模式下的按键 */
void vimHandleInsertMode(int c) {
    // 确保有可编辑的行
    if (E.numrows == 0) {
        editorInsertRow(0, "", 0);
        E.cx = 0;
        E.cy = 0;
    }
    
    // 处理普通可打印字符
    if (!iscntrl(c) && c < 128) {
        // 直接插入字符
        editorRowInsertChar(&E.row[E.cy], E.cx, c);
        E.cx++; // 移动光标
        
        // 立即更新rx值确保光标位置正确
        if (E.cy < E.numrows) {
            E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
        }
        return;
    }
    
    // 处理特殊按键
    switch (c) {
        case '\r':
            editorInsertNewline();
            // 更新rx值
            E.rx = 0;
            break;
            
        case BACKSPACE:
        case CTRL_KEY('h'):
            if (E.cx > 0 || E.cy > 0) {
                editorDelChar();
                // 更新rx值
                if (E.cy < E.numrows) {
                    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
                }
            }
            break;
            
        case DEL_KEY:
            if (E.cy < E.numrows && (E.cx < E.row[E.cy].size || E.cy < E.numrows - 1)) {
                editorMoveCursor(ARROW_RIGHT);
                editorDelChar();
                // 更新rx值
                if (E.cy < E.numrows) {
                    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
                }
            }
            break;
            
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while (times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                // 更新rx值
                if (E.cy < E.numrows) {
                    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
                }
            }
            break;
            
        case HOME_KEY:
            E.cx = 0;
            E.rx = 0;
            break;
            
        case END_KEY:
            if (E.cy < E.numrows) {
                E.cx = E.row[E.cy].size;
                E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
            }
            break;
            
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            // 更新rx值
            if (E.cy < E.numrows) {
                E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
            }
            break;
    }
}

/* 处理命令模式下的按键 */
void vimHandleCommandMode(int c) {
    if (c == '\r') {
        // 执行命令
        E.vim_cmd_buf[E.vim_cmd_pos] = '\0';
        vimExecuteCommand(E.vim_cmd_buf + 1); // 跳过':'字符
        vimSetMode(MODE_NORMAL);
    } else if (c == 27) { // ESC键
        vimSetMode(MODE_NORMAL);
    } else if (c == BACKSPACE || c == CTRL_KEY('h')) {
        if (E.vim_cmd_pos > 1) {
            E.vim_cmd_pos--;
            E.vim_cmd_buf[E.vim_cmd_pos] = '\0';
        } else {
            vimSetMode(MODE_NORMAL);
        }
    } else if (!iscntrl(c) && c < 128) {
        if (E.vim_cmd_pos < VIM_CMD_BUF_SIZE - 1) {
            E.vim_cmd_buf[E.vim_cmd_pos++] = c;
            E.vim_cmd_buf[E.vim_cmd_pos] = '\0';
        }
    }
}

/* 执行命令模式下的命令 */
void vimExecuteCommand(const char *cmd) {
    // 去掉前导空格
    while (*cmd && isspace(*cmd)) cmd++;
    
    // 跳过空命令
    if (*cmd == '\0') {
        return;
    }
    
    // 分析命令
    if (strcmp(cmd, "w") == 0) {
        // 保存文件
        editorSave();
    } else if (strcmp(cmd, "q") == 0) {
        // 退出如果没有未保存的修改
        if (!E.dirty) {
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
        } else {
            editorSetStatusMessage("错误: 有未保存的修改，使用 :q! 强制退出");
        }
    } else if (strcmp(cmd, "q!") == 0) {
        // 强制退出
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
    } else if (strcmp(cmd, "wq") == 0) {
        // 保存并退出
        editorSave();
        if (!E.dirty) {
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
        }
    } else {
        editorSetStatusMessage("未知命令: %s", cmd);
    }
}

/* 处理vim模式下的按键 */
void vimProcessKey(int c) {
    static int command_len = 0;
    static char command_buf[128] = {0};

    // 第一次调用时初始化
    static int initialized = 0;
    if (!initialized) {
        vimInit();
        initialized = 1;
    }

    // 记录上一个模式
    last_mode = current_mode;
    
    // 优先处理ESC键
    if (c == '\x1b' || c == 27 || c == CTRL_KEY('[')) {
        // ESC键在不同模式下的行为
        switch (current_mode) {
            case MODE_INSERT:
                // 从INSERT模式切换到NORMAL模式
                current_mode = MODE_NORMAL;
                editorSetStatusMessage("-- NORMAL模式 --");
                return;
                
            case MODE_COMMAND:
                // 从COMMAND模式取消并返回NORMAL模式
                current_mode = MODE_NORMAL;
                editorSetStatusMessage("-- NORMAL模式 --");
                return;
                
            case MODE_NORMAL:
                // 在NORMAL模式下ESC只是取消当前操作
                return;
                
            default:
                // 其他情况都回到NORMAL模式
                current_mode = MODE_NORMAL;
                editorSetStatusMessage("-- NORMAL模式 --");
                return;
        }
    }

    // 根据当前模式处理按键
    switch (current_mode) {
        case MODE_NORMAL:
            // 处理模式切换
            if (c == 'i') {
                current_mode = MODE_INSERT;
                editorSetStatusMessage("-- INSERT模式 --");
                return;
            } else if (c == ':') {
                current_mode = MODE_COMMAND;
                command_len = 0;
                memset(command_buf, 0, sizeof(command_buf));
                editorSetStatusMessage(":");
                return;
            }
            
            // 其他NORMAL模式按键处理
            vimHandleNormalMode(c);
            break;
            
        case MODE_INSERT:
            // 处理INSERT模式按键
            vimHandleInsertMode(c);
            break;
            
        case MODE_COMMAND:
            // 处理命令模式下的按键
            if (c == '\r') {
                // 回车键执行命令
                command_buf[command_len] = '\0';
                vimExecuteCommand(command_buf);
                current_mode = MODE_NORMAL;
                editorSetStatusMessage("-- NORMAL模式 --");
                return;
            } else if (c == BACKSPACE && command_len > 0) {
                // 退格键删除命令中的一个字符
                command_len--;
                command_buf[command_len] = '\0';
                editorSetStatusMessage(":%s", command_buf);
            } else if (!iscntrl(c) && c < 128 && command_len < sizeof(command_buf) - 1) {
                // 普通字符添加到命令中
                command_buf[command_len++] = c;
                command_buf[command_len] = '\0';
                editorSetStatusMessage(":%s", command_buf);
            }
            break;
    }

    // 如果模式发生变化，更新状态消息
    if (last_mode != current_mode) {
        switch (current_mode) {
            case MODE_NORMAL:
                editorSetStatusMessage("-- NORMAL模式 --");
                break;
            case MODE_INSERT:
                editorSetStatusMessage("-- INSERT模式 --");
                break;
            case MODE_COMMAND:
                editorSetStatusMessage(":");
                break;
        }
    }
} 