//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt.rg_start >= rg_elmt.rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt.rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = &rg_elmt;

  return 0;
}

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  struct vm_area_struct *pvma= mm->mmap;

  if(mm->mmap == NULL)
    return NULL;

  int vmait = 0;
  
  while (vmait < vmaid)
  {
    if(pvma == NULL)
	  return NULL;

    vmait++;
    pvma = pvma->vm_next;
  }

  return pvma;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if(rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size 
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, int size, int *alloc_addr)
{
  #ifdef TDBG
    printf("alloc\n");
  #endif
  #ifdef RAM_STATUS_DUMP
    printf("\n===== ALLOC size = %d at rgid #%d, proc #%d ======\n\n", size, rgid, caller->pid);
  #endif

  /*Allocate at the toproof */
  struct vm_rg_struct rgnode;

  /* Validate region ID */
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ) {
    printf("Process %d free error: Invalid region ID\n", caller->pid);
    return -1;
  }
  else if (caller->mm->symrgtbl[rgid].rg_start > caller->mm->symrgtbl[rgid].rg_end) {
    printf("Process %d allocated error: Region was alloc before\n", caller->pid);
    return -1;
  }

  /* get_free_vmrg_area success */
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    // printf("get_free_vmrg_area SUCCESSFUL, %d, %d\n", rgnode.rg_start, rgnode.rg_end);

    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;

    struct vm_area_struct *current_vma = get_vma_by_num(caller->mm, vmaid);
    current_vma->sbrk += size;

    *alloc_addr = rgnode.rg_start;

    for (int i = rgnode.rg_start / 256; i <= rgnode.rg_end / 256; i++) {
      // int current_pgn = PAGING_PGN(caller->mm->symrgtbl[rgid].rg_start);
      uint32_t current_pte = caller->mm->pgd[i];
      int current_fpn;


      if (!PAGING_PAGE_PRESENT(current_pte)) {
        // int tgtfpn = PAGING_SWP(current_pte);
        int tgtfpn = GETVAL(caller->mm->pgd[i], GENMASK(20, 0), 5);

      #ifdef RAM_STATUS_DUMP
        printf("NOTE: Region is currently in SWAP\n");
        printf("TARGET FRAME: %d\n", tgtfpn);
      #endif

        uint32_t *vicpte;
        int vicfpn, swpfpn;

        vicpte = find_victim_page(caller->mm, &vicpte);
        vicfpn = PAGING_PTE_FPN(*vicpte);

        MEMPHY_get_freefp(caller->active_mswp, &swpfpn);

        /* SWAP OUT */
        __swap_cp_page(caller->mram, vicfpn, caller->active_mswp, swpfpn);
        pte_set_swap(vicpte, 0, swpfpn);

        /* SWAP IN */
        __swap_cp_page(caller->active_mswp, tgtfpn, caller->mram, vicfpn);
        pte_set_fpn(&caller->mm->pgd[i], vicfpn);

        MEMPHY_put_freefp(caller->active_mswp, tgtfpn);
      }

      // printf("**DEBUGGING FROM ALLOC**\n");
      // printf_page_LRU();
    }

  #ifdef RAM_STATUS_DUMP
    printf("------------------------------------------\n");
    for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
      if (caller->mm->symrgtbl[i].rg_start == 0 && caller->mm->symrgtbl[i].rg_end == 0) {
        continue;
      } else {
        printf("Region #%d : [%lu, %lu]", i,caller->mm->symrgtbl[i].rg_start, caller->mm->symrgtbl[i].rg_end);
        if (i == rgid) {
          printf(" <--\n");
        } else {
          printf("\n");
        }
      }
    }

    printf("------------------------------------------\n");
    struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
    printf("vma_id #%d : [%lu, %lu], sbrk = %lu\n", cur_vma->vm_id, cur_vma->vm_start, cur_vma->vm_end, cur_vma->sbrk);
    RAM_dump_test(caller->mram);
    
    printf("------------------------------------------\n");
    printf("Process %d Free Region list \n",caller->pid);
    struct vm_rg_struct* temp=caller->mm->mmap->vm_freerg_list;
    if (temp == NULL) {
      printf("No free regions\n");
    }
    while (temp != NULL) {
      if(temp->rg_start != temp->rg_end)
        printf("[%lu, %lu]\tsize = %d\n", temp->rg_start,temp->rg_end, temp->rg_end - temp->rg_start); 
      temp=temp->rg_next;
    }
  #endif

    return 0;
  }

  /* TODO: get_free_vmrg_area FAILED handle the region management (Fig.6)*/
  /*Attempt to increate limit to get space */
  //int inc_limit_ret
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  int inc_sz = PAGING_PAGE_ALIGNSZ(size);
  
  int old_sbrk, old_end;
  old_sbrk = cur_vma->sbrk;
  old_end = cur_vma->vm_end;

  /* TODO INCREASE THE LIMIT
   * inc_vma_limit(caller, vmaid, inc_sz)
   */

  /* Failed increase limit */
  // if (inc_vma_limit(caller, vmaid, size) < 0) return -1;
  inc_vma_limit(caller, vmaid, size);

  /*Successful increase limit */
  caller->mm->symrgtbl[rgid].rg_start = old_end;
  caller->mm->symrgtbl[rgid].rg_end = old_end + size;

  *alloc_addr = old_end;
  // cur_vma->sbrk += size;
  
  /* Add free_rg to freerg_list */
  struct vm_rg_struct *rg_node_temp = malloc(sizeof(struct vm_rg_struct));
  rg_node_temp->rg_start = old_end + size;
  rg_node_temp->rg_end = cur_vma->vm_end;
  cur_vma->sbrk = old_end + size;

  enlist_vm_freerg_list(caller->mm, rg_node_temp);

