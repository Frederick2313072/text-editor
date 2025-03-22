#include <unistd.h>
#include <stdlib.h>
#include "../include/editor.h"
#include "../include/terminal.h"
#include "../include/fileio.h"
#include "../include/vim.h"

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    vimInit();
    
    if (argc >= 2) {
        editorOpen(argv[1]);
    }
    
    // 设置初始状态提示
    editorSetStatusMessage("帮助：Ctrl-Q = 退出 | Ctrl-S = 保存 | ESC = Normal模式");
    
    while (1) {
        // 刷新屏幕显示
        editorRefreshScreen();
        
        // 获取用户输入
        int c = editorReadKey();
        
        // 处理全局按键
        if (c == CTRL_KEY('q')) {
            if (E.dirty) {
                editorSetStatusMessage("警告！文件有未保存的修改。"
                    "再按 Ctrl-Q 强制退出。");
                c = editorReadKey();
                if (c != CTRL_KEY('q')) {
                    continue;
                }
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
        } else if (c == CTRL_KEY('s')) {
            editorSave();
        } else {
            // 处理基于当前模式的按键
            vimProcessKey(c);
        }
    }
    
    return 0;
} 