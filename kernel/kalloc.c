// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");

  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int id;

  if(((uint64)pa % PGSIZE) != 0 ||
     (char*)pa < end ||
     (uint64)pa >= PHYSTOP)
    panic("kfree");

  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  id = cpuid();

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);

  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r = 0;
  struct run *batch;
  struct run *last;
  int id;

  push_off();
  id = cpuid();

  // 先从当前 CPU 的空闲链表取一页。
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  // 当前 CPU 没有空闲页，就从其他 CPU 批量偷取。
  if(r == 0){
    for(int i = 0; i < NCPU; i++){
      if(i == id)
        continue;

      batch = 0;
      last = 0;

      acquire(&kmem[i].lock);

      batch = kmem[i].freelist;
      if(batch){
        last = batch;

        // 一次最多取走 64 页，减少以后反复争抢锁。
        for(int n = 1; n < 64 && last->next; n++)
          last = last->next;

        kmem[i].freelist = last->next;
        last->next = 0;
      }

      release(&kmem[i].lock);

      if(batch){
        // 第一页返回给调用者。
        r = batch;
        batch = batch->next;
        r->next = 0;

        // 剩余页面加入当前 CPU 的空闲链表。
        if(batch){
          acquire(&kmem[id].lock);
          last->next = kmem[id].freelist;
          kmem[id].freelist = batch;
          release(&kmem[id].lock);
        }

        break;
      }
    }
  }

  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE);

  return (void*)r;
}
