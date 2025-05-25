# Array sum test for RISC-V pipelined simulator
# This program computes the sum of 100 elements in an array
# Each core computes the sum of 25 elements

.data
array:  .word 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
        .word 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40
        .word 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60
        .word 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80
        .word 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100
result: .word 0

.text
main:
    # Initialize registers
    addi x1, x0, 0      # x1 = sum
    addi x2, x0, 0      # x2 = i (loop counter)
    la x3, array        # x3 = base address of array
    
    # Determine which part of the array to process based on core ID (x31)
    addi x4, x0, 25     # x4 = 25 (elements per core)
    mul x5, x31, x4     # x5 = coreId * 25 (starting index)
    add x6, x5, x4      # x6 = coreId * 25 + 25 (ending index)
    
    # Calculate starting address for this core
    slt x7, x0, x5      # x7 = (0 < x5) ? 1 : 0
    mul x8, x5, x0      # x8 = x5 * 4 (byte offset)
    add x3, x3, x8      # x3 = array + offset
    
    # Initialize loop counter for this core
    add x2, x0, x5      # i = starting index

loop:
    # Check loop condition
    slt x7, x2, x6      # x7 = (i < ending index) ? 1 : 0
    bne x7, x0, continue # if x7 != 0, continue loop
    jal x0, done        # else, exit loop

continue:
    # Calculate array index
    slt x7, x0, x2      # x7 = (0 < i) ? 1 : 0
    mul x8, x2, x0      # x8 = i * 4 (byte offset)
    add x9, x3, x8      # x9 = array + i*4
    
    # Load array element
    lw x10, 0(x9)       # x10 = array[i]
    
    # Add to sum
    add x1, x1, x10     # sum += array[i]
    
    # Increment loop counter
    addi x2, x2, 1      # i++
    jal x0, loop        # jump back to loop

done:
    # Store partial sum in a core-specific location
    la x11, result      # x11 = address of result
    sw x1, 0(x11)       # store sum at result
    
    # Only core 0 will combine all partial sums
    bne x31, x0, exit   # if coreId != 0, exit
    
    # Core 0 combines all partial sums
    addi x12, x0, 0     # x12 = total sum
    addi x13, x0, 0     # x13 = core counter
    addi x14, x0, 4     # x14 = number of cores

combine_loop:
    # Check if we've processed all cores
    slt x15, x13, x14   # x15 = (core_counter < num_cores) ? 1 : 0
    bne x15, x0, continue_combine # if x15 != 0, continue loop
    jal x0, print_result # else, print result

continue_combine:
    # Calculate address of partial sum for this core
    la x11, result      # x11 = base address of result
    slt x15, x0, x13    # x15 = (0 < core_counter) ? 1 : 0
    mul x16, x13, x0    # x16 = core_counter * 4
    add x11, x11, x16   # x11 = result + core_counter*4
    
    # Load partial sum
    lw x17, 0(x11)      # x17 = partial sum from core
    
    # Add to total sum
    add x12, x12, x17   # total_sum += partial_sum
    
    # Increment core counter
    addi x13, x13, 1    # core_counter++
    jal x0, combine_loop # jump back to loop

print_result:
    # Store final result
    la x11, result      # x11 = address of result
    sw x12, 0(x11)      # store total sum at result
    
exit:
    # End of program
    halt