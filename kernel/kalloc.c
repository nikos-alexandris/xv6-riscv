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
} kmem;

// 32768 pages of physical memory to keep track of
#define PRCSIZE ((PHYSTOP - KERNBASE) / PGSIZE)

// a uint8 is enough, since the maximum number of processes
// that can run simultaneously is 64 (NPROC). This means that
// the table is 32768 bytes (32KB) long.
struct {
  struct spinlock lock;
  uint8 table[PRCSIZE];
} prc;

// uint16 is enough, since __UINT16_MAX__ is 65535
#define PRCIDX(pa) ((uint16)((pa - KERNBASE) / PGSIZE))

void
prcinc(uint64 pa)
{
  acquire(&prc.lock);
  prc.table[PRCIDX(pa)]++;
  release(&prc.lock);
}

uint8
prccnt(uint64 pa)
{
  uint8 cnt;

  acquire(&prc.lock);
  cnt = prc.table[PRCIDX(pa)];
  release(&prc.lock);

  return cnt;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&prc.lock, "prc");
  freerange(end, (void*)PHYSTOP);
  acquire(&prc.lock);
  for(uint16 i = 0; i < PRCSIZE; i++)
    prc.table[i] = 0;
  release(&prc.lock);
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

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&prc.lock);
  if(prc.table[PRCIDX((uint64)pa)] > 1){
    prc.table[PRCIDX((uint64)pa)]--;
    release(&prc.lock);
    return;
  }
  prc.table[PRCIDX((uint64)pa)] = 0;
  release(&prc.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    acquire(&prc.lock);
    prc.table[PRCIDX((uint64)r)] = 1;
    release(&prc.lock);
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
    
  return (void*)r;
}
