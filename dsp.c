#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "dsp.h"
#include "tables.h"

static void transpose_block(float *in_data, float *out_data)
{
  int i, j;

  for (i = 0; i < 8; ++i)
  {
    for (j = 0; j < 8; ++j)
    {
      out_data[i*8+j] = in_data[j*8+i];
    }
  }
}

static void dct_1d(float *in_data, float *out_data)
{

  // varriables
  float32x4_t in, in2;          // in, in2 is varribles used to load in_Data
  float32x4_t r, r2;            // r, r2 is used as temporary varriables to store results.
  
  // load data
  in = vld1q_f32 (in_data);     // load first 4 elements from in_data
  in2 = vld1q_f32 (in_data +4); // load last 4 elements from in_data
  
  /*  unrolled loop with neon */
  // 0
  // load lookup table into neon varriables
  float32x4_t lookup = vld1q_f32 (dctlookup_T+0);     // dctlookup_T is a transposed version of dctlookup
  float32x4_t lookup2 = vld1q_f32 ((dctlookup_T[0])+4);
  
  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[0] = vaddvq_f32(r);  // add vector wide sum to out_data
  
  // 1
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup_T+1);     // dctlookup_T is a transposed version of dctlookup
  lookup2 = vld1q_f32 ((dctlookup_T[1])+4);
  
  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[1] = vaddvq_f32(r);  // add vector wide sum to out_data
  
  // 2
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup_T+2);     // dctlookup_T is a transposed version of dctlookup
  lookup2 = vld1q_f32 ((dctlookup_T[2])+4);
  
  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[2] = vaddvq_f32(r);  // add vector wide sum to out_data
  
  // 3
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup_T+3);     // dctlookup_T is a transposed version of dctlookup
  lookup2 = vld1q_f32 ((dctlookup_T[3])+4);
  
  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[3] = vaddvq_f32(r);  // add vector wide sum to out_data
  
  // 4
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup_T+4);     // dctlookup_T is a transposed version of dctlookup
  lookup2 = vld1q_f32 ((dctlookup_T[4])+4);
  
  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[4] = vaddvq_f32(r);  // add vector wide sum to out_data
  
  // 5
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup_T+5);     // dctlookup_T is a transposed version of dctlookup
  lookup2 = vld1q_f32 ((dctlookup_T[5])+4);
  
  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[5] = vaddvq_f32(r);  // add vector wide sum to out_data
  
  // 6
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup_T+6);     // dctlookup_T is a transposed version of dctlookup
  lookup2 = vld1q_f32 ((dctlookup_T[6])+4);
  
  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[6] = vaddvq_f32(r);  // add vector wide sum to out_data
  
  // 7
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup_T+7);     // dctlookup_T is a transposed version of dctlookup
  lookup2 = vld1q_f32 ((dctlookup_T[7])+4);
  
  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[7] = vaddvq_f32(r);  // add vector wide sum to out_data
  
}

