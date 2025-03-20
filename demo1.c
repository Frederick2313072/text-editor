#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
struct termios orig_termios;
void die(const char *s){
  perror(s);//检查全局errno变量，打印一个错误信息
  exit(1);//以退出状态1退出程序
}
void disableRawMode(){
  if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&orig_termios)== -1)
	  die("tcsetattr");
}
void enableRawMode(){
  if(tcgetattr(STDIN_FILENO,&orig_termios)==-1)die("tcgetattr");
  struct termios raw;
  tcgetattr(STDIN_FILENO,&orig_termios);//使用tcgetattr()将当前属性读入一个结构体中
  atexit(disableRawMode);//注册disableRawMode()函数，通过调用exit()函数，确保程序退出时，中断属性保持我们找到它们
  raw.c_iflag &=~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);//IXON关闭ctrl s/q，前者用于软件流控制，后者组织数据传输到中断,ICRNL关闭终端将用户输入的任何回车符转换为换行符，BRKINT开启时，一个终端条件将导致向程序发送一个SIGINT信号，就像按下ctrl-c键，INPCK启用奇偶检验，似乎不适用于现代终端仿真器，ISTRIP会导致每个输入字节的第8位被移除
  raw.c_oflag &=~(OPOST);//关闭OPOST标志来关闭所有输出处理功能
  raw.c_cflag |=(CS8);//掩码，将字符大小（CS）设置为梅子姐8位
  raw.c_lflag &=~(ECHO|ICANON| IEXTEN| ISIG);//将修改后的结构体传递给tcsetarr()以新的中断属性协会,关闭规范模式，诸子皆读取输入,ISIG关闭两个信号，SIGINT和SIGTSTP导致终止和挂起，ctrl c/z
  raw.c_cc[VMIN]=0;
  raw.c_cc[VTIME]=1;
  if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw)== -1) die("tcsetattr");//一个让tcgetattr()是啊比的简单方法是将程序的标准输入设置为文本文件或管道而不是终端，尝试echo test | ./kilo
}
/*** terminal ***/
char editorReadKey() {//等待一个按键操作，并返回它，涉及读取表示单个按键操作的多字节
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
}
/*** input ***/
void editorProcessKeypress() {//等待按键，将把各种ctrl键组合和其他特殊键映射到不同的编辑器功能，并将任何字母数字和其他可打印键的字符插入到正在编辑的文本中
  char c = editorReadKey();
  switch (c) {
    case CTRL_KEY('q'):
      exit(0);
      break;
  }
}
int main(){
    enableRawMode();
    while(1){
      char c='\0';
      if(read(STDIN_FILENO,&c,1)== -1 && errno != EAGAIN) die("read");
      if(iscntrl(c)){//iscntrl()检查一个字符是否为不可打印字符/控制字符，ASCII码32-126都是可打印字符
        printf("%d\r\n",c);//\r添加回车符
        }else{
        printf("%d('%c')\r\n",c,c);//将各种按键如何转换为读取的字节
        }
        if(c == 'q')break;  
    }//按q退出

    return 0;
}
