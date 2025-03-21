#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8   // 制表符大小
#define KILO_QUIT_TIMES 3 // 退出次数为3才能不保存退出

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey
{
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};
enum editorHighlight
{
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};
#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)
/*** data ***/
struct editorSyntax
{
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};
typedef struct erow
{
  int idx;
  int size;
  int rsize;
  char *chars;
  char *render;
  unsigned char *hl; // 0-255之间的整数，数组的每个值对应render中的一个字符，告诉用户该字符是否是字符串的一部分，或注释，或数字
  int hl_open_comment;
} erow; // 编辑行
struct editorConfig
{
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];    // 显示消息
  time_t statusmsg_time; // 存储消息的时间戳，以便在显示后几秒钟内删除消息
  struct editorSyntax *syntax;
  struct termios orig_termios;
};
struct editorConfig E;
/*** filetypes ***/
char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL}; // 用于高亮显示C语言文件的文件扩展名
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", NULL}; // 用于高亮显示C语言文件的关键字
struct editorSyntax HLDB[] = {
    // 高亮C文件时打开该标志
    {"c",
     C_HL_extensions,
     C_HL_keywords,
     "//", "/*", "*/",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0])) // 高亮数据库
/*** prototypes ***/
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));
/*** terminal ***/
void die(const char *s)
{
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3); // 清除屏幕并退出
  perror(s);                         // 检查全局errno变量，打印一个错误信息
  exit(1);                           // 以退出状态1退出程序
}
void disableRawMode()
{
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}
void enableRawMode()
{
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode); // 注册disableRawMode()函数，通过调用exit()函数，确保程序退出时，中断属性保持我们找到它们
  struct termios raw = E.orig_termios;
  // 使用tcgetattr()将当前属性读入一个结构体中

  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // IXON关闭ctrl s/q，前者用于软件流控制，后者组织数据传输到中断,ICRNL关闭终端将用户输入的任何回车符转换为换行符，BRKINT开启时，一个终端条件将导致向程序发送一个SIGINT信号，就像按下ctrl-c键，INPCK启用奇偶检验，似乎不适用于现代终端仿真器，ISTRIP会导致每个输入字节的第8位被移除
  raw.c_oflag &= ~(OPOST);                                  // 关闭OPOST标志来关闭所有输出处理功能
  raw.c_cflag |= (CS8);                                     // 掩码，将字符大小（CS）设置为梅子姐8位
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);          // 将修改后的结构体传递给tcsetarr()以新的中断属性协会,关闭规范模式，诸子皆读取输入,ISIG关闭两个信号，SIGINT和SIGTSTP导致终止和挂起，ctrl c/z
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr"); // 将程序的标准输入设置为文本文件或管道而不是终端，尝试echo test | ./kilo
}
int editorReadKey()
{ // 等待一个按键操作，并返回它，涉及读取表示单个按键操作的多字节,有许多没有处理的键转义序列，如F1-F12，忽略这些键
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
  {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  if (c == '\x1b')
  { // 如果我们读取到一个转义字符，我们立即将两个额外的字节读入 seq 缓冲区。如果其中任何一个读取超时（0.1 秒后），那么我们假设用户只是按下了 Escape 键，并返回该键。否则，我们查看该转义序列是否是一个箭头键转义序列。如果是，我们只需返回相应的 w a s d 字符即可。如果不是我们认识的转义序列，我们只需返回转义字符。
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';
    if (seq[0] == '[')
    {
      if (seq[1] >= '0' && seq[1] <= '9')
      {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~')
        {
          switch (seq[1])
          {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      }
      else
      {
        switch (seq[1])
        {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    }
    else if (seq[0] == '0')
    {
      switch (seq[1])
      {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }
    return '\x1b';
  }
  else
  {
    return c;
  }
}
int getCursorPosition(int *rows, int *cols)
{ // 获取光标位置，n查询终端的状态信息
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;
  while (i < sizeof(buf) - 1)
  {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;
  return 0;
}
int getWindowSize(int *rows, int *cols)
{
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
  {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols); // ioctl()将终端的列数和行数放入指定的winsize结构体中，没有简单的将光标移动到右下角的命令
  }
  else
  {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}
/*** syntax highlighting***/
int is_separator(int c)
{
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL; // 接受一个字符，如果被认为是分隔符，则返回true
}
void editorUpdateSyntax(erow *row)
{
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);
  if (E.syntax == NULL)
    return;
  char **keywords = E.syntax->keywords;
  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;
  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;
  int prev_sep = 1;  // 用于跟踪前一个字符是否是分隔符,假定行首是一个分隔符
  int in_string = 0; // 用于跟踪是否在字符串中
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

  int i = 0;
  while (i < row->rsize) // 每次迭代消费多个字符
  {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL; // 保存前一个字符的高亮
    if (scs_len && !in_string && !in_comment)                     // 如果不在字符串中，
    {
      if (!strncmp(&row->render[i], scs, scs_len)) // 那么检查是否是注释的开始
      {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i); // 如果是注释的开始，那么将剩余的行设置为注释高亮
        break;
      }
    }

    if (mcs_len && mce_len && !in_string) // 确保不在字符串中
    {
      if (in_comment)
      {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len))
        {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        }
        else
        {
          i++;
          continue;
        }
      }
      else if (!strncmp(&row->render[i], mcs, mcs_len))
      {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }
    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS)
    {
      if (in_string)
      {
        row->hl[i] = HL_STRING;              // 如果在字符串中，那么将当前字符设置为字符串高亮
        if (c == '\\' && i + 1 < row->rsize) // 考虑转义字符，如果出现\'或\"，那么将下一个字符设置为字符串高亮

        {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string)
          in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      }
      else // 如果不在字符串中，那么检查是否是字符串的开始，通过检查双引号或单引号
      {
        if (c == '"' || c == '\'')
        {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }
    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) // 用if语句包裹了数字高亮代码，以检查当前文件类型是否应该高亮显示数字
    {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)) // 如果前一个字符是分隔符或者前一个字符是数字，那么这个字符是数字,增加支持高亮显示包含小数点的数字
      {
        row->hl[i] = HL_NUMBER; // 设置高亮
        i++;
        prev_sep = 0; // 如果是数字，那么检查下一个字符
        continue;
      }
    }
    if (prev_sep) // 如果前一个字符是分隔符，那么检查是否是关键字,关键词前后都需要右分隔符，否则，avoid,voided,voided,avoided中的void将被突出显示为关键词
    {
      int j;
      for (j = 0; keywords[j]; j++)
      {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2)
          klen--;
        if (!strncmp(&row->render[i], keywords[j], klen) &&
            is_separator(row->render[i + klen]))
        {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL)
      {
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = is_separator(c); // 如果不是数字，那么检查是否是分隔符
    i++;
  }
  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows)
    editorUpdateSyntax(&E.row[row->idx + 1]);
}
int editorSyntaxToColor(int hl)
{
  switch (hl)
  {
  case HL_COMMENT:
  case HL_MLCOMMENT:
    return 36; // 评论着色为青色
  case HL_KEYWORD1:
    return 33; // 关键字着色为黄色
  case HL_KEYWORD2:
    return 32; // 类型着色为绿色
  case HL_STRING:
    return 35; // 字符串着色为紫色
  case HL_NUMBER:
    return 31; // 数字着色为红色
  case HL_MATCH:
    return 34; // 搜索结果，蓝色
  default:
    return 37; // 默认颜色foreground
  }
}
void editorSelectSyntaxHighlight() // 将当前文件名与HLDB中的filematch字段之一进行匹配，如果匹配成功，将E.syntax设置为该文件类型
{
  E.syntax = NULL;
  if (E.filename == NULL)
    return;
  // char *ext = strrchr(E.filename, '.'); // 查找.字符的最后出现设置，从而获得文件扩展部分的指针，没有扩展名，则ext将是NULL
  for (unsigned int j = 0; j < HLDB_ENTRIES; j++)
  {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i])
    {
      // int is_ext = (s->filematch[i][0] == '.');
      char *p = strstr(E.filename, s->filematch[i]);
      if (p != NULL)
      {
        int patlen = strlen(s->filematch[i]); // 文件名长度
        // if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||(!is_ext && strstr(E.filename, s->filematch[i]))) // 使用strcmp()查看文件名是否以该扩展名结尾{
        if (s->filematch[i][0] != '.' || p[patlen] == '\0')
        {
          E.syntax = s;
          int filerow;
          for (filerow = 0; filerow < E.numrows; filerow++)
          {
            editorUpdateSyntax(&E.row[filerow]);
          }
          return;
        }
      }
      i++;
    }
  }
}
/*** row operations ***/
int editorRowCxToRx(erow *row, int cx)
{
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++)
  {
    if (row->chars[j] == '\t')
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    rx++;
  }
  return rx;
}
int editorRowRxToCx(erow *row, int rx) // 将rx转换为cx
{
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++)
  {
    if (row->chars[cx] == '\t')
      cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
    cur_rx++;
    if (cur_rx > rx)
      return cx;
  }
  return cx;
}
void editorUpdateRow(erow *row) // 从chars复制每个字符到render
{
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t')
      tabs++;
  free(row->render);
  row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1); // 为每个制表符分配7个空间
  int idx = 0;
  for (j = 0; j < row->size; j++)
  {
    if (row->chars[j] == '\t')
    {
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0)
        row->render[idx++] = ' ';
    }
    else
    {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len)
{
  if (at < 0 || at > E.numrows)
    return;
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));               // 重新分配内存以容纳新行
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at)); // 将新行插入到行数组的末尾
  // int at = E.numrows;
  for (int j = at + 1; j <= E.numrows; j++)
    E.row[j].idx++;
  E.row[at].idx = at;
  E.row[at].size = len;              // 将新行插入到行数组的末尾
  E.row[at].chars = malloc(len + 1); // 为新行分配内存
  memcpy(E.row[at].chars, s, len);   // 将新行复制到新分配的内存中
  E.row[at].chars[len] = '\0';
  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  E.row[at].hl_open_comment = 0;
  editorUpdateRow(&E.row[at]);
  E.numrows++;
  E.dirty++;
}
void editorFreeRow(erow *row) // 释放删除的erow所拥有的内存
{
  free(row->render);
  free(row->chars);
  free(row->hl);
}
void editorDelRow(int at) // 行首退格，将当前行的内容追加到上一行，然后删除
{
  if (at < 0 || at >= E.numrows)
    return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  for (int j = at; j < E.numrows - 1; j++)
    E.row[j].idx--;
  E.numrows--;
  E.dirty++;
}
void editorRowInsertChar(erow *row, int at, int c) // 在指定位置将单个字符插入到erow
{
  if (at < 0 || at > row->size)
    at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}
