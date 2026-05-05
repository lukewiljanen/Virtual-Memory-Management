#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>


//Global constants and enums

#define PAGE_SIZE 256
#define FRAME_SIZE 256
#define NUM_PAGES 256
#define TLB_SIZE 16

typedef enum { 
    TLB_LRU, TLB_RANDOM 
} tlb_policy_t;

typedef enum { 
    PAGE_FIFO, PAGE_LRU, PAGE_RANDOM 
} page_policy_t;


//Core data structures 

typedef struct {
    int page;
    int frame;
    int valid;
    unsigned long last_used;
} tlb_entry_t;

typedef struct {
    int frame;
    int valid;
    unsigned long last_used;
} page_table_entry_t;

typedef struct {
    int page;
    int valid;
    unsigned long loaded_time;
    unsigned long last_used;
} frame_entry_t;

typedef struct {
    unsigned long accesses;
    unsigned long tlb_hits;
    unsigned long page_faults;
    unsigned long replacements;
} stats_t;


// BACKING STORE
// The backing store simulates disk storage for virtual pages
// Pages are read from BACKING_STORE.bin when a page fault occurs


static FILE *bs_fp = NULL;

//opens the binary file for reading
int bs_open(const char *filename) {
    bs_fp = fopen(filename, "rb");
    if (!bs_fp) {
        perror("Error opening backing store");
        return -1;
    }
    return 0;
}

// reads exactly one 256‑byte page into a buffer
int bs_read_page(int page_number, unsigned char *buffer) {
    if (!bs_fp) { 
        return -1;
    }

    long offset = (long)page_number * PAGE_SIZE;
    if (fseek(bs_fp, offset, SEEK_SET) != 0) {
        perror("fseek failed");
        return -1;
    }

    size_t n = fread(buffer, 1, PAGE_SIZE, bs_fp);
    if (n != PAGE_SIZE) {
        fprintf(stderr, "Error: could not read full page %d\n", page_number);
        return -1;
    }
    return 0;
}

// closes the file when the VM shuts down
void bs_close(void) {
    if (bs_fp) {
        fclose(bs_fp);
        bs_fp = NULL;
    }
}


// LOGGER 
// Writes a CSV file containing one row per translated address

typedef struct {
    FILE *fp;
} logger_t;

int logger_open(logger_t *log, const char *filename) {
    log->fp = fopen(filename, "w");
    return log->fp ? 0 : -1;
}

//writes the CSV column names
void logger_write_header(logger_t *log) {
    fprintf(log->fp, "logical_address,page,offset,tlb_hit,page_fault,frame,physical_address,value,replaced_page,replaced_frame\n");
}

//logs all details of each translation
void logger_write_row(logger_t *log, unsigned int logical, int page, int offset, int tlb_hit, int page_fault, 
                      int frame, int physical, signed char value, int replaced_page, int replaced_frame) {

    fprintf(log->fp, "%u,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", logical, page, offset, tlb_hit, page_fault,
            frame, physical, value, replaced_page, replaced_frame);
}

void logger_close(logger_t *log) {
    if (log->fp) {
        fclose(log->fp);
    } 
}


// PHYSICAL MEMORY 
//Represents the simulated RAM of the system

typedef struct {
    int num_frames;
    unsigned char *data;
    frame_entry_t *frames;
    unsigned long time_counter;
} phys_mem_t;

//allocates memory and initializes frames
int mem_init(phys_mem_t *mem, int num_frames) {
    mem->num_frames = num_frames;
    mem->time_counter = 0;

    mem->data = malloc(num_frames * FRAME_SIZE);
    if (!mem->data) {
        return -1;
    }

    mem->frames = malloc(num_frames * sizeof(frame_entry_t));
    if (!mem->frames) {
        free(mem->data);
        return -1;
    }

    for (int i = 0; i < num_frames; i++) {
        mem->frames[i].page = -1;
        mem->frames[i].valid = 0;
        mem->frames[i].loaded_time = 0;
        mem->frames[i].last_used = 0;
    }
    return 0;
}

void mem_destroy(phys_mem_t *mem) {
    free(mem->data);
    free(mem->frames);
}

//returns first unused frame or -1 if all are occupied
int mem_get_free_frame(phys_mem_t *mem) {
    for (int i = 0; i < mem->num_frames; i++)
        if (!mem->frames[i].valid){
            return i;
        }     
    return -1;
}

