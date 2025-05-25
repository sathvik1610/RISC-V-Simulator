# Test program: Array sum with 4 compute units
# Each compute unit computes sum of a portion of the array
# Then they synchronize and compute the final sum

.data
array: .word 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
       .word 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40
       .word 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60
       .word 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80
       .word 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100
partial_sums: .word 0, 0, 0, 0  # One slot per compute unit

.text
main:
    addi x1, x0, 0       # Initialize counter
    addi x2, x0, 25      # Elements per core (array size / 4)
    la x3, array         # Load array address
    la x4, partial_sums  # Load partial sums array address
    
    # Compute start and end indices for this core
    # Start = CID * elements_per_core
    # End = Start + elements_per_core
    addi x5, x31, 0      # Copy CID to x5
    mul x6, x5, x2       # start = CID * elements_per_core
    add x7, x6, x2       # end = start + elements_per_core
    
    # Calculate starting address for this core
    # address = array + start * 4
    slli x6, x6, 2       # start * 4 (word size)
    add x3, x3, x6       # array + start * 4
    
    # Initialize partial sum for this core
    addi x10, x0, 0      # partial_sum = 0

loop:
    bge x1, x2, done     # If counter >= elements_per_core, exit loop
    lw x8, 0(x3)         # Load array element
    add x10, x10, x8     # Add to partial sum
    addi x3, x3, 4       # Increment address
    addi x1, x1, 1       # Increment counter
    j loop               # Repeat loop

done:
    # Store partial sum
    slli x5, x31, 2      # CID * 4 (word size)
    add x4, x4, x5       # partial_sums + CID * 4
    sw x10, 0(x4)        # Store partial sum

    # Synchronize all cores
    sync

    # Only core 1 computes final sum
    addi x11, x0, 1      # x11 = 1
    bne x31, x11, exit   # If CID != 1, exit

    # Core 1 computes final sum
    la x4, partial_sums  # Reload partial_sums address
    lw x10, 0(x4)        # Load partial sum from core 0
    lw x11, 4(x4)        # Load partial sum from core 1
    lw x12, 8(x4)        # Load partial sum from core 2
    lw x13, 12(x4)       # Load partial sum from core 3
    
    # Compute final sum
    add x10, x10, x11    # x10 += x11
    add x10, x10, x12    # x10 += x12
    add x10, x10, x13    # x10 += x13
    
    # Store final result
    sw x10, 0(x0)        # Store at address 0

exit:
    halt