
#include <limits.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "regs.h"
#include "memory.h"
#include "loader.h"
#include "syscall.h"
#include "dlite.h"
#include "options.h"
#include "stats.h"
#include "sim.h"
#include "decode.def"

#include "instr.h"

/* PARAMETERS OF THE TOMASULO'S ALGORITHM */
/* ECE552 Assignment 3 - BEGIN CODE */
#define INSTR_QUEUE_SIZE         16

#define RESERV_INT_SIZE    5
#define RESERV_FP_SIZE     3
#define FU_INT_SIZE        3
#define FU_FP_SIZE         1

#define FU_INT_LATENCY     5
#define FU_FP_LATENCY      7
/* ECE552 Assignment 3 - END CODE */
/* IDENTIFYING INSTRUCTIONS */

//unconditional branch, jump or call
#define IS_UNCOND_CTRL(op) (MD_OP_FLAGS(op) & F_CALL || \
                         MD_OP_FLAGS(op) & F_UNCOND)

//conditional branch instruction
#define IS_COND_CTRL(op) (MD_OP_FLAGS(op) & F_COND)

//floating-point computation
#define IS_FCOMP(op) (MD_OP_FLAGS(op) & F_FCOMP)

//integer computation
#define IS_ICOMP(op) (MD_OP_FLAGS(op) & F_ICOMP)

//load instruction
#define IS_LOAD(op)  (MD_OP_FLAGS(op) & F_LOAD)

//store instruction
#define IS_STORE(op) (MD_OP_FLAGS(op) & F_STORE)

//trap instruction
#define IS_TRAP(op) (MD_OP_FLAGS(op) & F_TRAP) 

#define USES_INT_FU(op) (IS_ICOMP(op) || IS_LOAD(op) || IS_STORE(op))
#define USES_FP_FU(op) (IS_FCOMP(op))

#define WRITES_CDB(op) (IS_ICOMP(op) || IS_LOAD(op) || IS_FCOMP(op))

/* FOR DEBUGGING */

//prints info about an instruction
#define PRINT_INST(out,instr,str,cycle)	\
  myfprintf(out, "%d: %s", cycle, str);		\
  md_print_insn(instr->inst, instr->pc, out); \
  myfprintf(stdout, "(%d)\n",instr->index);

#define PRINT_REG(out,reg,str,instr) \
  myfprintf(out, "reg#%d %s ", reg, str);	\
  md_print_insn(instr->inst, instr->pc, out); \
  myfprintf(stdout, "(%d)\n",instr->index);

/* VARIABLES */

//instruction queue for tomasulo
static instruction_t* instr_queue[INSTR_QUEUE_SIZE];
//number of instructions in the instruction queue
static int instr_queue_size = 0;

//reservation stations (each reservation station entry contains a pointer to an instruction)
static instruction_t* reservINT[RESERV_INT_SIZE];
static instruction_t* reservFP[RESERV_FP_SIZE];

//functional units
static instruction_t* fuINT[FU_INT_SIZE];
static instruction_t* fuFP[FU_FP_SIZE];

//common data bus
static instruction_t* commonDataBus = NULL;

//The map table keeps track of which instruction produces the value for each register
static instruction_t* map_table[MD_TOTAL_REGS];

//the index of the last instruction fetched
static int fetch_index = 0;

/* ECE552 Assignment 3 - BEGIN CODE */
// use a circular buffer for the ifq entries
static int ifq_head_idx = 0;
static int ifq_tail_idx = 0;

static void ifq_push(instruction_t* instr)
{
  // insert instr into tail of ifq
  instr_queue[ifq_tail_idx] = instr;
  // update new tail index (circular)
  ifq_tail_idx = (ifq_tail_idx + 1) % INSTR_QUEUE_SIZE;
  instr_queue_size++;
}

static void ifq_pop()
{
  // update ifq head index
  ifq_head_idx = (ifq_head_idx + 1) % INSTR_QUEUE_SIZE;
  instr_queue_size--;
}

static instruction_t* ifq_head()
{
  return instr_queue[ifq_head_idx];
}

/* MAP TABLE */
static void update_q_from_map_table(instruction_t *instr)
{
  for(int i = 0; i < 3; i++)
  {
    int reg = instr->r_in[i];
    if(reg != NULL && reg >= 0 && reg < MD_TOTAL_REGS && map_table[reg] != NULL)
    {
      instr->Q[i] = map_table[reg];
    }
    else
    {
      instr->Q[i] = NULL;
    }
  }
}

