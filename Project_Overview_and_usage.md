# RISC-V Multi-Core Simulator Project Explanation



## Overview

This project is a RISC-V simulator designed to emulate lockstep execution of RISC-V instructions across four cores. In lockstep execution, all cores execute the same instruction at the same time in a synchronized manner. The simulator features shared memory management, instruction fetch and decode, label collection, and supports a subset of RISC-V instructions. A bubble sort algorithm is implemented to test correct instruction processing and synchronization.



## Key Features

1. **Core Setup and Memory Management**
   - Four cores are created, each with 32 registers.
   - Register `x0` is hardwired to 0 and `x31` holds the core ID.
   - A shared memory system is implemented, with each core assigned a separate 1KB segment.
   - The simulator supports both the `.data` and `.text` sections:
     - The `.data` section uses the `.word` directive to reserve and initialize memory.
     - The `.text` section contains executable instructions and label definitions.

2. **Instruction Set Support**
   - Supported instructions include:
     - **Arithmetic:** `ADD`, `ADDI`, `SUB`, `MUL`, `SLT`
     - **Memory:** `LW`, `SW`
     - **Control Flow:** `JAL`, `BNE`, `BLT`, and the pseudo-instruction `LA` (load address)
     - **Special:** `HALT` (to stop the simulation)
   - Instruction fetch and decode is implemented by parsing each line of assembly code and dispatching to appropriate handler functions.
   - Label collection is performed during program load to support branch and jump instructions.

3. **File Input**
   - The simulator can load assembly code either from a string or from a file (e.g., `program.txt`).
   - Two functions are provided: one for reading a complete assembly string and another for reading from a file.

4. **Lockstep Execution**
   - All cores execute instructions in lockstep, meaning they all execute the same instruction at the same time in a coordinated fashion.
   - Barrier synchronization (using mutexes and condition variables) ensures that no core gets ahead of the others.
   - Each core maintains its own cycle count (the number of instruction cycles executed), and the simulator prints both per-core and global cycle counts.

5. **Testing**
   - A bubble sort algorithm is implemented to validate the simulator.
   - The simulator prints the final state of registers and memory after execution.
   - Debug statements were used during development and then removed for the final version.
   - The simulator can be further modified to display the state after every instruction if step-by-step debugging is desired.



## Integration and Final Output

The final simulator integrates all components to:
- Load assembly code with proper handling of `.data` and `.text` sections.
- Set up multiple cores with shared memory and synchronized lockstep execution.
- Execute a wide range of RISC-V instructions, including those needed for bubble sort.
- Track clock cycles and display final states of registers and memory.


## Usage Instructions
In VS-Code

**To compile**    g++ -std=c++17 main.cpp simulator.cpp core.cpp -o simulator -pthread
 
**To execute**   ./simulator
1. **Running the Simulator:**  
   Run the simulator; it will:
    - Asks for input filename.Input the file name which contains your assembly code.
   - Load the program.
   - Execute it in lockstep across 4 cores.
   - Print the final state, including:
     - Final PC and register contents for each core.
     - The number of clock cycles per core and the global clock cycle count.
     - A memory dump for each core’s allocated segment.

 An example of bubblesort code and factorial code was already included in the project.
 
## To be read before using  


- Data is accecpted in `decimal` and `hexadecimal` formats.
- Negative numbers can be entered.
- In `.data` if series of numbers are entered, seperate them by a `,` . Eg : array:  .word 9 ,5, 1, 10, 7, 100 
- `.data` , `.globl` , `.text` are accecpted.
- The memory and register values are shown in `Hexadecimal` format.
- **Lockstep execution** is used (threads are used).
- `sw`, `lw`, `jal`, `blt`, `halt`, `slt`,`addi`, `add`, `sub`, `mul`, `bne`, `la` instructions are supported



## Team Contributions

**Mandadi Pranathi:**
- Implemented instructions: `ADD`, `LW`, `LA`, `BNE`, `ADDI`.
- Handled file input from a string.
- Developed instruction decode logic.
- Worked on core setup and shared memory management.
- Implemented lockstep execution using threads and barrier synchronization.
- Developed processing for `.word` and `.text` sections.
- Implemented bubble sort testing.
- Handled label collection.

**Pilyanam Sathvik:**
- Implemented instructions: `SUB`, `SW`, `JAL`, `BLT`, `SLT`.
- Handled file input from a file.
- Implemented register fetching.
- Assisted with core setup and shared memory management.
- Worked on lockstep execution using threads and barrier synchronization.
- bubble sort testing (without using the `.word` and `.text` formats).
- Implemented the `HALT` instruction.
- printing registers and memory.

---