static void idct_1d(float *in_data, float *out_data)
{

  
  // varriables
  float32x4_t in, in2;          // in, in2 is varribles used to load in_Data
  float32x4_t r, r2;            // r, r2 is used as temporary varriables to store results.
    
  // load data
  in = vld1q_f32 (in_data);     // load first 4 elements from in_data
  in2 = vld1q_f32 (in_data +4); // load last 4 elements from in_data
  
  
  /* unrolled loop with neon  */
  // 0
  // load lookup table into neon varriables
  float32x4_t lookup = vld1q_f32 (dctlookup+0);
  float32x4_t lookup2 = vld1q_f32 ((dctlookup[0])+4);

  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[0] = vaddvq_f32(r);  // add vector wide sum to out_data
  
  // 1
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup+1);
  lookup2 = vld1q_f32 ((dctlookup[1])+4);

  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[1] = vaddvq_f32(r);
  
  // 2
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup+2);
  lookup2 = vld1q_f32 ((dctlookup[2])+4);

  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[2] = vaddvq_f32(r);
  
  // 3
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup+3);
  lookup2 = vld1q_f32 ((dctlookup[3])+4);

  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[3] = vaddvq_f32(r);
  
  // 4
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup+4);
  lookup2 = vld1q_f32 ((dctlookup[4])+4);

  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[4] = vaddvq_f32(r);
  
  // 5
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup+5);
  lookup2 = vld1q_f32 ((dctlookup[5])+4);

  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[5] = vaddvq_f32(r);
  
  // 6
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup+6);
  lookup2 = vld1q_f32 ((dctlookup[6])+4);

  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[6] = vaddvq_f32(r);
  
  // 7
  // load lookup table into neon varriables
  lookup = vld1q_f32 (dctlookup+7);
  lookup2 = vld1q_f32 ((dctlookup[7])+4);

  r = vmulq_f32(in, lookup);    // multiply first 4 elements
  r2 = vmulq_f32(in2, lookup2); // multiply last 4 elements
  r = vaddq_f32(r, r2);         // add up the 4 elements from r and r2
  out_data[7] = vaddvq_f32(r);

}

static void scale_block(float *in_data, float *out_data)
{
  int u, v;

  for (v = 0; v < 8; ++v)
  {
    for (u = 0; u < 8; ++u)
    {
      float a1 = !u ? ISQRT2 : 1.0f;
      float a2 = !v ? ISQRT2 : 1.0f;

      /* Scale according to normalizing function */
      out_data[v*8+u] = in_data[v*8+u] * a1 * a2;
    }
  }
}

static void quantize_block(float *in_data, float *out_data, uint8_t *quant_tbl)
{
  int zigzag;

  for (zigzag = 0; zigzag < 64; ++zigzag)
  {
    uint8_t u = zigzag_U[zigzag];
    uint8_t v = zigzag_V[zigzag];

    float dct = in_data[v*8+u];

    /* Zig-zag and quantize */
    out_data[zigzag] = (float) round((dct / 4.0) / quant_tbl[zigzag]);
  }
}

static void dequantize_block(float *in_data, float *out_data,
    uint8_t *quant_tbl)
{
  int zigzag;

  for (zigzag = 0; zigzag < 64; ++zigzag)
  {
    uint8_t u = zigzag_U[zigzag];
    uint8_t v = zigzag_V[zigzag];

    float dct = in_data[zigzag];

    /* Zig-zag and de-quantize */
    out_data[v*8+u] = (float) round((dct * quant_tbl[zigzag]) / 4.0);
  }
}

void dct_quant_block_8x8(int16_t *in_data, int16_t *out_data,
    uint8_t *quant_tbl)
{
  float mb[8*8] __attribute((aligned(16)));
  float mb2[8*8] __attribute((aligned(16)));

  int i, v;

  for (i = 0; i < 64; ++i) { mb2[i] = in_data[i]; }

  /* Two 1D DCT operations with transpose */
  for (v = 0; v < 8; ++v) { dct_1d(mb2+v*8, mb+v*8); }
  transpose_block(mb, mb2);
  for (v = 0; v < 8; ++v) { dct_1d(mb2+v*8, mb+v*8); }
  transpose_block(mb, mb2);

  scale_block(mb2, mb);
  quantize_block(mb, mb2, quant_tbl);

  for (i = 0; i < 64; ++i) { out_data[i] = mb2[i]; }
}

void dequant_idct_block_8x8(int16_t *in_data, int16_t *out_data,
    uint8_t *quant_tbl)
{
  float mb[8*8] __attribute((aligned(16)));
  float mb2[8*8] __attribute((aligned(16)));

  int i, v;

  for (i = 0; i < 64; ++i) { mb[i] = in_data[i]; }

  dequantize_block(mb, mb2, quant_tbl);
  scale_block(mb2, mb);

  /* Two 1D inverse DCT operations with transpose */
  for (v = 0; v < 8; ++v) { idct_1d(mb+v*8, mb2+v*8); }
  transpose_block(mb2, mb);
  for (v = 0; v < 8; ++v) { idct_1d(mb+v*8, mb2+v*8); }
  transpose_block(mb2, mb);

  for (i = 0; i < 64; ++i) { out_data[i] = mb[i]; }
}

