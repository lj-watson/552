
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
  if(instr_queue_size != 0) return instr_queue[ifq_head_idx];
  else return NULL;
}

static instruction_t* ifq_tail()
{
  if(instr_queue_size != 0) 
  {
    return instr_queue[(ifq_tail_idx + INSTR_QUEUE_SIZE - 1) % INSTR_QUEUE_SIZE];
  }
  else return NULL;
}

/* MAP TABLE */
static void update_q_from_map_table(instruction_t *instr)
{
  for(int i = 0; i < 3; i++)
  {
    int reg = instr->r_in[i];
    if(reg != DNA && reg < MD_TOTAL_REGS && map_table[reg] != NULL)
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
    if(reg != DNA && reg < MD_TOTAL_REGS)
    {
      map_table[reg] = instr;
    }
  }
}

/* RESERVATION STATIONS */
static bool reservINT_insert(instruction_t* instr)
{
  // is a station available
  for(int i = 0; i < RESERV_INT_SIZE; i++)
  {
    if(reservINT[i] == NULL)
    {
      update_q_from_map_table(instr);
      update_map_table(instr);
      reservINT[i] = instr;
      return true;
    }
  }

  return false;
}

static bool reservFP_insert(instruction_t* instr)
{
  // is a station available
  for(int i = 0; i < RESERV_FP_SIZE; i++)
  {
    if(reservFP[i] == NULL)
    {
      update_q_from_map_table(instr);
      update_map_table(instr);
      reservFP[i] = instr;
      return true;
    }
  }

  return false;
}

static bool is_execute_complete(instruction_t* instr, int current_cycle) {
  if (IS_ICOMP(instr->op) || IS_LOAD(instr->op) || IS_STORE(instr->op)) {
    return (current_cycle - instr->tom_execute_cycle) >= FU_INT_LATENCY;
  } else if (IS_FCOMP(instr->op)) {
    return (current_cycle - instr->tom_execute_cycle) >= FU_FP_LATENCY;
  }
  else
  {
    printf("Received instruction that does not execute INT or FP");
    return false;
  }
}

static void clear_map_table_entry(instruction_t* instr)
{
  // clear map table entry
  for(int i = 0; i < 2; i++)
  {
    int reg = instr->r_out[i];
    if(reg != DNA && reg < MD_TOTAL_REGS && (map_table[reg] == instr))
    {
      map_table[reg] = NULL;
    }
  }
}