//copies a 256‑byte page into a frame and updates frame metadata
void mem_store_page(phys_mem_t *mem, int frame, int page, const unsigned char *page_data) {
    memcpy(mem->data + frame * FRAME_SIZE, page_data, FRAME_SIZE);
    mem->frames[frame].valid = 1;
    mem->frames[frame].page = page;
    mem->frames[frame].loaded_time = ++mem->time_counter;
    mem->frames[frame].last_used = mem->time_counter;
}

//updates last_used timestamp for LRU 
void mem_touch_frame(phys_mem_t *mem, int frame) {
    mem->frames[frame].last_used = ++mem->time_counter;
}


// PAGE TABLE 
// maps virtual pages to physical frames and tracks validity and usage for replacement policies

typedef struct {
    page_table_entry_t entries[NUM_PAGES];
    unsigned long time_counter;
} page_table_t;

// initializes all page table entries to invalid and resets time counter
void pt_init(page_table_t *pt) {
    pt->time_counter = 0;
    for (int i = 0; i < NUM_PAGES; i++) {
        pt->entries[i].frame = -1;
        pt->entries[i].valid = 0;
        pt->entries[i].last_used = 0;
    }
}

// returns the frame number for a page if valid, otherwise -1. Updates last_used for LRU policy
int pt_lookup(page_table_t *pt, int page) {
    if (!pt->entries[page].valid) {
        return -1;
    }
    pt->entries[page].last_used = ++pt->time_counter;
    return pt->entries[page].frame;
}

// sets the page table entry for a page to point to a frame and marks it valid. Updates last_used for LRU policy
void pt_set(page_table_t *pt, int page, int frame) {
    pt->entries[page].frame = frame;
    pt->entries[page].valid = 1;
    pt->entries[page].last_used = ++pt->time_counter;
}

// invalidates a page table entry, marking it as not valid and resetting frame and last_used
void pt_invalidate(page_table_t *pt, int page) {
    pt->entries[page].valid = 0;
    pt->entries[page].frame = -1;
    pt->entries[page].last_used = 0;
}


// TLB

typedef struct {
    tlb_entry_t entries[TLB_SIZE];
    tlb_policy_t policy;
    unsigned long time_counter;
} tlb_t;

// finds an index for a new TLB entry using the selected replacement policy. If there is an invalid entry, 
// it returns that index. Otherwise, it applies the policy to find a victim.
int tlb_find_victim(tlb_t *tlb) {
    for (int i = 0; i < TLB_SIZE; i++)
        if (!tlb->entries[i].valid) {
            return i;
        }

    if (tlb->policy == TLB_RANDOM) {
        return rand() % TLB_SIZE;
    }

    unsigned long best = tlb->entries[0].last_used;
    int idx = 0;

    for (int i = 1; i < TLB_SIZE; i++) {
        if (tlb->entries[i].last_used < best) {
            best = tlb->entries[i].last_used;
            idx = i;
        }
    }
    return idx;
}

// initializes the TLB entries to invalid and sets the replacement policy and time counter
void tlb_init(tlb_t *tlb, tlb_policy_t policy) {
    tlb->policy = policy;
    tlb->time_counter = 0;
    for (int i = 0; i < TLB_SIZE; i++) {
        tlb->entries[i].page = -1;
        tlb->entries[i].frame = -1;
        tlb->entries[i].valid = 0;
        tlb->entries[i].last_used = 0;
    }
}

// looks up a page in the TLB. If found, it returns the frame and sets hit to 1. Otherwise, it returns -1 
// and sets hit to 0. Updates last_used for LRU policy.
int tlb_lookup(tlb_t *tlb, int page, int *hit) {
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb->entries[i].valid && tlb->entries[i].page == page) {
            tlb->entries[i].last_used = ++tlb->time_counter;
            *hit = 1;
            return tlb->entries[i].frame;
        }
    }
    *hit = 0;
    return -1;
}

// inserts a page-frame mapping into the TLB, using the replacement policy to find a victim if necessary. 
// Marks the entry as valid and updates last_used for LRU policy.
void tlb_insert(tlb_t *tlb, int page, int frame) {
    int idx = tlb_find_victim(tlb);
    tlb->entries[idx].page = page;
    tlb->entries[idx].frame = frame;
    tlb->entries[idx].valid = 1;
    tlb->entries[idx].last_used = ++tlb->time_counter;
}

// invalidates any TLB entry that maps to the given page, marking it as not valid. 
void tlb_invalidate_page(tlb_t *tlb, int page) {
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb->entries[i].valid && tlb->entries[i].page == page) {
            tlb->entries[i].valid = 0;
        }
    }
}


