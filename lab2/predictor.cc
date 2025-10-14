#include "predictor.h"

/***********************************************************
* 
* 1. 2 Bit Saturating Counter
* 
***********************************************************/

/*
* Since Prediction Table is 2^10 = 1024 entries,
* need 10 bits to index into it
*/
#define NUM_ENTRIES 4096
// each entry has 4*2 bit counters = 1 byte
#define COUNTERS_PER_BYTE 4

/*
* Total Table Size = 8192/8 = 1024
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
  int index = PC & (NUM_ENTRIES - 1);
  uint8_t prediction = get_2bit_prediction(index);

  // If 0/1: Predict NOT_TAKEN
  // If 2/3: Predict TAKEN
  return prediction <= 1 ? NOT_TAKEN : TAKEN;
}

void UpdatePredictor_2bitsat(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) {
  // Get last 10 bits of PC to index:
  int index = PC & (NUM_ENTRIES - 1);
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
*     Size = 4 bytes / int * 60 * 512 = 122 880 bytes
*                4 * 60 + 4 * 512 + 4 =   2 292 bytes
*                               Total = 125 172 bytes
* 
***********************************************************/

#define MAX_TABLES 60 // Number of Perceptron Tables
#define MAX_WEIGHTS 512 // Number of Weights per Table
#define THETA (1.93 * MAX_TABLES + MAX_TABLES/2) // Confidence Threshold for Training

// To prevent oversaturated decisions, set MAX and MIN:
#define WEIGHT_MAX 127
#define WEIGHT_MIN -127

int perceptrons[MAX_TABLES][MAX_WEIGHTS];
unsigned int GHT[MAX_TABLES]; // Global History Table
int indices[MAX_WEIGHTS]; // Stores indices used in last prediction

int result = 0; // Most recent prediction confidence

void InitPredictor_openend() {
  for (int i = 0; i < MAX_TABLES; i ++) {
    for (int j = 0; j < MAX_WEIGHTS; j++) {
      // Initialize to a weak not taken state:
      perceptrons[i][j] = -1;
    }
    GHT[i] = 0;
  }
}

bool GetPrediction_openend(UINT32 PC) {
  int prediction;

  // Record most recent index:
  indices[0] = PC%MAX_WEIGHTS;
  prediction = perceptrons[0][indices[0]];

  for (int i = 1; i < MAX_TABLES; i++) {
    indices[i] = (GHT[i - 1] ^ PC) % MAX_WEIGHTS;
    prediction += perceptrons[i][indices[i]];
  }

  result = prediction;

  // If leans towards positive, T, otherwise, NT:
  return prediction >= 0 ? TAKEN : NOT_TAKEN;
}

void UpdatePredictor_openend(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) {
  
  // If prediction wrong or in training threshold:
  if (predDir != resolveDir || abs(result) <= THETA) {
    for (int i = 0; i < MAX_TABLES; i++) {
      // If TAKEN: Increment prediction towards TAKEN (positive)
      if (resolveDir) {
        perceptrons[i][indices[i]] = (perceptrons[i][indices[i]] < WEIGHT_MAX) ? perceptrons[i][indices[i]] + 1 : WEIGHT_MAX;
      // If NOT TAKEN: Decrement prediction towards NOT TAKEN (negative)
      } else {
        perceptrons[i][indices[i]] = (perceptrons[i][indices[i]] > WEIGHT_MIN) ? perceptrons[i][indices[i]] - 1 : WEIGHT_MIN;
      }
    }
  }

  // Shift history and save most recent history:
  for (int i = MAX_TABLES - 1; i > 0; i--) {
    GHT[i] = GHT[i - 1];
  }
  GHT[0] = resolveDir;
}