#ifdef RAM_STATUS_DUMP
  printf("------------------------------------------\n");
  for (int it = 0; it < PAGING_MAX_SYMTBL_SZ; it++)
  {
    if (caller->mm->symrgtbl[it].rg_start == 0 && caller->mm->symrgtbl[it].rg_end == 0)
      continue;
    else {
      printf("Region #%d: [%lu, %lu]", it, caller->mm->symrgtbl[it].rg_start, caller->mm->symrgtbl[it].rg_end); 
      if (it == rgid) {
        printf(" <--\n");
      } else {
        printf("\n");
      }
    }
  }

  printf("------------------------------------------\n");
  printf("vma_id #%d: [%lu, %lu], sbrk = %lu\n", cur_vma->vm_id,cur_vma->vm_start,cur_vma->vm_end,cur_vma->sbrk);
  RAM_dump_test(caller->mram);
  printf("------------------------------------------\n");
  printf("Process %d Free Region list \n",caller->pid);
  struct vm_rg_struct* temp=caller->mm->mmap->vm_freerg_list;
  if (temp == NULL) {
    printf("No free regions\n");
  }
  while (temp != NULL) {
    if(temp->rg_start != temp->rg_end)
      printf("[%lu, %lu]\tsize = %d\n", temp->rg_start,temp->rg_end, temp->rg_end - temp->rg_start); 
    temp=temp->rg_next;
  }
  