// PAGE REPLACEMENT
// Chooses which frame to evict when memory is full
// FIFO, LRU, and Random

// selects a victim frame based on the selected page replacement policy. If there is a free frame, it returns that.
int fifo_victim(phys_mem_t *mem) {
    unsigned long best = ULONG_MAX;
    int idx = 0;
    for (int i = 0; i < mem->num_frames; i++) {
        if (mem->frames[i].loaded_time < best) {
            best = mem->frames[i].loaded_time;
            idx = i;
        }
    }
    return idx;
}

// selects the least recently used frame by comparing the last_used timestamps of all valid frames 
// and returning the index of the one with the smallest value.
int lru_victim(phys_mem_t *mem) {
    unsigned long best = ULONG_MAX;
    int idx = 0;
    for (int i = 0; i < mem->num_frames; i++) {
        if (mem->frames[i].last_used < best) {
            best = mem->frames[i].last_used;
            idx = i;
        }
    }
    return idx;
}

// selects a random frame by generating a random index within the range of available frames
int random_victim(phys_mem_t *mem) {
    return rand() % mem->num_frames;
}

// first checks for a free frame. If none are available, it uses the selected replacement policy to choose 
// a victim frame
int select_victim_frame(phys_mem_t *mem, page_table_t *pt, page_policy_t policy, int *victim_page) {
    int free = mem_get_free_frame(mem);
    if (free != -1) {
        *victim_page = -1;
        return free;
    }

    int frame;
    switch (policy) {
        case PAGE_FIFO:
            frame = fifo_victim(mem); break;
        case PAGE_LRU:    
            frame = lru_victim(mem); break;
        case PAGE_RANDOM: 
            frame = random_victim(mem); break;
        default:          
            frame = fifo_victim(mem);
    }

    *victim_page = mem->frames[frame].page;
    mem->frames[frame].valid = 0;
    return frame;
}


// ARGUMENT PARSING

typedef struct {
    char *input_file;
    char *output_file;
    tlb_policy_t tlb_policy;
    page_policy_t page_policy;
    int frames;
} args_t;

// prints usage information for the program
void print_usage(char *prog) {
    printf("Usage: %s <addresses.txt> [options]\n", prog);
}

// parses command-line arguments to configure the VM
args_t parse_args(int argc, char *argv[]) {
    args_t cfg = {0};
    cfg.tlb_policy = TLB_LRU;
    cfg.page_policy = PAGE_FIFO;
    cfg.frames = 256;

    if (argc < 2) {
        print_usage(argv[0]);
        exit(1);
    }

    cfg.input_file = argv[1];

    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-tlb")) {
            if (!strcmp(argv[i+1], "lru")){
                cfg.tlb_policy = TLB_LRU;
            } else if (!strcmp(argv[i+1], "random")){
                cfg.tlb_policy = TLB_RANDOM;
            } else { 
                fprintf(stderr, "Bad TLB policy\n"); exit(1); 
            }
            i++; 
            continue;
        }
        if (!strcmp(argv[i], "-page")) {
            if (!strcmp(argv[i+1], "fifo")){
                cfg.page_policy = PAGE_FIFO;
            } else if (!strcmp(argv[i+1], "lru")) {
                cfg.page_policy = PAGE_LRU;
            } else if (!strcmp(argv[i+1], "random")) {
                cfg.page_policy = PAGE_RANDOM;
            } else { 
                fprintf(stderr, "Bad page policy\n"); exit(1); 
            }
            i++;
            continue;
        }
        if (!strcmp(argv[i], "-frames")) {
            cfg.frames = atoi(argv[i+1]);
            i++; 
            continue;
        }
        if (!strcmp(argv[i], "-o")) {
            cfg.output_file = argv[i+1];
            i++; 
            continue;
        }
    }
    return cfg;
}


// VM CORE 

typedef struct {
    tlb_t tlb;
    page_table_t pt;
    phys_mem_t mem;
    tlb_policy_t tlb_policy;
    page_policy_t page_policy;
    stats_t stats;
} vm_t;

// extracts the page number from a 16-bit logical address by shifting right 8 bits and masking with 0xFF
int extract_page(unsigned int addr) { 
    return (addr >> 8) & 0xFF; 
}

// extracts the offset from a 16-bit logical address by masking with 0xFF to get the lower 8 bits
int extract_offset(unsigned int addr) {
    return addr & 0xFF; 
}

