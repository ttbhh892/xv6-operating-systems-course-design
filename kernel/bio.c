// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

struct {
  // 只在跨桶寻找空闲缓存块时使用。
  struct spinlock evict_lock;

  struct buf buf[NBUF];

  struct {
    struct spinlock lock;
    struct buf head;
  } bucket[NBUCKET];
} bcache;

static uint
bhash(uint blockno)
{
  return blockno % NBUCKET;
}
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.evict_lock, "bcache.evict");

  // 初始化每个哈希桶。
  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head.prev = &bcache.bucket[i].head;
    bcache.bucket[i].head.next = &bcache.bucket[i].head;
  }

  // 将所有缓存块平均放入不同哈希桶。
  for(int i = 0; i < NBUF; i++){
    b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");

    int h = i % NBUCKET;

    b->next = bcache.bucket[h].head.next;
    b->prev = &bcache.bucket[h].head;
    bcache.bucket[h].head.next->prev = b;
    bcache.bucket[h].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint h = bhash(blockno);

  // 先只搜索目标哈希桶。
  acquire(&bcache.bucket[h].lock);

  for(b = bcache.bucket[h].head.next;
      b != &bcache.bucket[h].head;
      b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[h].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucket[h].lock);

  // 缓存未命中。串行化缓存块回收，防止产生重复缓存块。
  acquire(&bcache.evict_lock);

  // 等待 evict_lock 期间，其他 CPU 可能已经加入了该块，所以重新检查。
  acquire(&bcache.bucket[h].lock);

  for(b = bcache.bucket[h].head.next;
      b != &bcache.bucket[h].head;
      b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[h].lock);
      release(&bcache.evict_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucket[h].lock);

  // 在所有桶中寻找 refcnt 为 0 的缓存块。
  for(int i = 0; i < NBUCKET; i++){
    acquire(&bcache.bucket[i].lock);

    for(b = bcache.bucket[i].head.prev;
        b != &bcache.bucket[i].head;
        b = b->prev){
      if(b->refcnt == 0){
        // 从原来的桶中删除。
        b->next->prev = b->prev;
        b->prev->next = b->next;

        if(i == h){
          // 原桶就是目标桶，当前已经持有目标桶的锁。
          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;
          b->refcnt = 1;

          b->next = bcache.bucket[h].head.next;
          b->prev = &bcache.bucket[h].head;
          bcache.bucket[h].head.next->prev = b;
          bcache.bucket[h].head.next = b;

          release(&bcache.bucket[i].lock);
        } else {
          // 先释放原桶锁，再加入目标桶。
          release(&bcache.bucket[i].lock);
          acquire(&bcache.bucket[h].lock);

          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;
          b->refcnt = 1;

          b->next = bcache.bucket[h].head.next;
          b->prev = &bcache.bucket[h].head;
          bcache.bucket[h].head.next->prev = b;
          bcache.bucket[h].head.next = b;

          release(&bcache.bucket[h].lock);
        }

        release(&bcache.evict_lock);
        acquiresleep(&b->lock);
        return b;
      }
    }

    release(&bcache.bucket[i].lock);
  }

  release(&bcache.evict_lock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  uint h;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  h = bhash(b->blockno);

  acquire(&bcache.bucket[h].lock);
  b->refcnt--;
  release(&bcache.bucket[h].lock);
}

void
bpin(struct buf *b)
{
  uint h = bhash(b->blockno);

  acquire(&bcache.bucket[h].lock);
  b->refcnt++;
  release(&bcache.bucket[h].lock);
}

void
bunpin(struct buf *b)
{
  uint h = bhash(b->blockno);

  acquire(&bcache.bucket[h].lock);
  b->refcnt--;
  release(&bcache.bucket[h].lock);
}