static void update_map_table(instruction_t *instr)
{
  for(int i = 0; i < 2; i++)
  {
    int reg = instr->r_out[i];
    if(reg != NULL && reg >= 0 && reg < MD_TOTAL_REGS)
    {
      map_table[reg] = instr;
    }
  }
}

/* RESERVATION STATIONS */
static bool reservINT_insert(instruction_t* instr, int current_cycle)
{
  // is a station available
  for(int i = 0; i < RESERV_INT_SIZE; i++)
  {
    if(reservINT[i] == NULL)
    {
      instr->tom_dispatch_cycle = current_cycle;
      update_q_from_map_table(instr);
      update_map_table(instr);
      reservINT[i] = instr;
      return true;
    }
  }

  return false;
}

static bool reservFP_insert(instruction_t* instr, int current_cycle)
{
  // is a station available
  for(int i = 0; i < RESERV_FP_SIZE; i++)
  {
    if(reservFP[i] == NULL)
    {
      instr->tom_dispatch_cycle = current_cycle;
      update_q_from_map_table(instr);
      update_map_table(instr);
      reservFP[i] = instr;
      return true;
    }
  }

  return false;
}
/* ECE552 Assignment 3 - END CODE */

/* FUNCTIONAL UNITS */


/* RESERVATION STATIONS */


/* 
 * Description: 
 * 	Checks if simulation is done by finishing the very last instruction
 *      Remember that simulation is done only if the entire pipeline is empty
 * Inputs:
 * 	sim_insn: the total number of instructions simulated
 * Returns:
 * 	True: if simulation is finished
 */
static bool is_simulation_done(counter_t sim_insn) {
  /* ECE552 Assignment 3 - BEGIN CODE */

  // If # of completed instructions = total # instruction, sim is done:
  return instr_queue_size == sim_insn;

  /* ECE552 Assignment 3 - END CODE */
}

/* 
 * Description: 
 * 	Retires the instruction from writing to the Common Data Bus
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void CDB_To_retire(int current_cycle) {

  /* ECE552 Assignment 3 - BEGIN CODE */
  /* ECE552 Assignment 3 - END CODE */

}


/* 
 * Description: 
 * 	Moves an instruction from the execution stage to common data bus (if possible)
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void execute_To_CDB(int current_cycle) {

  /* ECE552 Assignment 3 - BEGIN CODE */
  /* ECE552 Assignment 3 - END CODE */

}

/* 
 * Description: 
 * 	Moves instruction(s) from the issue to the execute stage (if possible). We prioritize old instructions
 *      (in program order) over new ones, if they both contend for the same functional unit.
 *      All RAW dependences need to have been resolved with stalls before an instruction enters execute.
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void issue_To_execute(int current_cycle) {

  /* ECE552 Assignment 3 - BEGIN CODE */
  /* ECE552 Assignment 3 - END CODE */
}

/* 
 * Description: 
 * 	Moves instruction(s) from the dispatch stage to the issue stage
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void dispatch_To_issue(int current_cycle) {

  /* ECE552 Assignment 3 - BEGIN CODE */
   for (int i = 0; i < INSTR_QUEUE_SIZE; i++) {
    // Get the next instruction from the queue:
    instruction_t* instr = instr_queue[i];
    if (instr == NULL) {
      continue;
    }

    // Check if reservation station is available:
    bool reservAvailable = false;
    // Note: Memory instructions use the integer FUs and RSs:
    if (IS_ICOMP(instr->op) || IS_LOAD(instr->op) || IS_STORE(instr->op)) {
      for (int j = 0; j < RESERV_INT_SIZE; j++) {
        if (reservINT[j] == NULL) {
          reservINT[j] = instr;
          reservAvailable = true;
          break;
        }
      }
    } else if (IS_FCOMP(instr->op)) {
      for (int j = 0; j < RESERV_FP_SIZE; j++) {
        if (reservFP[j] == NULL) {
          reservFP[j] = instr;
          reservAvailable = true;
          break;
        }
      }
    } else if (IS_COND_CTRL(instr->op) || IS_UNCOND_CTRL(instr->op)) {
      reservAvailable = true; // Does not require any RS
    }

    // Can complete dispatch if RS is available next cycle (Therefore, issue next cycle):
    if (reservAvailable) {
      instr->tom_issue_cycle = current_cycle + 1;
    }
  }

  /* ECE552 Assignment 3 - END CODE */
}

