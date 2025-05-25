# Strided array addition without using scratchpad memory
# Based on Listing 1 from the assignment

.data
array: .word 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
       .word 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40
       .word 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60
       .word 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80
       .word 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100
       # Repeat to ensure size > L1D cache
       .word 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
       .word 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40
       .word 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60
       .word 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80
       .word 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100
partial_sums: .word 0, 0, 0, 0  # One slot per compute unit

.text
main:
    # X value = L1D cache size / size of word = 400 / 4 = 100
    addi x2, x0, 100     # X value (stride)
    la x3, array         # Load array address
    la x4, partial_sums  # Load partial sums array address
    
    # Initialize partial sum for this core in register
    addi x10, x0, 0      # partial_sum = 0
    
    # Initialize outer loop counter
    addi x5, x0, 0       # outer_count = 0
    addi x6, x0, 100     # outer_limit = 100

outer_loop:
    bge x5, x6, outer_done  # If outer_count >= 100, exit loop
    
    # Initialize inner loop counter
    addi x7, x0, 0       # inner_count = 0
    addi x8, x0, 100     # inner_limit = 100
    la x3, array         # Reset array address for each iteration
    
inner_loop:
    bge x7, x8, inner_done  # If inner_count >= 100, exit loop
    
    # Calculate address = array + (inner_count * X * 4)
    mul x9, x7, x2       # inner_count * X
    slli x9, x9, 2       # (inner_count * X) * 4
    add x11, x3, x9      # array + (inner_count * X * 4)
    
    # Load element and add to partial sum
    lw x12, 0(x11)       # Load array[inner_count * X]
    add x10, x10, x12    # partial_sum += array[inner_count * X]
    
    # Increment inner loop counter
    addi x7, x7, 1       # inner_count++
    j inner_loop         # Repeat inner loop
    
inner_done:
    # Increment outer loop counter
    addi x5, x5, 1       # outer_count++
    j outer_loop         # Repeat outer loop

outer_done:
    # Store partial sum
    slli x13, x31, 2     # CID * 4 (word size)
    add x14, x4, x13     # partial_sums + CID * 4
    sw x10, 0(x14)       # Store partial sum

    # Synchronize all cores
    sync

    # Only core 1 computes final sum
    addi x15, x0, 1      # x15 = 1
    bne x31, x15, exit   # If CID != 1, exit

    # Core 1 computes final sum
    la x4, partial_sums  # Reload partial_sums address
    lw x10, 0(x4)        # Load partial sum from core 0
    lw x15, 4(x4)        # Load partial sum from core 1
    lw x16, 8(x4)        # Load partial sum from core 2
    lw x17, 12(x4)       # Load partial sum from core 3
    
    # Compute final sum
    add x10, x10, x15    # x10 += x15
    add x10, x10, x16    # x10 += x16
    add x10, x10, x17    # x10 += x17
    
    # Store final result
    sw x10, 0(x0)        # Store at address 0

exit:
    halt