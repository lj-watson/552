#include "predictor.h"

/***********************************************************
* 
* 1. 2 Bit Saturating Counter
* 
***********************************************************/

#define NUM_ENTRIES 1024
// each entry has 4*2 bit counters = 1 byte
// total = 1024*4*2 = 8192 bits of storage
#define COUNTERS_PER_BYTE 4
#define NUM_COUNTERS (NUM_ENTRIES * COUNTERS_PER_BYTE)

/*
* Prediction Table Values:
*   0 - Strongly Not Taken
*   1 - Weak Not Taken
*   2 - Weak Taken
*   3 - Strong Taken
*/
uint8_t prediction_table[NUM_ENTRIES];

uint8_t get_2bit_prediction(uint32_t indx) {
  uint32_t byte_indx = indx / COUNTERS_PER_BYTE;
  uint32_t offset = (indx % COUNTERS_PER_BYTE) * 2; //each counter is 2bits
  return (prediction_table[byte_indx] >> offset) & 0x03; // return 2 bit counter
}

void set_2bit_counter(uint32_t indx, uint8_t counter_val) {
  uint32_t byte_indx = indx / COUNTERS_PER_BYTE;
  uint32_t offset = (indx % COUNTERS_PER_BYTE) * 2; //each counter is 2bits
  prediction_table[byte_indx] &= ~(0x03 << offset); // clear old counter
  prediction_table[byte_indx] |= (counter_val << offset); // set new counter
}

void InitPredictor_2bitsat() {
  // Instantiate all elements of prediction table
  // to weak not taken:
  for (int i = 0; i < NUM_ENTRIES; i++) {
    prediction_table[i] = 0x55; //01010101
  }
}

bool GetPrediction_2bitsat(UINT32 PC) {
  // Get last 10 bits of PC to index:
  int index = PC & (NUM_COUNTERS - 1);
  uint8_t prediction = get_2bit_prediction(index);

  // If 0/1: Predict NOT_TAKEN
  // If 2/3: Predict TAKEN
  return prediction <= 1 ? NOT_TAKEN : TAKEN;
}

void UpdatePredictor_2bitsat(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) {
  // Get last 10 bits of PC to index:
  int index = PC & (NUM_COUNTERS - 1);
  uint8_t prediction = get_2bit_prediction(index);

  // If branch was taken, increment:
  if (resolveDir && prediction < 3) {
    prediction++;
  // If branch was not taken, decrement:
  } else if (!resolveDir && prediction > 0) {
    prediction--;
  }

  set_2bit_counter(index, prediction);
}

/***********************************************************
* 
* 2. 2 Level PAp Predictor
* 
***********************************************************/
/*
* Since PHT has 8 tables
* Need to index last 3 PC bits
*/
#define PHT_INDEX_BITS 0x007

/*
* Since BHT has 512 entries (2^9)
* Need to index 9 PC bits
*/
#define BHT_INDEX_BITS 0x1FF

/*
* 6 history bits per entry
*/
int BHT[512] = {0};

/*
* Total Table Size = 64*8 = 512
* Indexed by [Last 3 PC bits][6 history bits]
* PHT Counter Values:
*   0 - Strongly Not Taken
*   1 - Weak Not Taken
*   2 - Weak Taken
*   3 - Strong Taken
*/
int PHT[8][64];

void InitPredictor_2level() {
  // Instantiate all elements of PHT
  // to weak not taken:
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 64; j++) {
      PHT[i][j] = 1;
    }
  }
}

bool GetPrediction_2level(UINT32 PC) {
  int PHT_index = PC & PHT_INDEX_BITS;
  int BHT_index = (PC >> 3) & BHT_INDEX_BITS;
  
  int history = BHT[BHT_index];
  int prediction = PHT[PHT_index][history];

  // If 0/1: Predict NOT_TAKEN
  // If 2/3: Predict TAKEN
  return prediction <= 1 ? NOT_TAKEN : TAKEN;
}