/* 
 * Description: 
 * 	Grabs an instruction from the instruction trace (if possible)
 * Inputs:
 *      trace: instruction trace with all the instructions executed
 * Returns:
 * 	None
 */
void fetch(instruction_trace_t* trace) {

  /* ECE552 Assignment 3 - BEGIN CODE */
  // Only fetch if there is room in the queue:
  if (instr_queue_size < INSTR_QUEUE_SIZE) { 
    instruction_t* next_instr = NULL;

    // Fetch next instr and skip all TRAP instructions:
    while(fetch_index < sim_num_insn)
    {
      next_instr = get_instr(trace, fetch_index);
      fetch_index++;
      if(next_instr != NULL && !IS_TRAP(next_instr->op))
      {
        ifq_push(next_instr);
        return;
      }
    }
  }
  /* ECE552 Assignment 3 - END CODE */
}

/* 
 * Description: 
 * 	Calls fetch and dispatches an instruction at the same cycle (if possible)
 * Inputs:
 *      trace: instruction trace with all the instructions executed
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void fetch_To_dispatch(instruction_trace_t* trace, int current_cycle) {

  /* ECE552 Assignment 3 - BEGIN CODE */
  // fetch next instruction and place into IFQ
  fetch(trace);

  // A fetched instruction can be dispatched in the same cycle,
  // check if the oldest instruction can be dispatched
  // if not, all younger instructions must stall

  if(instr_queue_size == 0)
  {
    return;
  }

  instruction_t* instr = ifq_head();

  // control instructions do not use subsequent stages
  if(IS_COND_CTRL(instr->op) || IS_UNCOND_CTRL(instr->op))
  {
    instr->tom_dispatch_cycle = current_cycle;
    // remove instr from dispatch queue
    ifq_pop();
    return;
  }

  // dispatch instruction if reservation station is available
  if(USES_INT_FU(instr->op))
  {
    if(reservINT_insert(instr, current_cycle))
    {
      ifq_pop();
    }
  }
  else if(USES_FP_FU(instr->op))
  {
    if(reservFP_insert(instr, current_cycle))
    {
      ifq_pop();
    }
  }
  else
  {
    die("how");
  }

  return;

  /* ECE552 Assignment 3 - END CODE */
}

/* 
 * Description: 
 * 	Performs a cycle-by-cycle simulation of the 4-stage pipeline
 * Inputs:
 *      trace: instruction trace with all the instructions executed
 * Returns:
 * 	The total number of cycles it takes to execute the instructions.
 * Extra Notes:
 * 	sim_num_insn: the number of instructions in the trace
 */
counter_t runTomasulo(instruction_trace_t* trace)
{
  //initialize instruction queue
  int i;
  for (i = 0; i < INSTR_QUEUE_SIZE; i++) {
    instr_queue[i] = NULL;
  }

  //initialize reservation stations
  for (i = 0; i < RESERV_INT_SIZE; i++) {
      reservINT[i] = NULL;
  }

  for(i = 0; i < RESERV_FP_SIZE; i++) {
      reservFP[i] = NULL;
  }

  //initialize functional units
  for (i = 0; i < FU_INT_SIZE; i++) {
    fuINT[i] = NULL;
  }

  for (i = 0; i < FU_FP_SIZE; i++) {
    fuFP[i] = NULL;
  }

  //initialize map_table to no producers
  int reg;
  for (reg = 0; reg < MD_TOTAL_REGS; reg++) {
    map_table[reg] = NULL;
  }
  
  int cycle = 1;
  while (true) {

    /* ECE552 Assignment 3 - BEGIN CODE */
    // do not chain stages within one cycle
    CDB_To_retire(cycle);
    execute_To_CDB(cycle);
    issue_To_execute(cycle);
    dispatch_To_issue(cycle);
    fetch_To_dispatch(trace, cycle);
    /* ECE552 Assignment 3 - END CODE */

    cycle++;

    if (is_simulation_done(sim_num_insn))
      break;
  }
  
  return cycle;
}
