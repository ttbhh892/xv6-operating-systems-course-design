#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
sieve(int input)
{
  int prime;
  int number;
  int next_pipe[2];

  // 管道中没有数字时，本级进程结束
  if(read(input, &prime, sizeof(prime)) != sizeof(prime)){
    close(input);
    exit(0);
  }

  printf("prime %d\n", prime);

  if(pipe(next_pipe) < 0){
    fprintf(2, "pipe failed\n");
    exit(1);
  }

  int pid = fork();

  if(pid < 0){
    fprintf(2, "fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // 子进程读取下一级管道
    close(next_pipe[1]);
    close(input);
    sieve(next_pipe[0]);
    exit(0);
  }

  // 父进程筛除当前质数的倍数
  close(next_pipe[0]);

  while(read(input, &number, sizeof(number)) == sizeof(number)){
    if(number % prime != 0){
      write(next_pipe[1], &number, sizeof(number));
    }
  }

  close(input);
  close(next_pipe[1]);
  wait(0);
  exit(0);
}

int
main(int argc, char *argv[])
{
  int first_pipe[2];

  if(pipe(first_pipe) < 0){
    fprintf(2, "pipe failed\n");
    exit(1);
  }

  int pid = fork();

  if(pid < 0){
    fprintf(2, "fork failed\n");
    exit(1);
  }

  if(pid == 0){
    close(first_pipe[1]);
    sieve(first_pipe[0]);
    exit(0);
  }

  close(first_pipe[0]);

  for(int number = 2; number <= 35; number++){
    write(first_pipe[1], &number, sizeof(number));
  }

  close(first_pipe[1]);
  wait(0);
  exit(0);
}

