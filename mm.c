/*
 * Malloclab explicit list implementation
 *
 * Our explicit free list implementation uses the head and 
 * footers to store pointers to the next and previous free blocks.
 * 
 * The free list is split into segregated lists to improve performance.
 *
 * Each segregated list will store free blocks in order from smallest to 
 * largest. In this way we search for the best fit to improve memory utility.
 * 
 * global variables minList and numFree are added to speed up searches.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "group371",
    /* First member's full name */
    "Eric Lewis",
    /* First member's SLO (@cs.vt.edu) email address*/
    "airshp12@cs.vt.edu",
    /* Second member's full name (leave blank if none) */
    "Hang Lin",
    /* Second member's SLO (@cs.vt.edu) email address*/
    "lin1412@cs.vt.edu"
};



/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

static char *heap_listp = NULL;
int minList = 0;             // keeps track of the list number for the free list containing the first free block  
int numFree = 0;             // Keeps track of the number of free blocks            
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void add_free_list(void *bp);
static void remove_free_list(void *bp);
static void *find_fit(size_t adjSize);
static void place(void *bp, size_t adjSize);
//static int mm_check(void);

/////////// Macros from the book /////////////////

/* Basic constants and macros */
 #define WSIZE 4 /* Word and header/footer size (bytes) */
 #define DSIZE 8 /* Double word size (bytes) */
 #define CHUNKSIZE (1<<12) /* Extend heap by this amount (bytes) */

 #define MAX(x, y) ((x) > (y)? (x) : (y))

 /* Pack a size and allocated bit into a word */
 #define PACK(size, alloc) ((size) | (alloc))

 /* Read and write a word at address p */
 #define GET(p) (*(unsigned int *)(p))
 #define PUT(p, val) (*(unsigned int *)(p) = (val))

 /* Read the size and allocated fields from address p */
 #define GET_SIZE(p) (GET(p) & ~0x7)
 #define GET_ALLOC(p) (GET(p) & 0x1)

 /* Given block ptr bp, compute address of its header and footer */
 #define HDRP(bp) ((char *)(bp) - WSIZE)
 #define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

 /* Given block ptr bp, compute address of next and previous blocks */
 #define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
 #define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* 
 * mm_init initializes the initial heap area. It also creates 84 words for the 
 * segregated lists. 
 *
 * return -1 if the allocation fails, 0 otherwise
 */
int mm_init(void)
{
    // initialize heap, return -1 if failed
    if ((heap_listp = mem_sbrk(88*WSIZE)) == (void *)-1){
        return -1;
    }

    // start with no free blocks
    minList = 100; 
    numFree = 0; 

    PUT(heap_listp, 0); 
    PUT(heap_listp + (1*WSIZE), PACK(43*DSIZE, 1)); 

    // initialize 84 segregated free lists
    int i;
    for(i = 2; i < 86; i++) {
        PUT(heap_listp + (i*WSIZE), 0); 
    }

    // prologue header and footer
    PUT(heap_listp + (87*WSIZE), PACK(0, 1));
    PUT(heap_listp + (86*WSIZE), PACK(43*DSIZE, 1));

    // start by pointing to the prologue
    heap_listp += (2*WSIZE);  

    /* Extend the empty heap with a free block with 
    *  size CHUNKSIZE, return -1 if failed 
    */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL){
        return -1;
    }

    return 0; 
}



/*
 * extends the size of the heap by words words.
 *
 * Return -1 if heap cant be extended, 0 otherwise;
 */
 static void *extend_heap(size_t words)
 {
    char *bp;
    size_t size;

    // Must extend by an even number to maintain allignment
    if(words % 2){
        size = (words+1) * WSIZE;
    }
    else{
        size = words * WSIZE;
    }

    //return null if failed moving heap pointer
    bp = mem_sbrk(size);
    if ((long)(bp) == -1){
        return NULL;
    }

    // set the free block header and footer
    PUT(HDRP(bp), PACK(size, 0)); 
    PUT(FTRP(bp), PACK(size, 0)); 

    // set epilogue header
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    // Combine consecutive free blocks
    bp = coalesce(bp);

    return bp;
 }
 
 
/* 
 * Search the free list for for a large enough free block. If found then place
 * it. If not found then allocate size for it. Extend the heap if necessary.
 *
 * Return NULL if either size == 0 or heap is full, 
 * otherwise return a pointer to the new block.
 */
void *mm_malloc(size_t size)
{
    // Dont waste time allocating nothing
    if (size == 0){
        return NULL;
    }
 
    // Get an adjusted size so that we conform to allignment
    size_t adjSize; 
    if (size <= DSIZE){
        adjSize = 2*DSIZE;
    }
    else{
        adjSize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    }

    // search free list for a fiting block
    char *bp = find_fit(adjSize);
    if (bp != NULL) {
        place(bp, adjSize);
        return bp;
    }

    // Extend heap if necessary
    size_t extendsize = MAX(adjSize,CHUNKSIZE);
    bp = extend_heap(extendsize/WSIZE);
    if (bp  == NULL){
        return NULL;
    }
        
    //place in new memory.  
    place(bp, adjSize);
    return bp;
}


