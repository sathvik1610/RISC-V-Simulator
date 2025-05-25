# COA_PROJECT
## Timeline and Development Milestones

*   **Date:20-Feb-2025**  
  **Members: Pranathi, Sathvik**
    - Tried for making UI
    - Pushed Final code to github
*  **Date:19-Feb-2025**  
 **Members: Pranathi, Sathvik**
    - Checked the working of simulator by running different codes
    - Finalized the project code and documentation.

*  **Date:18-Feb-2025**  
 **Members: Pranathi, Sathvik**
    - Developed processing for .word and .text sections.
    - Implemented and tested the bubble sort algorithm(with .word and .text) on multiple cores
    - Implemented program counter (PC) and clock cycle tracking.  
    - Removed debug statements and refined final code.

*  **Date:17-Feb-2025**  
 **Members: Pranathi, Sathvik**
      - Implemented and tested the bubble sort algorithm(without .word and .text) on multiple cores.  
      - Debugged initial errors using extensive debug statements.  
      - Achieved a working bubble sort simulation.

*  **Date:16-Feb-2025**  
 **Members: Pranathi, Sathvik**
      - Realized the need for shared memory instead of isolated memories.  
      - Implemented a shared memory module and integrated lockstep execution.  
      - Developed barrier synchronization to coordinate cores.

*  **Date:15-Feb-2025**  
 **Members: Pranathi, Sathvik**
 
    - Implemented Branch Instructions
    - Created four cores and set up the simulator.  
    - Assigned each core a separate 1KB memory segment.  
    - Tested instruction execution across cores.

*  **Date:13-Feb-2025**  
 **Members: Pranathi, Sathvik**

      - Implemented arithmetic instructions (`ADD`, `ADDI`, etc.) and memory instructions.  
      - Continued working on label resolution.

*  **Date:12-Feb-2025**  
 **Members: Pranathi, Sathvik**
      - Developed instruction decode.  
      - Worked on label collection and categorization of instructions.  
      - Determined the design for separate instruction handler functions.

*  **Date:11-Feb-2025**  
 **Members: Pranathi, Sathvik**
      - Focused on taking assembly code as input.  
      - Implemented loading assembly code as a string (`loadProgram`) and from a file (`loadProgramFromFile`).  
      - Tested basic execution.

*  **Date:10-Feb-2025**  
 **Members: Pranathi, Sathvik**
      - Read project guidelines and studied existing simulators (e.g., Ripes).  
      - Decided on implementation details and work distribution.



## Team Contributions

**Mandadi Pranathi:**
- Implemented instructions: `ADD`, `LW`, `LA`, `BNE`, `ADDI`.
- Handled file input from a string.
- Implemented instruction decode logic.
- Worked on core setup and shared memory management.
- Implemented lockstep execution using threads and barrier synchronization.
- Developed processing for `.word` and `.text` sections.
- Bubble sort testing(With .word and .text sections)
- Handled label collection.

**Pilyanam Sathvik:**
- Implemented instructions: `SUB`, `SW`, `JAL`, `BLT`, `SLT`.
- Handled file input from a file.
- Implemented register fetching.
- Worked on core setup and shared memory management.
- Implemented lockstep execution using threads and barrier synchronization.
- bubble sort testing (without using the `.word` and `.text` formats).
- Implemented the `HALT` instruction.
- printing registers and memory.

---


## COA PROJECT Phase2
## Minutes of the Meeting

*   **Date:10-Mar-2025**  
  **Members: Pranathi, Sathvik**
    - Corrected functionalities and ran several test cases.
    - Tried for making UI
    - Pushed Final code to github

*  **Date:9-Mar-2025**  
 **Members: Pranathi, Sathvik**
    - Improved latencies for arithemetic instructions.
    - Corrected functionalities of previous functions.
    - Implemented and tested the bubble sort algorithm on multiple cores.
    - Removed debug statements and refined final code.
    - CSV file simulation for pipelining.
    - Tested for different test cases.

*  **Date:8-Mar-2025**  
 **Members: Pranathi, Sathvik**
      - Implemented latencies for arithemetic instructions. 
      - Improved Hazard detections. 
      - Achieved a working bubble sort simulation.

*  **Date:7-Mar-2025**  
 **Members: Pranathi, Sathvik**
      - Implemented without-data forwading module.
      - Improved and re-written functions.
      
*  **Date:6-Mar-2025**  
 **Members: Pranathi, Sathvik**  
      - Implemented data forwarding and stalls count.
      - Improved previous functions.
      - Implemented Hazard detections. 

*  **Date:5-Mar-2025**  
 **Members: Pranathi, Sathvik**
 
    - Implemented Writeback Stage.
    - Checked the pipeline implementation for some test cases.  
    - Corrected  and improved functionalities.
    - Tried to implement data forwarding.

*  **Date:3-Mar-2025**  
 **Members: Pranathi, Sathvik**

      - Implemented execute stage and Memoryaccess stage.  
      - Converted fetch stage to centralized fetch stage.
*  **Date:2-Mar-2025**  
 **Members: Pranathi, Sathvik**
      - Implemented how exactly the stages run in a clockcycle  
      - Worked on fetch stage.  
      - Worked on decode Stage.

*  **Date:28-Feb-2025**  
 **Members: Pranathi, Sathvik**
      - Implemented code to take user input whether to enable or disable forwading and the instruction latencies.  
      - Implemented how the instructions run core wise for each clock cycle.

*  **Date:27-Feb-2025**  
 **Members: Pranathi, Sathvik**
      - Read project guidelines and  Understood what exactly to implement  
      - Decided on implementation details and work distribution.
