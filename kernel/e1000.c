#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
uint32 index;

  acquire(&e1000_lock);

  // TDT 指向网卡希望驱动填入的下一个发送描述符。
  index = regs[E1000_TDT];

  // DD 没有置位，说明网卡还没有使用完这个描述符。
  if((tx_ring[index].status & E1000_TXD_STAT_DD) == 0){
    release(&e1000_lock);
    return -1;
  }

  // 该描述符之前保存的数据包已经发送完成，现在可以释放。
  if(tx_mbufs[index] != 0)
    mbuffree(tx_mbufs[index]);

  // 填写发送描述符。
  tx_ring[index].addr = (uint64)m->head;
  tx_ring[index].length = m->len;
  tx_ring[index].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
  tx_ring[index].status = 0;

  // 保存 mbuf，等网卡发送完成后再释放。
  tx_mbufs[index] = m;

  // 确保描述符内容在通知网卡之前已经写入内存。
  __sync_synchronize();

  // 通知网卡存在新的发送描述符。
  regs[E1000_TDT] = (index + 1) % TX_RING_SIZE;

  release(&e1000_lock);
  
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
while(1){
    struct mbuf *m;
    struct mbuf *new_m;
    uint32 index;

    acquire(&e1000_lock);

    // RDT 指向上次处理完的描述符，
    // 因此下一个待检查的描述符是 RDT + 1。
    index = (regs[E1000_RDT] + 1) % RX_RING_SIZE;

    // DD 没置位，说明当前没有新的接收数据包。
    if((rx_ring[index].status & E1000_RXD_STAT_DD) == 0){
      release(&e1000_lock);
      break;
    }

    // 先为网卡准备一个新的接收缓冲区。
    new_m = mbufalloc(0);
    if(new_m == 0){
      release(&e1000_lock);
      break;
    }

    // 取出网卡刚刚写入的数据包。
    m = rx_mbufs[index];
    m->len = rx_ring[index].length;

    // 给该描述符换上新的空缓冲区。
    rx_mbufs[index] = new_m;
    rx_ring[index].addr = (uint64)new_m->head;
    rx_ring[index].status = 0;

    // 确保描述符更新完成后再通知网卡。
    __sync_synchronize();

    // 告诉网卡这个描述符已经重新可用。
    regs[E1000_RDT] = index;

    release(&e1000_lock);

    // 必须在释放 E1000 锁之后调用。
    // net_rx() 处理 ARP 时可能再次调用 e1000_transmit()。
    net_rx(m);
  }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
