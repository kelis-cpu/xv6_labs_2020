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
void e1000_init(uint32 *xregs) {
    int i;

    initlock(&e1000_lock, "e1000");

    regs = xregs;

    // Reset the device
    regs[E1000_IMS] = 0;  // disable interrupts
    regs[E1000_CTL] |= E1000_CTL_RST;
    regs[E1000_IMS] = 0;  // redisable interrupts
    __sync_synchronize();

    // [E1000 14.5] Transmit initialization
    memset(tx_ring, 0, sizeof(tx_ring));
    for (i = 0; i < TX_RING_SIZE; i++) {
        tx_ring[i].status = E1000_TXD_STAT_DD;
        tx_mbufs[i] = 0;
    }
    regs[E1000_TDBAL] = (uint64)tx_ring;
    if (sizeof(tx_ring) % 128 != 0) panic("e1000");
    regs[E1000_TDLEN] = sizeof(tx_ring);
    regs[E1000_TDH] = regs[E1000_TDT] = 0;

    // [E1000 14.4] Receive initialization
    memset(rx_ring, 0, sizeof(rx_ring));
    for (i = 0; i < RX_RING_SIZE; i++) {
        rx_mbufs[i] = mbufalloc(0);
        if (!rx_mbufs[i]) panic("e1000");
        rx_ring[i].addr = (uint64)rx_mbufs[i]->head;
    }
    regs[E1000_RDBAL] = (uint64)rx_ring;
    if (sizeof(rx_ring) % 128 != 0) panic("e1000");
    regs[E1000_RDH] = 0;
    regs[E1000_RDT] = RX_RING_SIZE - 1;
    regs[E1000_RDLEN] = sizeof(rx_ring);

    // filter by qemu's MAC address, 52:54:00:12:34:56
    regs[E1000_RA] = 0x12005452;
    regs[E1000_RA + 1] = 0x5634 | (1 << 31);
    // multicast table
    for (int i = 0; i < 4096 / 32; i++) regs[E1000_MTA + i] = 0;

    // transmitter control bits.
    regs[E1000_TCTL] = E1000_TCTL_EN |                  // enable
                       E1000_TCTL_PSP |                 // pad short packets
                       (0x10 << E1000_TCTL_CT_SHIFT) |  // collision stuff
                       (0x40 << E1000_TCTL_COLD_SHIFT);
    regs[E1000_TIPG] = 10 | (8 << 10) | (6 << 20);      // inter-pkt gap

    // receiver control bits.
    regs[E1000_RCTL] = E1000_RCTL_EN |       // enable receiver
                       E1000_RCTL_BAM |      // enable broadcast
                       E1000_RCTL_SZ_2048 |  // 2048-byte rx buffers
                       E1000_RCTL_SECRC;     // strip CRC

    // ask e1000 for receive interrupts.
    regs[E1000_RDTR] = 0;  // interrupt after every received packet (no timer)
    regs[E1000_RADV] = 0;  // interrupt after every packet (no timer)
    regs[E1000_IMS] = (1 << 7);  // RXDW -- Receiver Descriptor Write Back
}

/**
 * 操作系统要发送数据包时，首先将数据放入环形缓冲区数组tx_ring内，然后递增E1000_TDT,网卡自动发送数据
 * 当网卡收到数据时，网卡首先利用DMA将数据放到rx_ring环形缓冲区数组中，然后发起一个中断，CPU收到中断后，直接读取rx_ring中的数据即可
 * 描述符存放的信息都是关于mbuf的
 * 接收描述符：
 * addr 网卡利用DMA将接收数据包写入内存的地址mbuf，
 * length 接收数据包的大小
 * 发送描述符：
 * addr 指向上层协议栈要发送数据包在内存中的地址mbuf，
 * length 要发送数据包的大小
 *
 *
 *
 * 总之：收发数据都是读写内存之后(mbuf)，更新mbuf描述符，OS/网卡通过读取mbuf描述符进行收发
 */

int e1000_transmit(struct mbuf *m) {
    //
    // Your code here.
    //
    // the mbuf contains an ethernet frame; program it into
    // the TX descriptor ring so that the e1000 sends it. Stash
    // a pointer so that it can be freed after sending.
    //
    struct tx_desc *tail_tx_desc;
    uint32 tail_tx_desc_id;

    acquire(&e1000_lock);
    tail_tx_desc_id = regs[E1000_TDT];
    tail_tx_desc = &tx_ring[tail_tx_desc_id];

    // 检查该描述符对应的包是否被发送
    if (!(tail_tx_desc->status & E1000_TXD_STAT_DD)) {
        // panic("the desc is not done\n");
        release(&e1000_lock);
        return -1;
    }
    // 释放掉在内存中已经发送的包
    if (tx_mbufs[tail_tx_desc_id]) mbuffree(tx_mbufs[tail_tx_desc_id]);
    tx_mbufs[tail_tx_desc_id] = m;
    // 设置描述符的addr指向包头
    tail_tx_desc->addr = (uint64)m->head;
    tail_tx_desc->length = m->len;
    // 设置网卡在发送数据包之后，将status中DD位置1
    tail_tx_desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
    // 更新TDT寄存器，指向下一个可用buffer描述符
    regs[E1000_TDT] = (tail_tx_desc_id + 1) % TX_RING_SIZE;
    release(&e1000_lock);
    return 0;
}

static void e1000_recv(void) {
    //
    // Your code here.
    //
    // Check for packets that have arrived from the e1000
    // Create and deliver an mbuf for each packet (using net_rx()).
    //
    struct rx_desc *tail_rx_desc;
    struct mbuf *recv_buf, *new_buf;
    uint32 tail_rx_desc_id;
    // 循环接收数据包
    while (1) {
        // 获取已经将数据写入内存的buffer描述符
        tail_rx_desc_id = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
        tail_rx_desc = &rx_ring[tail_rx_desc_id];
        // 还未写入
        if (!(tail_rx_desc->status & E1000_RXD_STAT_DD)) {
            return;
        }
        // 获取写入数据包在内存中的地址
        recv_buf = rx_mbufs[tail_rx_desc_id];
        recv_buf->len = tail_rx_desc->length;
        net_rx(recv_buf);  // 读取数据包，交由上层协议栈进行处理

        regs[E1000_RDT] = tail_rx_desc_id;

        new_buf = mbufalloc(0);  // 申请新的buffer给网卡写入接受的数据包
        rx_mbufs[tail_rx_desc_id] = new_buf;
        tail_rx_desc->addr = (uint64)new_buf->head;
        tail_rx_desc->status = 0;
    }
}

void e1000_intr(void) {
    // tell the e1000 we've seen this interrupt;
    // without this the e1000 won't raise any
    // further interrupts.
    regs[E1000_ICR] = 0xffffffff;

    e1000_recv();
}
