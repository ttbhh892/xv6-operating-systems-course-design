#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char *args[MAXARG];
  char buf[512];
  int base;
  int n = 0;
  char c;

  if(argc < 2){
    fprintf(2, "usage: xargs command [arguments ...]\n");
    exit(1);
  }

  // 保存 xargs 后面原有的命令和参数
  for(base = 0; base < argc - 1 && base < MAXARG - 1; base++)
    args[base] = argv[base + 1];

  // 每次从标准输入读取一行
  while(read(0, &c, 1) == 1){
    if(c == '\n'){
      buf[n] = 0;

      int count = base;
      char *p = buf;

      // 按空格切分这一行的参数
      while(*p && count < MAXARG - 1){
        while(*p == ' ' || *p == '\t')
          p++;

        if(*p == 0)
          break;

        args[count++] = p;

        while(*p && *p != ' ' && *p != '\t')
          p++;

        if(*p)
          *p++ = 0;
      }

      args[count] = 0;

      if(fork() == 0){
        exec(args[0], args);
        fprintf(2, "xargs: exec %s failed\n", args[0]);
        exit(1);
      }

      wait(0);
      n = 0;
    } else if(n < sizeof(buf) - 1){
      buf[n++] = c;
    } else {
      fprintf(2, "xargs: input line too long\n");
      exit(1);
    }
  }

  // 处理最后一行没有换行符的情况
  if(n > 0){
    buf[n] = 0;

    int count = base;
    char *p = buf;

    while(*p && count < MAXARG - 1){
      while(*p == ' ' || *p == '\t')
        p++;

      if(*p == 0)
        break;

      args[count++] = p;

      while(*p && *p != ' ' && *p != '\t')
        p++;

      if(*p)
        *p++ = 0;
    }

    args[count] = 0;

    if(fork() == 0){
      exec(args[0], args);
      fprintf(2, "xargs: exec %s failed\n", args[0]);
      exit(1);
    }

    wait(0);
  }

  exit(0);
}
