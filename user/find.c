#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

void
find(char *path, char *target)
{
  char buf[512];
  char *p;
  int fd;
  struct dirent de;
  struct stat st;

  fd = open(path, 0);
  if(fd < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
    p = path + strlen(path);
    while(p >= path && *p != '/')
      p--;
    p++;

    if(strcmp(p, target) == 0)
      printf("%s\n", path);
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
      fprintf(2, "find: path too long\n");
      break;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;

      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;

      // 不递归进入当前目录和父目录
      if(strcmp(p, ".") == 0 || strcmp(p, "..") == 0)
        continue;

      find(buf, target);
    }
    break;
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "usage: find path filename\n");
    exit(1);
  }

  find(argv[1], argv[2]);
  exit(0);
}