static void free_rs_and_fu(instruction_t* instr)
{
  if (USES_INT_FU(instr->op)) {
    for (int i = 0; i < RESERV_INT_SIZE; i++) {
      if (reservINT[i] == instr) {
        reservINT[i] = NULL;
      }
    }
    for (int i = 0; i < FU_INT_SIZE; i++) {
      if (fuINT[i] == instr) {
        fuINT[i] = NULL;
      }
    }
  }

  
  if (USES_FP_FU(instr->op)) {
    for (int i = 0; i < RESERV_FP_SIZE; i++) {
      if (reservFP[i] == instr) {
        reservFP[i] = NULL;
      }
    }
    for (int i = 0; i < FU_FP_SIZE; i++) {
      if (fuFP[i] == instr) {
        fuFP[i] = NULL;
      }
    }
  }
}
/* ECE552 Assignment 3 - END CODE */

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

  // If # of fetched instructions = total # instruction:
  if(fetch_index < sim_insn) return false;
  
  // all pipelines empty
  if(instr_queue_size != 0) return false;

  for(int i = 0; i < RESERV_INT_SIZE; i++)
  {
    if(reservINT[i] != NULL)
    {
      return false;
    }
  }

  for(int i = 0; i < RESERV_FP_SIZE; i++)
  {
    if(reservFP[i] != NULL)
    {
      return false;
    }
  }

  for(int i = 0; i < FU_INT_SIZE; i++)
  {
    if(fuINT[i] != NULL)
    {
      return false;
    }
  }

  for(int i = 0; i < FU_FP_SIZE; i++)
  {
    if(fuFP[i] != NULL)
    {
      return false;
    }
  }

  if(commonDataBus != NULL) return false;

  for(int i = 0; i < MD_TOTAL_REGS; i++)
  {
    if(map_table[i] != NULL) return false;
  }

  return true;
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
  if(commonDataBus != NULL)
  {
    clear_map_table_entry(commonDataBus);
    commonDataBus = NULL;
  }
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
  // An instruction broadcasts its results via the
  // Common Data Bus (enters the CDB stage) the cycle after it completes execution
  // only oldest completed instruction can broadcast to cdb
  instruction_t *oldest_completed_instr = NULL;
  for (int i = 0; i < FU_INT_SIZE; i++) {
    instruction_t* instr = fuINT[i];
    if (instr == NULL || !is_execute_complete(instr, current_cycle)) {
      continue;
    }

    if(WRITES_CDB(instr->op))
    {
      if(oldest_completed_instr == NULL || oldest_completed_instr->index > instr->index)
      {
        oldest_completed_instr = instr;
      }
    }
    else
    {
      free_rs_and_fu(instr);
    }
  }

  for (int i = 0; i < FU_FP_SIZE; i++) {
    instruction_t* instr = fuFP[i];
    if (instr == NULL || !is_execute_complete(instr, current_cycle)) {
      continue;
    }

    if(WRITES_CDB(instr->op))
    {
      if(oldest_completed_instr == NULL || oldest_completed_instr->index > instr->index)
      {
        oldest_completed_instr = instr;
      }
    }
    else
    {
      free_rs_and_fu(instr);
    }
  }

  if(commonDataBus == NULL && oldest_completed_instr != NULL)
  {
    oldest_completed_instr->tom_cdb_cycle = current_cycle;
    commonDataBus = oldest_completed_instr;
    free_rs_and_fu(oldest_completed_instr);
  }
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

  // INT Instructions:
  for (int i = 0; i < FU_INT_SIZE; i++) {

    // function unit is busy
    if (fuINT[i] != NULL) {
      continue;
    }
    // Need to prioritize the oldest instruction (based on index):
    instruction_t* oldestInstr = NULL;
    for (int j = 0; j < RESERV_INT_SIZE; j++) {
      instruction_t* instr = reservINT[j];

      // RS has an entry, it has been issued, and has not already been executed
      if (instr == NULL || instr->tom_issue_cycle == 0 || instr->tom_execute_cycle != 0) {
        continue;
      }
      // An instruction spends at least 1 cycle in this stage:
      if (instr->tom_issue_cycle == current_cycle) {
        continue;
      }

      // Check RAW hazards:
      bool hasRAWHazard = false;
      for (int k = 0; k < 3; k++) {
        if (instr->Q[k] != NULL && (instr->Q[k]->tom_cdb_cycle == 0 || instr->Q[k]->tom_cdb_cycle >= current_cycle)) {
              hasRAWHazard = true;
        }
      }

      if (!hasRAWHazard) {
        if (oldestInstr == NULL || oldestInstr->index > instr->index) {
          oldestInstr = instr;
        }
      }
    }

    if (oldestInstr != NULL) {
      oldestInstr->tom_execute_cycle = current_cycle;
      fuINT[i] = oldestInstr;
    }
  }

  // FP Instructions:
  for (int i = 0; i < FU_FP_SIZE; i++) {
    if (fuFP[i] != NULL) {
      continue;
    }
    instruction_t* oldestInstr = NULL;
    for (int j = 0; j < RESERV_FP_SIZE; j++) {
      instruction_t* instr = reservFP[j];

      if (instr == NULL || instr->tom_issue_cycle == 0 || instr->tom_execute_cycle != 0) {
        continue;
      }
      // An instruction spends at least 1 cycle in this stage:
      if (instr->tom_issue_cycle == current_cycle) {
        continue;
      }

      // Check RAW hazards:
      bool hasRAWHazard = false;
      for (int k = 0; k < 3; k++) {
        if (instr->Q[k] != NULL && (instr->Q[k]->tom_cdb_cycle == 0 || instr->Q[k]->tom_cdb_cycle >= current_cycle)) {
              hasRAWHazard = true;
        }
      }

      if (!hasRAWHazard) {
        if (oldestInstr == NULL || oldestInstr->index > instr->index) {
          oldestInstr = instr;
        }
      }
    }

    if (oldestInstr != NULL) {
      oldestInstr->tom_execute_cycle = current_cycle;
      fuFP[i] = oldestInstr;
    }
  }
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
  instruction_t* instr = ifq_head();
  if(instr == NULL) return;

  // control instructions do not use subsequent stages
  if(IS_COND_CTRL(instr->op) || IS_UNCOND_CTRL(instr->op))
  {
    // remove instr from dispatch queue
    ifq_pop();
    return;
  }

  // dispatch instruction if reservation station is available
  if(USES_INT_FU(instr->op))
  {
    if(reservINT_insert(instr))
    {
      instr->tom_issue_cycle = current_cycle;
      ifq_pop();
    }
  }
  else if(USES_FP_FU(instr->op))
  {
    if(reservFP_insert(instr))
    {
      instr->tom_issue_cycle = current_cycle;
      ifq_pop();
    }
  }
  else
  {
    printf("This instruction is none of the above, removing from ifq\n");
    ifq_pop();
  }
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
      // "the first instruction of your trace should be skipped"
      if(next_instr != NULL && !IS_TRAP(next_instr->op) && next_instr->index > 0)
      {
        // set all initial cycle paramaters
        next_instr->tom_dispatch_cycle = 0;
        next_instr->tom_issue_cycle = 0;
        next_instr->tom_execute_cycle = 0;
        next_instr->tom_cdb_cycle = 0;
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

  // instr that was just fetched
  instruction_t* instr = ifq_tail();

  if(instr->tom_dispatch_cycle == 0)
  {
    instr->tom_dispatch_cycle = current_cycle;
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