/* 
 * Searches the free list for a free block lareg enough for the requesting 
 * malloc call. Start at the minimum free list that is lareg enough to 
 * save time.
 */
 static void *find_fit(size_t size)
 {
     
    //no free blocks
    if(numFree == 0){
        return NULL;
    }
         
    // calculate where the minimum free list large enough is
    int minListLocal = size / 50;
      
    if(minListLocal < minList){
        minListLocal = minList;
    }   
    else if(minListLocal > 83){
        minListLocal = 83;
    } 
         
    //Loop through the remaining free lists starting at min list.  
    for(; minListLocal < 84; minListLocal++){
        int i = 0;
        // look for a large enough block
        void *bp = (char *)GET(heap_listp + (minListLocal * WSIZE));
        for (;  i < 250 && (int)bp != 0 && GET_SIZE(HDRP(bp)) > 0; bp = (char *)GET(bp+WSIZE)) {
            if (!GET_ALLOC(HDRP(bp)) && (size <= GET_SIZE(HDRP(bp)))) {
                //found one
                return bp;
            }
            i++;
        }
    }
    //if no fits wer found return null
    return NULL;
}
 
/* 
 * places a block into a block pointer. split the block into an allocated
 * and free block if it is large enough to accomodate both. That block will then 
 * be removed from free list.
 */ 
 static void place(void *bp, size_t size)
 {
    size_t currentSize = GET_SIZE(HDRP(bp));
    //Large enough to hold bp AND a free block 
    if ((currentSize - size) >= (2*DSIZE)) {
                 
        // remove from free list
        remove_free_list(bp);
                 
        PUT(HDRP(bp), PACK(size, 1));
        PUT(FTRP(bp), PACK(size, 1));
                 
        void * nextBP = NEXT_BLKP(bp);
                 
        PUT(HDRP(nextBP), PACK(currentSize-size, 0));
        PUT(FTRP(nextBP), PACK(currentSize-size, 0));
                                  
        //ADD new free block to free list
        add_free_list(nextBP);
    }
         
    //not large enough for a free block to remain.
    else {
        //set to allocated 
        PUT(HDRP(bp), PACK(currentSize, 1));
        PUT(FTRP(bp), PACK(currentSize, 1));
        //remove from free list 
        remove_free_list(bp);
    }
 }


/* 
 * remove a pointer to a free block from the list of free blocks.
 * update global values to reflect changes
 */  
 static void remove_free_list(void *bp){              
    //decrementfree count. 
    numFree--; 
         
    int size = GET_SIZE(HDRP(bp));
         
    int minListLocal = size / 50;
    if(minListLocal > 83){
        minListLocal = 83;
    } 
         
    // set up prev and next to represent neighbor
    size_t prev = GET(bp);
    size_t next = GET(bp + WSIZE);

    // prev is empty and next is empty;
    if(prev == 0 && next == 0) { 
        //set this list pointer to 0 indicating no items on the list. 
        PUT(heap_listp+(minListLocal * WSIZE), 0);
                 
        if(minList == minListLocal) { 
            if(numFree > 0){
                int i;
                for (i = minListLocal; GET(heap_listp+(i * WSIZE)) == 0; i++){
                    minList = i;
                }
            }
            else(minList = 100);                         
        }
    }
         
    // prev is empty and next is full 
    else if (prev == 0 && next != 0){
        PUT(heap_listp+(minListLocal * WSIZE), next);
        PUT((char *)next, 0);
    }
         
    // prev is full and next is empty
    else if (prev != 0 && next == 0){
        PUT(((char *)prev + WSIZE), 0);
    }
                 
    // prev is full and next is full
    else {
        PUT(((char *)prev + WSIZE), next);        
        PUT(((char *)next), prev);        
    }
 }
 
 
 /* 
 * add a pointer to the free list and update globals to reflect changes
 */  
 static void add_free_list(void *bp)
 {     
    numFree++; 
       
    int size = GET_SIZE(HDRP(bp));
    int minListLocal = size / 50;

    if(minListLocal > 83){
        minListLocal = 83;
    }
    
    //update global minList
    if(minList > minListLocal || minList == 100){
        minList = minListLocal;
    }
        
    void *tempNext;
    void *tempPrev;
  
    void *tempCurrent = (char *)GET(heap_listp + (minListLocal * WSIZE));
        
    //free list is empty 
    if(tempCurrent == 0){
        PUT(heap_listp + (minListLocal * WSIZE), (int)bp);        
        PUT(bp, 0); 
        PUT(bp+WSIZE, 0);
    }
        
    //the list is not free
    else {
        tempPrev = (char *)GET(heap_listp + (minListLocal * WSIZE));
        //find where to put the free block        
        for (; (int)tempCurrent != 0 && GET_SIZE(HDRP(tempCurrent)) < size; tempCurrent = (char *)GET(tempCurrent+WSIZE)){
            tempPrev = tempCurrent;
        }
            
        tempCurrent = tempPrev;
        tempNext = (char *)GET(tempCurrent + WSIZE); 
        PUT(tempCurrent + WSIZE, (int)bp); 
        if((int)tempNext != 0){
            PUT(tempNext, (int)bp);
        }
        //set pointers to this block
        PUT(bp, (int)tempCurrent); 
        PUT(bp+WSIZE, (int)tempNext);          
    }
}