#endif

  return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size 
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
#ifdef TDBG
        printf("__free\n");
  #endif
  struct vm_rg_struct *rgnode;
  
  if(rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ){
    return -1;
    printf("Process %d free error: Invalid region\n",caller->pid);
  }


  /* TODO: Manage the collect freed region to freerg_list */

  rgnode= get_symrg_byid(caller->mm, rgid);
  if(rgnode->rg_start==rgnode->rg_end)
  {
    printf("Process %d FREE Error: Region wasn't alloc or was freed before\n",caller->pid);
    return -1;
  }
  struct vm_rg_struct* rgnode_temp=malloc(sizeof(struct vm_rg_struct));

  // Clear content of region in RAM
  // BYTE value;
  // value = 0;
  // for(int i = rgnode->rg_start; i < rgnode->rg_end; i++)
  //   pg_setval(caller->mm,i,value,caller);

  int rgnode_size = rgnode->rg_end - rgnode->rg_start; 

  //Create new node for region
  rgnode_temp->rg_start=rgnode->rg_start;
  rgnode_temp->rg_end=rgnode->rg_end;
  rgnode->rg_start=rgnode->rg_end = 0;
  
  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, rgnode_temp);
  #ifdef RAM_STATUS_DUMP
  	printf("------------------------------------------\n");
  	printf("\n===== FREE size = %d at rgid #%d, proc #%d ======\n\nRegion #%d after free: [%lu,%lu]\n", rgnode_size, rgid, caller->pid, rgid, rgnode->rg_start, rgnode->rg_end);
  	for (int it = 0; it < PAGING_MAX_SYMTBL_SZ; it++)
  	{
  		if (caller->mm->symrgtbl[it].rg_start == 0 && caller->mm->symrgtbl[it].rg_end == 0)
  			continue;
  		else
  			printf("Region #%d : [%lu, %lu]\n", it, caller->mm->symrgtbl[it].rg_start, caller->mm->symrgtbl[it].rg_end); 
  	}
    printf("------------------------------------------\n");
    printf("Process %d Free Region list \n",caller->pid);
    struct vm_rg_struct* temp=caller->mm->mmap->vm_freerg_list;
    while (temp!=NULL)
  	{
      if(temp->rg_start!=temp->rg_end)
        printf("[%lu, %lu]\tsize = %d\n", temp->rg_start,temp->rg_end, temp->rg_end - temp->rg_start); 
      temp=temp->rg_next;
  	}
    printf("------------------------------------------\n\n");
  #endif 
  return 0;
}

/*pgalloc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size 
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int pgalloc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
  int addr;

  /* By default using vmaid = 0 */
  return __alloc(proc, 0, reg_index, size, &addr);
}

/*pgfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size 
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int pgfree_data(struct pcb_t *proc, uint32_t reg_index)
{
   return __free(proc, 0, reg_index);
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  uint32_t pte = mm->pgd[pgn];
 
  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    int vicpgn, swpfpn; 
    int vicfpn;
    uint32_t vicpte;

    int tgtfpn = PAGING_SWP(pte);//the target frame storing our variable

    /* TODO: Play with your paging theory here */
    /* Find victim page */

    if (find_victim_page(caller->mm, &vicpgn) == -1){
      return -1;
    }

    /* Get free frame in MEMSWP */
    if (MEMPHY_get_freefp(caller->active_mswp, &swpfpn) == -1){
      return -1;
    }

    vicpte = mm->pgd[vicpgn];
    vicfpn = PAGING_FPN(vicpte);
    /* Do swap frame from MEMRAM to MEMSWP and vice versa*/
    /* Copy victim frame to swap */
    __swap_cp_page(caller->mram, vicfpn, caller->active_mswp, swpfpn);
    /* Copy target frame from swap to mem */
    __swap_cp_page(caller->active_mswp, tgtfpn, caller->mram, vicfpn);

    /* Update page table */
    pte_set_swap(&mm->pgd[vicpgn], 0, swpfpn);

    /* Update its online status of the target page */
    
    pte_set_fpn(&pte, tgtfpn);

#ifdef CPU_TLB
    /* Update its online status of TLB (if needed) */
    printf("heheheheheeh")
    pte_set_fpn(&mm->pgd[pgn], vicfpn) ;
#endif

    enlist_pgn_node(&caller->mm->fifo_pgn,pgn);
  }

  *fpn = PAGING_FPN(pte);

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess 
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if(pg_getpage(mm, pgn, &fpn, caller) != 0) 
    return -1; /* invalid page access */

  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  MEMPHY_read(caller->mram,phyaddr, data);

  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess 
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if(pg_getpage(mm, pgn, &fpn, caller) != 0) 
    return -1; /* invalid page access */

  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  MEMPHY_write(caller->mram,phyaddr, value);

   return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region 
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size 
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE *data)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if(currg == NULL || cur_vma == NULL) /* Invalid memory identify */
	  return -1;

  pg_getval(caller->mm, currg->rg_start + offset, data, caller);

  return 0;
}