void editorRowAppendString(erow *row, char *s, size_t len)
{
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}
void editorRowDelChar(erow *row, int at) // 删除左侧的字符
{
  if (at < 0 || at >= row->size)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}
void editorInsertChar(int c)
{
  if (E.cy == E.numrows)
  { // 在文件末尾插入新行
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}
void editorInsertNewline()
{ // 处理enter键
  if (E.cx == 0)
  {
    editorInsertRow(E.cy, "", 0);
  }
  else
  {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}
void editorDelChar()
{
  if (E.cy == E.numrows)
    return; // 如果光标已经超过文件末尾，那么就没有东西可以删除了，就立即return
  if (E.cx == 0 && E.cy == 0)
    return; // 如果光标在文件的开头，那么就没有东西可以删除了，就立即return
  erow *row = &E.row[E.cy];
  if (E.cx > 0)
  {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  }
  else
  {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/*** file i/o ***/
char *editorRowsToString(int *buflen)
{ // 将E.row中的所有行连接成一个单独的字符串，以便将其写入磁盘
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen; // 将每行文本的长度相加，为每行添加一个换行符1，将总长度保存到buflen中
  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++)
  {
    memcpy(p, E.row[j].chars, E.row[j].size); // 在分配所需的内存之后，遍历行，并将每行的内容memcpy()到缓冲区的末尾
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

void editorOpen(char *filename)
{
  free(E.filename);
  E.filename = strdup(filename); // 将文件名复制到E.filename中
  editorSelectSyntaxHighlight();
  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1)
  { // 将整个文件读取到E.row中
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;
    // editorAppendRow(line, linelen);
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0; // 重置文件状态
}
void editorSave()
{
  if (E.filename == NULL)
  {
    E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL); // windows的bash需要按三次escape,将NULL传递给editorpormpt()以防不想使用回调
    if (E.filename == NULL)
    {
      editorSetStatusMessage("Save aborted");
      return;
    }
    editorSelectSyntaxHighlight();
  }
  int len;
  char *buf = editorRowsToString(&len);
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1)
  { // 添加错误处理
    if (ftruncate(fd, len) != -1)
    {
      if (write(fd, buf, len) == len)
      {

        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno)); // 通知消息，是否保存成功
}

/*** find ***/
void editorFindCallback(char *query, int key)
{
  static int last_match = -1; //-1向后搜索
  static int direction = 1;   // 1向前搜索
  static int saved_hl_line;
  static char *saved_hl = NULL;
  if (saved_hl)
  {
    memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }
  if (key == '\r' || key == '\x1b') // \r是enter键，\x1b是escape,按下这两个键意味着即将离开搜索模式
  {
    last_match = -1;
    direction = 1;
    return;
  }
  else if (key == ARROW_RIGHT || key == ARROW_DOWN)
  {
    direction = 1;
  }
  else if (key == ARROW_LEFT || key == ARROW_UP)
  {
    direction = -1;
  }
  else
  {
    last_match = -1;
    direction = 1;
  }
  if (last_match == -1)
    direction = 1;
  int current = last_match; // 当前索引为current，找到匹配项时将last_match设置为current,这样如果用户按下箭头键，我们将从该店开始下一次搜索
  int i;
  for (i = 0; i < E.numrows; i++)
  {
    current += direction;
    if (current == -1)
      current = E.numrows - 1;
    else if (current == E.numrows)
      current = 0;
    erow *row = &E.row[current];
    char *match = strstr(row->render, query);
    if (match)
    {
      last_match = current;
      E.cy = current;
      E.cx = editorRowRxToCx(row, match - row->render);
      E.rowoff = E.numrows;

      saved_hl_line = current;       // 静态变量，知道哪一行的hl需要恢复
      saved_hl = malloc(row->rsize); // 动态分配的数组，没有需要恢复的内容时，指向NULL
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}
void editorFind()
{
  int saved_cx = E.cx; // 保存为了后序搜索取消后恢复这些值
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;

  char *query = editorPrompt("Search: %s (Use ESC/aRROWS/Enter)", editorFindCallback);
  if (query)
  {
    free(query);
  }
  else
  { // 如果query等于NULL,等于他们按了Escape，恢复保存的值
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
}

/*** append buffer ***/
struct abuf
{
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
  char *new = realloc(ab->b, ab->len + len); // 请求realloc()给我们一块内存，大小位当前字符串的大小加上要追加的字符串大小

  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len); // 赋值缓冲区当前数据末尾的字符串s，并更新abuf的指针和长度到新值
  ab->b = new;
  ab->len += len;
}
void abFree(struct abuf *ab)
{ // 释放由abuf使用的动态内存
  free(ab->b);
}
/*** output ***/
void editorScroll()
{
  E.rx = 0;
  if (E.cy < E.numrows)
  {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  } // 设置E.rx为光标所在行的渲染偏移量
  if (E.cy < E.rowoff) // 水平滚动，检查光标是否在可见窗口上发过，如果是，则向上滚动到光标位置
  {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) // 检查光标是否在可见窗口底部
  {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) // 垂直滚动
  {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols)
  {
    E.coloff = E.rx - E.screencols + 1;
  }
}
void editorDrawRows(struct abuf *ab)
{
  int y;
  for (y = 0; y < E.screenrows; y++)
  {
    int filerow = y + E.rowoff; // 将屏幕行号转换为文本缓冲区行号
    if (filerow >= E.numrows)   // 检查是否正在绘制属于文本缓冲区的行，或者是否正在绘制文本缓冲区结束后的行
    {
      if (E.numrows == 0 && y == E.screenrows / 3) // 待定，欢迎信息仅在用户不带参数启动程序时显示，而不是在打开文件时显示，以为欢迎信息可能会妨碍文件显示
      {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "Kilo editor -- version %s", KILO_VERSION); // 欢迎信息
        if (welcomelen > E.screencols)
          welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2; // 居中
        if (padding)
        {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--)
          abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      }
      else
      {
        abAppend(ab, "~", 1);
      }
    }
    else
    { // 确保不会超出屏幕的末尾
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.screencols)
        len = E.screencols;
      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];
      int current_color = -1; //-1是默认颜色
      int j;
      for (j = 0; j < len; j++)
      {
        if (iscntrl(c[j]))
        {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAppend(ab, "\x1b[7m", 4);
          abAppend(ab, &sym, 1);
          abAppend(ab, "\x1b[m", 3);
          if (current_color != -1)
          {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color); // 打印当前颜色的转移序列
            abAppend(ab, buf, clen);
          }
        }
        else if (hl[j] == HL_NORMAL)
        { // 如果当前字符是控制字符，那么我们将其显示为问号，否则我们将其显示为@加上字符的ASCII值
          if (current_color != -1)
          {
            abAppend(ab, "\x1b[39m", 5);
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);
        }
        else
        {
          int color = editorSyntaxToColor(hl[j]);
          if (color != current_color)
          {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5);
    }

    abAppend(ab, "\x1b[K", 3); // K逐行删除
                               // if (y < E.screenrows - 1)
    //{
    abAppend(ab, "\r\n", 2); // 最后一行不打印""\r\n
  }
  // }
}

void editorDrawStatusBar(struct abuf *ab) // 状态栏反转颜色
{
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[No Name]", E.numrows,
                     E.dirty ? "(modified)" : ""); // 状态栏显示文件修改状态，通过在文件名后显示 (modified) 来展示 E.dirty 的状态。
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
                      E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);
  if (len > E.screencols)
    len = E.screencols;
  abAppend(ab, status, len); // 显示文件名前20个字符和行数，如果没有文件名则显示[No Name]
  while (len < E.screencols)
  {
    if (E.screencols - len == rlen)
    {
      abAppend(ab, rstatus, rlen);
      break;
    }
    else
    {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2); // 状态栏下一行留出空间显示消息
}
void editorDrawMessageBar(struct abuf *ab)
{
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols)
    msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}
void editorRefreshScreen()
{
  editorScroll();
  struct abuf ab = ABUF_INIT;    // 初始化一个新的abuf，称为ab，替换所有WRITE为abAppend
  abAppend(&ab, "\x1b[?25l", 6); // 重置模式
  // \x1b是住哪一字符,J命令清楚屏幕，参数是2，表示清楚整个屏幕，<esc>[1J 将清除屏幕到光标处，而 <esc>[0J 将清除从光标到屏幕末尾的屏幕。此外， 0 是 J 的默认参数，因此仅使用 <esc>[J 本身也会清除从光标到屏幕末尾的屏幕。
  abAppend(&ab, "\x1b[H", 3); // 重新定位光标到屏幕左上角
  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1); // E.cy不再指向屏幕上光标位置，而是光标在文本文件中的位置
  abAppend(&ab, buf, strlen(buf));
  // abAppend(&ab, "\x1b[H", 3);    // 绘制完成后，重新定位光标到屏幕左上角
  abAppend(&ab, "\x1b[?25h", 6); // 设置模式
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}
void editorSetStatusMessage(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** input ***/
char *editorPrompt(char *prompt, void (*callback)(char *, int)) // 提示用户在保存新文件是输入文件名，在状态栏中显示提示，允许用户在提示后输入一行文本
{
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  while (1)
  {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();
    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) // 允许在输入提示中按下backspace
    {
      if (buflen != 0)
        buf[--buflen] = '\0';
    }
    else if (c == '\x1b')
    {

      editorSetStatusMessage("");
      if (callback)
        callback(buf, c);
      free(buf);
      return NULL;
    }
    else if (c == '\r')
    {
      if (buflen != 0)
      {
        editorSetStatusMessage("");
        if (callback)
          callback(buf, c);
        return buf;
      }
    }
    else if (!iscntrl(c) && c < 128)
    {
      if (buflen == bufsize - 1)
      {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
    if (callback)
      callback(buf, c);
  } //??
}
void editorMoveCursor(int key)
{
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy]; // 由于 E.cy 允许位于文件最后一行之后，我们使用三元运算符来检查光标是否位于实际行上。如果是，则 row 变量将指向光标所在的 erow ，在我们允许光标向右移动之前，我们会检查 E.cx 是否位于该行末尾的左侧。
  switch (key)
  {
  case ARROW_LEFT:
    if (E.cx != 0)
    {
      E.cx--;
    }
    else if (E.cy > 0)
    {
      E.cy--;
      E.cx = E.row[E.cy].size; // 允许用户在行首按下左箭头移动到上一行的末尾
    }
    break;
  case ARROW_RIGHT:
    // if (E.cx != E.screencols - 1)//允许滚动到右侧
    //{
    // E.cx++;
    //}
    if (row && E.cx < row->size)
    {
      E.cx++;
    }
    else if (row && E.cx == row->size)
    {
      E.cy++;
      E.cx = 0;
    }
    break;
  case ARROW_UP:
    if (E.cy != 0)
    {
      E.cy--;
    }
    break;
  case ARROW_DOWN:
    if (E.cy < E.numrows)
    {
      E.cy++;
    }
    break;
  }
  // 纠正如果最终超出了所在行的末尾情况
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen)
  {
    E.cx = rowlen;
  }
}
void editorProcessKeypress()
{ // 等待按键，将把各种ctrl键组合和其他特殊键映射到不同的编辑器功能，并将任何字母数字和其他可打印键的字符插入到正在编辑的文本中
  static int quit_times = KILO_QUIT_TIMES;
  int c = editorReadKey();
  switch (c)
  {
  case '\r':
    editorInsertNewline();
    break;
  case CTRL_KEY('q'):
    if (E.dirty && quit_times > 0)
    {
      editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                             "Press Ctrl-Q %d more times to quit.",
                             quit_times);
      quit_times--;
      return;
    }
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  case CTRL_KEY('S'):
    editorSave();
    break;
  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    break;
  case CTRL_KEY('f'): // 搜索
    editorFind();
    break;
  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY:
    if (c == DEL_KEY)
      editorMoveCursor(ARROW_RIGHT);
    editorDelChar();
    break;
  case PAGE_UP:
  case PAGE_DOWN:
  {
    if (c == PAGE_UP)
    {
      E.cy = E.rowoff;
    }
    else if (c == PAGE_DOWN)
    {
      E.cy = E.rowoff + E.screenrows - 1;
      if (E.cy > E.numrows)
        E.cy = E.numrows;
    }
    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  }
  break;

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;

  case CTRL_KEY('l'):
  case '\x1b':
    break;

  default:
    editorInsertChar(c);
    break;
  }
  quit_times = KILO_QUIT_TIMES; // 当按下除ctrl_q之外的任何键时，重置退出次数
}

void initEditor()
{
  E.cx = 0; // 光标水平，列
  E.cy = 0; // 光标垂直，行
  E.rx = 0;
  E.rowoff = 0; // 默认情况下将滚动到文件顶部
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.syntax = NULL;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  E.screenrows -= 2; // 空出两行显示状态栏和消息
}
int main(int argc, char *argv[])
{
  enableRawMode();
  initEditor(); // 初始化E结构体中的所有字段
  if (argc >= 2)
  {
    editorOpen(argv[1]);
  }
  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
  while (1)
  {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
