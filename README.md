# Virtual Memory Management

**Name:** Luke Wiljanen

All csv files uploaded in this submission were the results of 128 frames, and random workloads (addresses.txt)

# Workload Experiments

256 Frames:

# Random
          Page-Fault Rate |  TLB Hit Rate |  Replacements
FIFO:   |       0.244          0.055              0
LRU:    |       0.244          0.055              0 
RANDOM: |       0.244          0.055              0 

# Sequential

          Page-Fault Rate |  TLB Hit Rate |  Replacements
FIFO:   |       0.004            0.996            0
LRU:    |       0.004            0.996            0
RANDOM: |       0.004            0.996            0         

# Looping

          Page-Fault Rate |  TLB Hit Rate |  Replacements
FIFO:   |       0                 1               0
LRU:    |       0                 1               0  
RANDOM: |       0                 1               0   

# Stride

          Page-Fault Rate |  TLB Hit Rate |  Replacements
FIFO:   |        1                0               0
LRU:    |        1                0               0     
RANDOM: |        1                0               0  


128 Frames:

# Random
          Page-Fault Rate |  TLB Hit Rate |  Replacements
FIFO:   |      0.538            0.055            410
LRU:    |      0.539            0.055            411
RANDOM: |      0.541            0.057            413

# Sequential

          Page-Fault Rate |  TLB Hit Rate |  Replacements
FIFO:   |       0.004           0.996             128                     
LRU:    |       0.004           0.996             128         
RANDOM: |       0.004           0.996             128          

# Looping

          Page-Fault Rate |  TLB Hit Rate |  Replacements
FIFO:   |        0                 1              0
LRU:    |        0                 1              0 
RANDOM: |        0                 1              0   

# Stride

          Page-Fault Rate |  TLB Hit Rate |  Replacements
FIFO:   |       1                  0              128
LRU:    |       1                  0              128      
RANDOM: |       1                  0              128   


# Performance Analysis


    Under high locality, all three policies perform equally well since for all policies, the page-fault rate's
    are 0, TBL hit rates are 1, and all have 0 replacements. 


    In this experiment LRU performs nearly identical to FIFO. LRU should outperform FIFO when there is high temporal
    locality, however in this experiment I believe that since the working set is bigger than memory about half of the
    pages are out of memory, and no matter which policy being used, pages are frequently being removed that may be 
    needed again, ultimately not drastically outperforming one or the other.


    Random performs similarly to other policies when locality is either very strong or very weak. It also is very 
    similar when memory is large enough, as seen when using 256 frames and there were zero replacements. 


    Reducing memory size significantly increases the page-fault rate as well as increases the number of replacements
    on low-locality workloads as seen when comparing 256 frames to 128 frames. Even when the page-fault rate is low
    in sequential, replacements still exist with a smaller memory size.



### Sources

- TLB https://www.geeksforgeeks.org/operating-systems/translation-lookaside-buffer-tlb-in-paging/
- fseek() https://www.tutorialspoint.com/c_standard_library/c_function_fseek.htm
- Zybooks