/*pgwrite - PAGING-based read a region memory */
int pgread(
		struct pcb_t * proc, // Process executing the instruction
		uint32_t source, // Index of source register
		uint32_t offset, // Source address = [source] + [offset]
		uint32_t destination) 
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);

  destination = (uint32_t) data;
#ifdef IODUMP
  printf("read region=%d offset=%d value=%d\n", source, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  MEMPHY_dump(proc->mram);
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region 
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size 
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE value)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  
  if(currg == NULL || cur_vma == NULL) /* Invalid memory identify */
	  return -1;

  pg_setval(caller->mm, currg->rg_start + offset, value, caller);

  return 0;
}

/*pgwrite - PAGING-based write a region memory */
int pgwrite(
		struct pcb_t * proc, // Process executing the instruction
		BYTE data, // Data to be wrttien into memory
		uint32_t destination, // Index of destination register
		uint32_t offset)
{
#ifdef IODUMP
  printf("write region=%d offset=%d value=%d\n", destination, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  MEMPHY_dump(proc->mram);
#endif

  return __write(proc, 0, destination, offset, data);
}


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  int pagenum, fpn;
  uint32_t pte;


  for(pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte= caller->mm->pgd[pagenum];

    if (!PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->mram, fpn);
    } else {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->active_mswp, fpn);    
    }
  }

  return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct* get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, int size, int alignedsz)
{
  struct vm_rg_struct * newrg;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  newrg = malloc(sizeof(struct vm_rg_struct));

  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end = newrg->rg_start + size;

  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, int vmastart, int vmaend)
{
    //struct vm_area_struct *vma = caller->mm->mmap;

    /* TODO validate the planned memory area is not overlapped */
    struct vm_area_struct *vmit = caller->mm->mmap;

    while (vmit != NULL) {
        if (vmastart < vmit->vm_start && vmaend > vmit->vm_end) {
            printf("vm area overlapped\n");
            return -1;
        }
        vmit = vmit->vm_next;
    }

    return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size 
 *
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, int inc_sz)
{
  struct vm_rg_struct * newrg = malloc(sizeof(struct vm_rg_struct));
  int inc_amt = PAGING_PAGE_ALIGNSZ(inc_sz);
  int incnumpage =  inc_amt / PAGING_PAGESZ;
  struct vm_rg_struct *area = get_vm_area_node_at_brk(caller, vmaid, inc_sz, inc_amt);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  int old_end = cur_vma->vm_end;

  /*Validate overlap of obtained region */
  if (validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end) < 0)
    return -1; /*Overlap and failed allocation */

  /* The obtained vm area (only) 
   * now will be alloc real ram region */
  cur_vma->vm_end += inc_sz;
  if (vm_map_ram(caller, area->rg_start, area->rg_end, 
                    old_end, incnumpage , newrg) < 0)
    return -1; /* Map the memory to MEMRAM */

  return 0;

}

/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, int *retpgn)
{
   // Get the first page node from the FIFO list
   struct pgn_t *pg = mm->fifo_pgn;

   /* TODO: Implement the theoretical mechanism to find the victim page */

   // If the FIFO list is empty, return -1 (no victim page found)
   if (!pg)
   {
       return -1;
   }

   // Traverse the FIFO list to find the last node
   struct pgn_t *prev = NULL;
   while (pg->pg_next)
   {
       prev = pg;
       pg = pg->pg_next;
   }

   // Store the page number of the victim page in *retpgn
   *retpgn = pg->pgn;

   // Remove the last node from the FIFO list
   prev->pg_next = NULL;

   // Free the memory occupied by the last node
   free(pg);

   return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size 
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        { /*End of free list */
          rgit->rg_start = rgit->rg_end;	//dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
    }
    else
    {
      rgit = rgit->rg_next;	// Traverse next rg
    }
  }

 if(newrg->rg_start == -1) // new region not found
   return -1;

 return 0;
}

//#endif