/*
 * free a block pointed to by bp
 * coalesce to save time in searches.
 */
void mm_free(void *bp)
{
        
    size_t size = GET_SIZE(HDRP(bp));

    //set header and footer    
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
        
    coalesce(bp);
}

 /* 
 * combine neighboring free blocks and put the resulting 
 * free block in the free list
 */  
 static void *coalesce(void *bp)
 {
    size_t prevAlloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t nextAlloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    //previous block is free and next block is free 
    if(!prevAlloc && !nextAlloc) {  
        remove_free_list(PREV_BLKP(bp));
        remove_free_list(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        add_free_list(bp);
    }

    //previous block is free but next block is allocated 
    else if (!prevAlloc && nextAlloc) { 
        remove_free_list(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        add_free_list(bp);
    }

    // previous block is allocated but next block is free 
    else if (prevAlloc && !nextAlloc) {
        remove_free_list(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
        add_free_list(bp);
    }
    // previous block is allocated and next block is allocated
    else {  
        add_free_list(bp);
    }
    //mm_check();
    return bp;
 }

/*
 * Reallocate a block of memory to a new size
 * if ptr is null, behave as mm_maloc
 * if size == 0, behave as free
 *    
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    
    size_t prevAlloc = GET_ALLOC(FTRP(PREV_BLKP(oldptr)));
    size_t nextAlloc = GET_ALLOC(HDRP(NEXT_BLKP(oldptr)));
    
    size_t oldSize = GET_SIZE(HDRP(oldptr));

    int change;
    
    //is new size is larger?
    if(oldSize  < size + DSIZE){ 
        change = 1;
    }
    else{
        change = 0;
    }

    void *newptr;
    // if size is 0, just call mm_free
    if (size == 0){
        mm_free(ptr);
        newptr = 0;
        return NULL;
    }
    
    //if pointer is NULL, just call mm_malloc
    if (oldptr == NULL){        
        return mm_malloc(size);
    }

    // ptr is decreasing in size and there is enough leaft over space to make a free block
    if(change == 0 && (oldSize - size - DSIZE) > (2*DSIZE)){
        // adjust block size       
        if (size <= DSIZE){
            size = 2*DSIZE;
        }
        else{
            size =  DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); // align size
        }

        // the adjusted size still has enough space to make a free block
        if((oldSize - size) > (2*DSIZE)){
            //reset header and footer
            PUT(HDRP(oldptr), PACK(size, 1)); 
            PUT(FTRP(oldptr), PACK(size, 1)); 
         

            newptr = oldptr;
            oldptr =  (NEXT_BLKP(newptr)); 
            
            //set header and footer for the empty block
            PUT(HDRP(oldptr), PACK(oldSize - size, 0));
            PUT(FTRP(oldptr), PACK(oldSize - size, 0));
          
            coalesce(oldptr);
        
            return newptr;
        }
    }

    size_t tempSize;
    size_t copySize;
    void *temp;
    
    // ptr is decreasing in size but there isnt enough after to make a free block
    if(change == 0) {
        return ptr;
    }
    
    // ptr is increasing in size
    else {

        int tempPrev = GET_SIZE(HDRP(PREV_BLKP(oldptr)));
        int tempNext = GET_SIZE(HDRP(NEXT_BLKP(oldptr)));

        // next and prev are unallocated and will create a large enough block 
        if (nextAlloc == 0 && prevAlloc == 0 && (tempPrev + tempNext + oldSize) >= (size + DSIZE)){
            newptr = PREV_BLKP(oldptr);
            temp = NEXT_BLKP(oldptr);
            tempSize = GET_SIZE(FTRP(newptr)) + GET_SIZE(FTRP(temp));
            //remove from free list since they will combine into 1
            remove_free_list(PREV_BLKP(oldptr));
            remove_free_list(NEXT_BLKP(oldptr));
                           
            // adjust size 
            if (size <= DSIZE){
                size = 2*DSIZE;
            }
            else{
                size =  DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); 
            }

            // if not big enough for new free block
            if((tempSize + oldSize) < (size + 2*DSIZE)){
                size = tempSize + oldSize;
            }
                         
            PUT(HDRP(newptr), PACK(size, 1));
            copySize = GET_SIZE(HDRP(oldptr));
            memcpy(newptr, oldptr, copySize);
            PUT(FTRP(newptr), PACK(size, 1));
            //new free block 
            if((tempSize + oldSize) >= (size + 2*DSIZE)){ 
                temp = NEXT_BLKP(newptr); 
                //set new header and footer for free block
                PUT(HDRP(temp), PACK(tempSize + oldSize - size, 0));
                PUT(FTRP(temp), PACK(tempSize + oldSize - size, 0));
                add_free_list(temp);
            }
            return newptr;                                       
        }   

        // prev is unallocated and will create a large enough block when combined
        else if(prevAlloc == 0 && ((tempPrev + oldSize) >= (size + DSIZE))){
            newptr = PREV_BLKP(oldptr);
            tempSize = GET_SIZE(FTRP(newptr));
            remove_free_list(PREV_BLKP(oldptr));
            //adjust size
            if (size <= DSIZE){
                size = 2*DSIZE;
            }
            else{
                size =  DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
            }

            if((tempSize + oldSize) < (size + 2*DSIZE)){
                size = tempSize + oldSize;
            }
            //set new header and footer            
            PUT(HDRP(newptr), PACK(size, 1));         
            copySize = GET_SIZE(HDRP(oldptr));
            memcpy(newptr, oldptr, copySize);        
            PUT(FTRP(newptr), PACK(size, 1)); 
                     
            //new free block                
            if((tempSize + oldSize) >= (size + 2*DSIZE)){
                                        
                temp = NEXT_BLKP(newptr);
                //set new header and footer for free block      
                PUT(HDRP(temp), PACK(tempSize + oldSize - size, 0));
                PUT(FTRP(temp), PACK(tempSize + oldSize - size, 0));
                            
                add_free_list(temp);
            }
            return newptr;            
        }             
                
        // next is unallocated and will create a large enough block
        else if(nextAlloc == 0 && (tempNext + oldSize) >= (size + DSIZE)){
            temp = NEXT_BLKP(oldptr);
            tempSize = GET_SIZE(FTRP(temp));
            remove_free_list(NEXT_BLKP(ptr));
            //adjust size
            if (size <= DSIZE){
                size = 2*DSIZE;
            }
            else{
                size =  DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
            }

            if((tempSize + oldSize) < (size + 2*DSIZE)){
                size = tempSize + oldSize;
            }
            //set header and footer            
            PUT(HDRP(oldptr), PACK(size, 1));
            PUT(FTRP(oldptr), PACK(size, 1)); 
                   
            //new free block                
            if((tempSize + oldSize) >= (size + 2*DSIZE)){ 
                newptr = NEXT_BLKP(oldptr);
                //set new header and footer for free block                       
                PUT(HDRP(newptr), PACK(tempSize + oldSize - size, 0));
                PUT(FTRP(newptr), PACK(tempSize + oldSize - size, 0));
                            
                add_free_list(newptr);
            }
            return oldptr;                            
        }

        //prev and next are already allocated
        else{         
            newptr = mm_malloc(size);
            copySize = GET_SIZE(HDRP(oldptr));
            if (size < copySize){
                copySize = size;
            }
                
            memcpy(newptr, oldptr, copySize);   
            mm_free(oldptr);
        }
        return newptr;
    }
}

/*
 * Check for consistency between the heap and free list.
 * Make sure the pointers are valid.
 * Check if there are contiguous free blocks.
 */

/*

static int mm_check(void){
    int error = 0;
    size_t* topHeap =  mem_heap_lo();        
    size_t* bottomHeap =  mem_heap_hi();        
    void* tempPtr;

    //while there is a next pointer, keep looping
    for(tempPtr = topHeap; GET_SIZE(HDRP(tempPtr)) > 0; tempPtr = NEXT_BLKP(tempPtr)) {
                
        if (GET_ALLOC(HDRP(tempPtr)) == 0 && GET_ALLOC(HDRP(NEXT_BLKP(tempPtr)))==0){
            error += 1;
            //printf("Error: blocks %p and %p are contiguous free block\n", tempPtr, NEXT_BLKP(tempPtr));
        }

        if ((int)tempPtr > (int)bottomHeap || (int)tempPtr < (int)topHeap){        
            error += 1;
            //printf("Error: pointer %p out of heap bounds\n", tempPtr);
        }

        if ((size_t)tempPtr%8){
            error += 1;
            //printf("Error: %p misaligned our headers and payload\n", tempPtr);
        }
    }

    if( error == 0){
        return 1;
    }
    return 0;
 }
 */