int vm_init(vm_t *vm, int frames, tlb_policy_t tlb_pol, page_policy_t page_pol) {
    vm->tlb_policy = tlb_pol;
    vm->page_policy = page_pol;

    vm->stats.accesses = 0;
    vm->stats.page_faults = 0;
    vm->stats.tlb_hits = 0;
    vm->stats.replacements = 0;

    tlb_init(&vm->tlb, tlb_pol);
    pt_init(&vm->pt);

    if (mem_init(&vm->mem, frames) != 0) {
        return -1;
    }
    if (bs_open("BACKING_STORE.bin") != 0) {
        return -1;
    }

    return 0;
}

void vm_destroy(vm_t *vm) {
    mem_destroy(&vm->mem);
    bs_close();
}

// translates a logical address to a physical address, handling TLB lookups, page table lookups, 
// page faults, and updates to the TLB and page table as needed. It also updates statistics for accesses, 
// TLB hits, page faults, and replacements.
int vm_translate(vm_t *vm, unsigned int logical, int *physical, signed char *value, int *tlb_hit, int *page_fault, int *replaced_page, int *replaced_frame) {
    vm->stats.accesses++;

    int page = extract_page(logical);
    int offset = extract_offset(logical);

    *replaced_page = -1;
    *replaced_frame = -1;

    int hit = 0;
    int frame = tlb_lookup(&vm->tlb, page, &hit);
    *tlb_hit = hit;

    if (hit) {
        vm->stats.tlb_hits++;
    } else {
        frame = pt_lookup(&vm->pt, page);
    }

    if (frame == -1) {
        vm->stats.page_faults++;
        *page_fault = 1;

        unsigned char buffer[PAGE_SIZE];
        bs_read_page(page, buffer);

        int victim_page;
        frame = select_victim_frame(&vm->mem, &vm->pt, vm->page_policy, &victim_page);

        if (victim_page != -1) {
            vm->stats.replacements++;
            *replaced_page = victim_page;
            *replaced_frame = frame;
            pt_invalidate(&vm->pt, victim_page);
            tlb_invalidate_page(&vm->tlb, victim_page);
        }

        mem_store_page(&vm->mem, frame, page, buffer);
        pt_set(&vm->pt, page, frame);
        tlb_insert(&vm->tlb, page, frame);

    } else {
        *page_fault = 0;
        if (!hit) {
            tlb_insert(&vm->tlb, page, frame);
        }
    }

    mem_touch_frame(&vm->mem, frame);

    *physical = frame * FRAME_SIZE + offset;
    *value = vm->mem.data[*physical];

    return 0;
}


// MAIN PROGRAM

int main(int argc, char *argv[]) {

    args_t args = parse_args(argc, argv);

    FILE *fp = fopen(args.input_file, "r");

    if (!fp) {
        perror("Error opening addresses file");
        return 1;
    }

    vm_t vm;

    if (vm_init(&vm, args.frames, args.tlb_policy, args.page_policy) != 0) {
        fprintf(stderr, "VM initialization failed\n");
        fclose(fp);
        return 1;
    }

    logger_t log;
    const char *csv = args.output_file ? args.output_file : "results.csv";

    if (logger_open(&log, csv) != 0) {
        fprintf(stderr, "Could not open CSV file\n");
        fclose(fp);
        vm_destroy(&vm);
        return 1;
    }

    logger_write_header(&log);

    unsigned int logical;
    while (fscanf(fp, "%u", &logical) == 1) {

        int physical, tlb_hit, page_fault;
        int replaced_page, replaced_frame;
        signed char value;

        if (vm_translate(&vm, logical, &physical, &value, &tlb_hit, &page_fault, &replaced_page, &replaced_frame) != 0) {
            fprintf(stderr, "Translation error\n");
            break;
        }

        int page = (logical >> 8) & 0xFF;
        int offset = logical & 0xFF;
        int frame = physical / 256;

        logger_write_row(&log, logical, page, offset, tlb_hit, page_fault, frame, physical, value, replaced_page, replaced_frame);
    }

    fclose(fp);
    logger_close(&log);

    printf("Number of Translated Addresses = %lu\n", vm.stats.accesses);
    printf("Page Faults = %lu\n", vm.stats.page_faults);
    printf("Page Fault Rate = %.3f\n", (double)vm.stats.page_faults / vm.stats.accesses);
    printf("TLB Hits = %lu\n", vm.stats.tlb_hits);
    printf("TLB Hit Rate = %.3f\n", (double)vm.stats.tlb_hits / vm.stats.accesses);
    printf("Replacements = %lu\n", vm.stats.replacements);

    vm_destroy(&vm);
    return 0;
}
