#ifndef SEEDFINDER_CRACK_SIMD_H_
#define SEEDFINDER_CRACK_SIMD_H_
#include <stdint.h>

/* ponytail: uma macro-shim de ~12 ops cobre AVX2 (8 lanes) e WASM SIMD128
 * (4 lanes). Sem wrapper de tipos, sem header novo por ISA, sem lib. As
 * funcoes que usam CV_* sao compiladas uma unica vez (lane-generic). */

#if defined(__wasm_simd128__)
  #include <wasm_simd128.h>
  #define CRACK_SIMD 1
  #define CRACK_LANES 4
  typedef v128_t crack_vec;
  #define CV_SPLAT(x)    wasm_i32x4_splat((int32_t)(x))
  #define CV_ADD(a, b)   wasm_i32x4_add((a), (b))
  #define CV_MUL(a, b)   wasm_i32x4_mul((a), (b))
  #define CV_XOR(a, b)   wasm_v128_xor((a), (b))
  #define CV_OR(a, b)    wasm_v128_or((a), (b))
  #define CV_AND(a, b)   wasm_v128_and((a), (b))
  #define CV_SUB(a, b)   wasm_i32x4_sub((a), (b))
  #define CV_SHR(a, n)   wasm_u32x4_shr((a), (n))
  #define CV_SHL(a, n)   wasm_i32x4_shl((a), (n))
  #define CV_SEEDS(p)    wasm_i32x4_make((int32_t)(p)[0], (int32_t)(p)[1], \
                                         (int32_t)(p)[2], (int32_t)(p)[3])
  #define CV_STORE(p, v) wasm_v128_store((p), (v))
#elif (defined(__x86_64__) || defined(__i386__)) && defined(__GNUC__)
  #include <immintrin.h>
  #define CRACK_SIMD 1
  #define CRACK_LANES 8
  typedef __m256i crack_vec;
  #define CV_SPLAT(x)    _mm256_set1_epi32((int)(x))
  #define CV_ADD(a, b)   _mm256_add_epi32((a), (b))
  #define CV_MUL(a, b)   _mm256_mullo_epi32((a), (b))
  #define CV_XOR(a, b)   _mm256_xor_si256((a), (b))
  #define CV_OR(a, b)    _mm256_or_si256((a), (b))
  #define CV_AND(a, b)   _mm256_and_si256((a), (b))
  #define CV_SUB(a, b)   _mm256_sub_epi32((a), (b))
  #define CV_SHR(a, n)   _mm256_srli_epi32((a), (n))
  #define CV_SHL(a, n)   _mm256_slli_epi32((a), (n))
  #define CV_SEEDS(p)    _mm256_setr_epi32((int)(p)[0], (int)(p)[1], (int)(p)[2], \
                                           (int)(p)[3], (int)(p)[4], (int)(p)[5], \
                                           (int)(p)[6], (int)(p)[7])
  #define CV_STORE(p, v) _mm256_storeu_si256((__m256i *)(p), (v))
#else
  #define CRACK_SIMD 0
  #define CRACK_LANES 1
#endif

/* Grupos de vetores independentes processados no mesmo laco do init MT.
 * CRACK_GROUPS=1 nao custa nada; subir esconde a latencia de i32x4.mul
 * (ver Task 4 — so mudar depois de medir). */
#ifndef CRACK_GROUPS
  #define CRACK_GROUPS 1
#endif
#define CRACK_WIDTH (CRACK_LANES * CRACK_GROUPS)

#endif /* SEEDFINDER_CRACK_SIMD_H_ */
