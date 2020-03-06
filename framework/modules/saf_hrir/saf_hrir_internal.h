/*
 * Copyright 2016-2018 Leo McCormack
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * @file saf_hrir_internal.h
 * @brief Internal part of the "saf_hrir" module
 *
 * A collection of head-related impulse-response (HRIR) functions. Including
 * estimation of the interaural time differences (ITDs), conversion of HRIRs to
 * HRTF filterbank coefficients, and HRTF interpolation utilising amplitude-
 * normalised VBAP gains.
 *
 * @author Leo McCormack
 * @date 12.12.2016
 */

#ifndef __HRIR_INTERNAL_H_INCLUDED__
#define __HRIR_INTERNAL_H_INCLUDED__

#include <stdio.h>
#include <math.h> 
#include <string.h>
#include "saf_hrir.h"
#include "../../resources/afSTFT/afSTFTlib.h"
#include "../saf_utilities/saf_utilities.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef NUM_EARS
# define NUM_EARS 2
#endif

/* ========================================================================== */
/*                             Internal Functions                             */
/* ========================================================================== */

/**
 * Converts and FIR filter into Filterbank Coefficients
 *
 * @note This is currently hard coded for a 128 hop size with hybrid mode
 *       enabled (see afSTFTlib.h).
 *
 * @param[in]  hIR     Time-domain FIR; FLAT: N_dirs x nCH x ir_len
 * @param[in]  N_dirs  Number of FIR sets
 * @param[in]  nCH     Number of channels per FIR set
 * @param[in]  ir_len  Length of the FIR
 * @param[in]  N_bands Number of time-frequency domain bands
 * @param[out] hFB     The FIRs as Filterbank coefficients;
 *                     FLAT: N_bands x nCH x N_dirs
 */
void FIRtoFilterbankCoeffs(/* Input Arguments */
                           float* hIR,
                           int N_dirs,
                           int nCH,
                           int ir_len,
                           int N_bands,
                           /* Output Arguments */
                           float_complex* hFB);


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __HRIR_INTERNAL_H_INCLUDED__ */