void UpdatePredictor_2level(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) {
  int PHT_index = PC & PHT_INDEX_BITS;
  int BHT_index = (PC >> 3) & BHT_INDEX_BITS;
  

  int history = BHT[BHT_index];
  int prediction = PHT[PHT_index][history];

  // If branch was taken, increment:
  if (resolveDir && prediction < 3) {
    PHT[PHT_index][history]++;
  // If branch was not taken, decrement:
  } else if (!resolveDir && prediction > 0) {
    PHT[PHT_index][history]--;
  }

  // Update history bits:
  // Shift least recent bit out, add if taken (1/0) 
  // and only include the 6 bits of history
  BHT[BHT_index] = ((history << 1) + resolveDir) & 0x03F;
}

/***********************************************************
* 
* 3. Open-Ended Predictor
*     Perceptron Branch Predictor
*
*     Size = number of perceptrons * history bits * weight size + history bits + bias bits
*          = 256 * 62 * 8 + 62 + 256 = 127294 bits
***********************************************************/

// branch history is the input x to the perceptron function

#define NUM_PERCEPTRONS 256 // Number of Perceptron weights
#define HISTORY_BITS 62 // Number of history bits
#define THETA (1.93 * HISTORY_BITS + 14) // Confidence Threshold for Training

// To prevent oversaturated decisions, set MAX and MIN:
#define WEIGHT_MAX 127
#define WEIGHT_MIN -127

int8_t perceptron_weights[NUM_PERCEPTRONS][HISTORY_BITS];
int8_t bias_weights[NUM_PERCEPTRONS];
int8_t GHR[HISTORY_BITS]; // Global History Register stores -1/1

int idx;
int result;

void InitPredictor_openend() {
  for (int i = 0; i < NUM_PERCEPTRONS; i ++) {
    bias_weights[i] = 0;
    for (int j = 0; j < HISTORY_BITS; j++) {
      perceptron_weights[i][j] = 0;
    }
  }

  for(int i = 0; i < HISTORY_BITS; i++)
  {
    GHR[i] = -1;
  }
}

bool GetPrediction_openend(UINT32 PC) {

  // use ghr to xor with pc to reduce aliasing
  uint32_t ghr_hash = 0;
  for(int i = 0; i < 32 && i < HISTORY_BITS; i++)
  {
    if(GHR[i] == 1)
    {
      ghr_hash |= (1 << i);
    }
  }
  
  idx = (PC^ghr_hash) & (NUM_PERCEPTRONS - 1);

  // perceptron function
  // y = w_0 + sum_i (x_i + w_i)
  // where w is weight and x is branch history
  // prediction which is -1 or 1

  // w_0
  int prediction = bias_weights[idx];

  for (int i = 0; i < HISTORY_BITS; i++) {
    prediction += perceptron_weights[idx][i] * GHR[i];
  }

  result = prediction;

  // prediction is based on sign
  return prediction >= 0 ? TAKEN : NOT_TAKEN;
}

void UpdatePredictor_openend(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) {

  int target = resolveDir ? 1 : -1;
  
  // if sign(y) != t || |y| <= theta
  // w_i = w_i + t*x_i

  if (predDir != resolveDir || abs(result) <= THETA) {

    // update w_0 (there is no x_0)
    if(target == 1 && bias_weights[idx] < WEIGHT_MAX)
    {
      bias_weights[idx]++;
    }
    else if(target == -1 && bias_weights[idx] > WEIGHT_MIN)
    {
      bias_weights[idx]--;
    }

    for(int i = 0; i < HISTORY_BITS; i++)
    {
      if(target == GHR[i] && perceptron_weights[idx][i] < WEIGHT_MAX)
      {
        perceptron_weights[idx][i]++;
      }
      else if(target != GHR[i] && perceptron_weights[idx][i] > WEIGHT_MIN)
      {
        perceptron_weights[idx][i]--;
      }
    }
  }

  // Shift history and save most recent history:
  for (int i = HISTORY_BITS - 1; i > 0; i--) {
    GHR[i] = GHR[i - 1];
  }
  GHR[0] = target;
}