void sad_block_8x8(uint8_t *block1, uint8_t *block2, int stride, int *result)
{
  

  // Neon Intrinsics varriables
  uint8x8_t b_1, b_2;           // variables hold 8 block1 and block 2 elements
  uint16x8_t sad, total_sad;    // variables calcualte sad and hold total sad.
  *result = 0; 

    /*  unrolled loop with Neon Intrinsics*/
    // 0
    b_1 = vld1_u8(block1 + 0*stride); // load 8 elems from block1
    b_2 = vld1_u8(block2 + 0*stride); // load 8 elems from block2
    
    sad = vabdl_u8(b_2, b_1);        // calculate abs difference long, uint8x8_t -> uint16x8_t
    total_sad = vaddq_u16(sad, total_sad);   // add to total sad amount
    
    // 1
    b_1 = vld1_u8(block1 + 1*stride); // load 8 elems from block1
    b_2 = vld1_u8(block2 + 1*stride); // load 8 elems from block2
    
    sad = vabdl_u8(b_2, b_1);        // calculate abs difference long, uint8x8_t -> uint16x8_t
    total_sad = vaddq_u16(sad, total_sad);   // add to total sad amount

    // 2
    b_1 = vld1_u8(block1 + 2*stride); // load 8 elems from block1
    b_2 = vld1_u8(block2 + 2*stride); // load 8 elems from block2
    
    sad = vabdl_u8(b_2, b_1);        // calculate abs difference long, uint8x8_t -> uint16x8_t
    total_sad = vaddq_u16(sad, total_sad);   // add to total sad amount
    
    // 3
    b_1 = vld1_u8(block1 + 3*stride); // load 8 elems from block1
    b_2 = vld1_u8(block2 + 3*stride); // load 8 elems from block2
    
    sad = vabdl_u8(b_2, b_1);        // calculate abs difference long, uint8x8_t -> uint16x8_t
    total_sad = vaddq_u16(sad, total_sad);   // add to total sad amount
    
    // 4
    b_1 = vld1_u8(block1 + 4*stride); // load 8 elems from block1
    b_2 = vld1_u8(block2 + 4*stride); // load 8 elems from block2
    
    sad = vabdl_u8(b_2, b_1);        // calculate abs difference long, uint8x8_t -> uint16x8_t
    total_sad = vaddq_u16(sad, total_sad);   // add to total sad amount
    
    // 5
    b_1 = vld1_u8(block1 + 5*stride); // load 8 elems from block1
    b_2 = vld1_u8(block2 + 5*stride); // load 8 elems from block2
    
    sad = vabdl_u8(b_2, b_1);        // calculate abs difference long, uint8x8_t -> uint16x8_t
    total_sad = vaddq_u16(sad, total_sad);   // add to total sad amount
  
    // 6
    b_1 = vld1_u8(block1 + 6*stride); // load 8 elems from block1
    b_2 = vld1_u8(block2 + 6*stride); // load 8 elems from block2
    
    sad = vabdl_u8(b_2, b_1);        // calculate abs difference long, uint8x8_t -> uint16x8_t
    total_sad = vaddq_u16(sad, total_sad);   // add to total sad amount
  
    // 7
    b_1 = vld1_u8(block1 + 7*stride); // load 8 elems from block1
    b_2 = vld1_u8(block2 + 7*stride); // load 8 elems from block2
    
    sad = vabdl_u8(b_2, b_1);        // calculate abs difference long, uint8x8_t -> uint16x8_t
    total_sad = vaddq_u16(sad, total_sad);
    *result += vaddvq_u16(total_sad);      // vector wide sum of total sad amount
}
