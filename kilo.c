#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
/*** defines ***/
#define KILO_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  ARROW_LEFT = 'a',
  ARROW_RIGHT = 'd',
  ARROW_UP = 'w',
  ARROW_DOWN = 's'
};
struct editorConfig{
  int cx,cy;
  int screenrows;
  int screencols;
  struct termios orig_termios;
};
struct editorConfig E;
void die(const char *s){
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);//清除屏幕并退出
  perror(s);//检查全局errno变量，打印一个错误信息
  exit(1);//以退出状态1退出程序
}
void disableRawMode(){
  if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&E.orig_termios)== -1)
	  die("tcsetattr");
}
void enableRawMode(){
  if(tcgetattr(STDIN_FILENO,&E.orig_termios)==-1) die("tcgetattr");
  struct termios raw=E.orig_termios;
  //使用tcgetattr()将当前属性读入一个结构体中
  atexit(disableRawMode);//注册disableRawMode()函数，通过调用exit()函数，确保程序退出时，中断属性保持我们找到它们
  raw.c_iflag &=~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);//IXON关闭ctrl s/q，前者用于软件流控制，后者组织数据传输到中断,ICRNL关闭终端将用户输入的任何回车符转换为换行符，BRKINT开启时，一个终端条件将导致向程序发送一个SIGINT信号，就像按下ctrl-c键，INPCK启用奇偶检验，似乎不适用于现代终端仿真器，ISTRIP会导致每个输入字节的第8位被移除
  raw.c_oflag &=~(OPOST);//关闭OPOST标志来关闭所有输出处理功能
  raw.c_cflag |=(CS8);//掩码，将字符大小（CS）设置为梅子姐8位
  raw.c_lflag &=~(ECHO|ICANON| IEXTEN| ISIG);//将修改后的结构体传递给tcsetarr()以新的中断属性协会,关闭规范模式，诸子皆读取输入,ISIG关闭两个信号，SIGINT和SIGTSTP导致终止和挂起，ctrl c/z
  raw.c_cc[VMIN]=0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)raw.c_cc[VTIME]=1;
  if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw)== -1) die("tcsetattr");//一个让tcgetattr()是啊比的简单方法是将程序的标准输入设置为文本文件或管道而不是终端，尝试echo test | ./kilo
}
/*** terminal ***/
char editorReadKey() {//等待一个按键操作，并返回它，涉及读取表示单个按键操作的多字节
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  if (c == '\x1b') {//如果我们读取到一个转义字符，我们立即将两个额外的字节读入 seq 缓冲区。如果其中任何一个读取超时（0.1 秒后），那么我们假设用户只是按下了 Escape 键，并返回该键。否则，我们查看该转义序列是否是一个箭头键转义序列。如果是，我们只需返回相应的 w a s d 字符即可。如果不是我们认识的转义序列，我们只需返回转义字符。
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      switch (seq[1]) {
        case 'A': return ARROW_UP;
        case 'B': return ARROW_DOWN;
        case 'C': return ARROW_RIGHT;
        case 'D': return ARROW_LEFT;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {//获取光标位置，n查询终端的状态信息
  char buf[32];
  unsigned int i=0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
}
int getWindowSize(int *rows,int *cols){
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows,cols);//ioctl()将终端的列数和行数放入指定的winsize结构体中，没有简单的将光标移动到右下角的命令
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}
/*** append buffer ***/
struct abuf{
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab,const char *s,int len){
  char *new = realloc(ab->b,ab->len+len);//请求realloc()给我们一块内存，大小位当前字符串的大小加上要追加的字符串大小

  if(new == NULL)return;
  memcpy(&new[ab->len],s,len);//赋值缓冲区当前数据末尾的字符串s，并更新abuf的指针和长度到新值
  ab->b=new;
  ab->len+=len;
}
void abFree(struct abuf *ab){//释放由abuf使用的动态内存
  free(ab->b);
}
/*** input ***/
void editorMoveCursor(char key) {
  switch (key) {
    case ARROW_LEFT:
     if (E.cx != 0) {
	    E.cx--;
     }
      break;
    case ARROW_RIGHT:
      if (E.cx != E.screencols - 1) {
      E.cx++;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
	 E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy != E.screenrows - 1) {
	 E.cy++;
      }
      break;
  }
}
void editorProcessKeypress() {//等待按键，将把各种ctrl键组合和其他特殊键映射到不同的编辑器功能，并将任何字母数字和其他可打印键的字符插入到正在编辑的文本中
  char c = editorReadKey();
  switch (c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
  }
}
void editorDrawRows(struct abuf *ab){
  int y;
  for(y=0;y<E.screenrows;y++){
    if (y == E.screenrows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome),
        "Kilo editor -- version %s", KILO_VERSION);//欢迎信息
      if (welcomelen > E.screencols) welcomelen = E.screencols;
      int padding = (E.screencols - welcomelen) / 2;//居中
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }
      while (padding--) abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomelen);
    } else {   
    abAppend(ab,"~",1);
    }
    abAppend(ab, "\x1b[K", 3);//K逐行删除
    if(y<E.screenrows-1){
      abAppend(ab,"\r\n",2);//最后一行不打印""\r\n
    }
  }
}
void editorRefreshScreen(){//清屏
  struct abuf ab = ABUF_INIT;//初始化一个新的abuf，称为ab，替换所有WRITE为abAppend 
  abAppend(&ab, "\x1b[?25l", 6);//重置模式
  // \x1b是住哪一字符,J命令清楚屏幕，参数是2，表示清楚整个屏幕，<esc>[1J 将清除屏幕到光标处，而 <esc>[0J 将清除从光标到屏幕末尾的屏幕。此外， 0 是 J 的默认参数，因此仅使用 <esc>[J 本身也会清除从光标到屏幕末尾的屏幕。
  abAppend(&ab,"\x1b[H",3);//重新定位光标到屏幕左上角
  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab,"\x1b[H",3);//绘制完成后，重新定位光标到屏幕左上角
  abAppend(&ab, "\x1b[?25h", 6);//设置模式
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}
void initEditor(){
 E.cx = 0;//光标水平，列
 E.cy = 0;//光标垂直。行
 if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}
int main(){
    enableRawMode();
    initEditor();//初始化E结构体中的所有字段
    while(1){
      editorRefreshScreen();
      editorProcessKeypress();
   }

    return 0;
}
