/*
 * Copyright 2020 Leo McCormack
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
 * @file saf_reverb_internal.c
 * @ingroup Reverb
 * @brief Internal source for the reverb processing module (#SAF_REVERB_MODULE)
 *
 * A collection of reverb and room simulation algorithms.
 *
 * @author Leo McCormack
 * @date 06.05.2020
 */

#include "saf_reverb.h"
#include "saf_reverb_internal.h"

/* ========================================================================== */
/*                         IMS Shoebox Room Simulator                         */
/* ========================================================================== */

void ims_shoebox_echogramCreate
(
    void** phEcho
)
{
    *phEcho = malloc1d(sizeof(echogram_data));
    echogram_data *ec = (echogram_data*)(*phEcho);

    ec->numImageSources = 0;
    ec->nChannels = 0;
    ec->value = NULL;
    ec->time = NULL;
    ec->order = NULL;
    ec->coords = NULL;
    ec->sortedIdx = NULL;
}

void ims_shoebox_echogramResize
(
    void* hEcho,
    int numImageSources,
    int nChannels
)
{
    echogram_data *ec = (echogram_data*)(hEcho);

    if(ec->nChannels != nChannels ||  ec->numImageSources != numImageSources){
        ec->nChannels = nChannels;
        ec->numImageSources = numImageSources;
        ec->value = (float**)realloc2d((void**)ec->value, numImageSources, nChannels, sizeof(float));
        ec->time = realloc1d(ec->time, numImageSources*sizeof(float));
        ec->order = (int**)realloc2d((void**)ec->order, numImageSources, 3, sizeof(int));
        ec->coords = realloc1d(ec->coords, numImageSources * sizeof(ims_pos_xyz));
        ec->sortedIdx = realloc1d(ec->sortedIdx, numImageSources*sizeof(int));
    }
}

void ims_shoebox_echogramDestroy
(
    void** phEcho
)
{
    echogram_data *ec = (echogram_data*)(*phEcho);

    if(ec!=NULL){
        free(ec->value);
        free(ec->time);
        free(ec->order);
        free(ec->coords);
        free(ec->sortedIdx);
        free(ec);
        ec=NULL;
        *phEcho = NULL;
    }
}

void ims_shoebox_coreWorkspaceCreate
(
    void** phWork,
    int nBands
)
{
    ims_shoebox_coreWorkspaceDestroy(phWork);
    *phWork = malloc1d(sizeof(ims_core_workspace));
    ims_core_workspace *wrk = (ims_core_workspace*)(*phWork);
    int i, band;

    /* locals */
    wrk->d_max = 0.0f;
    wrk->lengthVec = 0;
    wrk->numImageSources = 0;
    memset(wrk->room, 0, 3*sizeof(int));
    for(i=0; i<3; i++){
        wrk->src.v[i] = -1; /* outside the room (forces reinit) */
        wrk->rec.v[i] = -1;
    }
    wrk->nBands = nBands;

    /* Internals */
    wrk->validIDs = NULL;
    wrk->II  = wrk->JJ    = wrk->KK  = NULL;
    wrk->s_x = wrk->s_y   = wrk->s_z = wrk->s_d = NULL;
    wrk->s_t = wrk->s_att = NULL;

    /* Echograms */
    wrk->refreshEchogramFLAG = 1;
    ims_shoebox_echogramCreate( &(wrk->hEchogram) );
    ims_shoebox_echogramCreate( &(wrk->hEchogram_rec) );
    wrk->hEchogram_abs = malloc1d(nBands*sizeof(voidPtr));
    for(band=0; band< nBands; band++)
        ims_shoebox_echogramCreate( &(wrk->hEchogram_abs[band]) );

    /* Room impulse responses */
    wrk->refreshRIRFLAG = 1;
    wrk->rir_len_samples = 0;
    wrk->rir_len_seconds = 0.0f;
    wrk->rir_bands = (float***)malloc1d(nBands*sizeof(float**));
    for(band=0; band < nBands; band++)
        wrk->rir_bands[band] = NULL;
}

void ims_shoebox_coreWorkspaceDestroy
(
    void** phWork
)
{
    ims_core_workspace *wrk = (ims_core_workspace*)(*phWork);
    int band;

    if(wrk!=NULL){
        /* free internals */
        free(wrk->validIDs);
        free(wrk->II);
        free(wrk->JJ);
        free(wrk->KK);
        free(wrk->s_x);
        free(wrk->s_y);
        free(wrk->s_z);
        free(wrk->s_d);
        free(wrk->s_t);
        free(wrk->s_att);

        /* Destroy echogram containers */
        ims_shoebox_echogramDestroy( &(wrk->hEchogram) );
        ims_shoebox_echogramDestroy( &(wrk->hEchogram_rec) );
        for(band=0; band< wrk->nBands; band++)
            ims_shoebox_echogramDestroy( &(wrk->hEchogram_abs[band]) );
        free(wrk->hEchogram_abs);

        /* free rirs */
        for(band=0; band < wrk->nBands; band++)
            free(wrk->rir_bands[band]);

        free(wrk);
        wrk=NULL;
        *phWork = NULL;
    }
}

void ims_shoebox_coreInit
(
    void* hWork,
    int room[3],
    ims_pos_xyz src,
    ims_pos_xyz rec,
    float maxTime_s,
    float c_ms
)
{
    ims_core_workspace *wrk = (ims_core_workspace*)(hWork);
    echogram_data *echogram = (echogram_data*)(wrk->hEchogram);
    ims_pos_xyz src_orig, rec_orig;
    int imsrc, vIdx;
    int ii, jj, kk;
    float d_max;

    d_max = maxTime_s*c_ms;

    /* move origin to the centre of the room */
    src_orig.x = src.x - (float)room[0]/2.0f;
    src_orig.y = (float)room[1]/2.0f - src.y;
    src_orig.z = src.z - (float)room[2]/2.0f;
    rec_orig.x = rec.x - (float)room[0]/2.0f;
    rec_orig.y = (float)room[1]/2.0f - rec.y;
    rec_orig.z = rec.z - (float)room[2]/2.0f;

    /* Update indices only if the maximum permitted delay or room dimensions have changed */
    if( (wrk->d_max != d_max) ||
        (wrk->room[0] != room[0]) || (wrk->room[1] != room[1]) || (wrk->room[2] != room[2]) )
    {
        wrk->d_max = d_max;
        memcpy(wrk->room, room, 3*sizeof(int));
        wrk->Nx = (int)(d_max/(float)room[0] + 1.0f); /* ceil */
        wrk->Ny = (int)(d_max/(float)room[1] + 1.0f); /* ceil */
        wrk->Nz = (int)(d_max/(float)room[2] + 1.0f); /* ceil */
        wrk->lengthVec = (2*(wrk->Nx)+1) * (2*(wrk->Ny)+1) * (2*(wrk->Nz)+1);

        /* i,j,k indices for calculation in x,y,z respectively */
        wrk->II = realloc1d(wrk->II, wrk->lengthVec*sizeof(float));
        wrk->JJ = realloc1d(wrk->JJ, wrk->lengthVec*sizeof(float));
        wrk->KK = realloc1d(wrk->KK, wrk->lengthVec*sizeof(float));
        ii = -(wrk->Nx); jj = -(wrk->Ny); kk = -(wrk->Nz);
        for(imsrc = 0; imsrc<wrk->lengthVec; imsrc++){
            wrk->II[imsrc] = (float)ii;
            wrk->JJ[imsrc] = (float)jj;
            wrk->KK[imsrc] = (float)kk;
            ii++;
            if(ii>wrk->Nx){
                ii = -(wrk->Nx);
                jj++;
            }
            if(jj>wrk->Ny){
                jj = -(wrk->Ny);
                kk++;
            }
            if(kk>wrk->Nz){
                kk = -(wrk->Nz);
            }
        }

        /* Re-allocate memory */
        wrk->validIDs = realloc1d(wrk->validIDs, wrk->lengthVec*sizeof(int));
        wrk->s_x = realloc1d(wrk->s_x, wrk->lengthVec*sizeof(float));
        wrk->s_y = realloc1d(wrk->s_y, wrk->lengthVec*sizeof(float));
        wrk->s_z = realloc1d(wrk->s_z, wrk->lengthVec*sizeof(float));
        wrk->s_d = realloc1d(wrk->s_d, wrk->lengthVec*sizeof(float));
        wrk->s_t = realloc1d(wrk->s_t, wrk->lengthVec*sizeof(float));
        wrk->s_att = realloc1d(wrk->s_att, wrk->lengthVec*sizeof(float));
    }

    /* Update echogram only if the source/receiver positions or room dimensions have changed */
    if( (wrk->rec.x != rec_orig.x) || (wrk->rec.y != rec_orig.y) || (wrk->rec.z != rec_orig.z) ||
        (wrk->src.x != src_orig.x) || (wrk->src.y != src_orig.y) || (wrk->src.z != src_orig.z) ||
        (wrk->room[0] != room[0]) || (wrk->room[1] != room[1]) || (wrk->room[2] != room[2]))
    {
        memcpy(wrk->room, room, 3*sizeof(int));
        memcpy(&(wrk->rec), &rec_orig, sizeof(ims_pos_xyz));
        memcpy(&(wrk->src), &src_orig, sizeof(ims_pos_xyz));

        /* image source coordinates with respect to receiver, and distance */
        for(imsrc = 0; imsrc<wrk->lengthVec; imsrc++){
            wrk->s_x[imsrc] = wrk->II[imsrc]*(float)room[0] + powf(-1.0f, wrk->II[imsrc])*src_orig.x - rec_orig.x;
            wrk->s_y[imsrc] = wrk->JJ[imsrc]*(float)room[1] + powf(-1.0f, wrk->JJ[imsrc])*src_orig.y - rec_orig.y;
            wrk->s_z[imsrc] = wrk->KK[imsrc]*(float)room[2] + powf(-1.0f, wrk->KK[imsrc])*src_orig.z - rec_orig.z;
            wrk->s_d[imsrc] = sqrtf(powf(wrk->s_x[imsrc], 2.0f) + powf(wrk->s_y[imsrc], 2.0f) + powf(wrk->s_z[imsrc], 2.0f));
        }

        /* Determine the indices where the distance is below the specified maximum */ 
        for(imsrc = 0, wrk->numImageSources = 0; imsrc<wrk->lengthVec; imsrc++){
            if(wrk->s_d[imsrc]<d_max){
                wrk->validIDs[imsrc] = 1;
                wrk->numImageSources++; /* (within maximum distance) */
            }
            else
                wrk->validIDs[imsrc] = 0;
        }

        /* Resize echogram container (only done if needed) */
        ims_shoebox_echogramResize(wrk->hEchogram, wrk->numImageSources, 1/*omni-pressure*/);

        /* Copy data into echogram struct */
        for(imsrc = 0, vIdx = 0; imsrc<wrk->lengthVec; imsrc++){
            if(wrk->validIDs[imsrc]){
                echogram->time[vIdx]     = wrk->s_d[imsrc]/c_ms;

                /* reflection propagation attenuation - if distance is <1m set
                 * attenuation to 1 to avoid amplification */
                echogram->value[vIdx][0]   = wrk->s_d[imsrc]<=1 ? 1.0f : 1.0f / wrk->s_d[imsrc];

                /* Order */
                echogram->order[vIdx][0] = (int)(wrk->II[imsrc] + 0.5f); /* round */
                echogram->order[vIdx][1] = (int)(wrk->JJ[imsrc] + 0.5f);
                echogram->order[vIdx][2] = (int)(wrk->KK[imsrc] + 0.5f);

                /* Coordinates */
                echogram->coords[vIdx].x = wrk->s_x[imsrc];
                echogram->coords[vIdx].y = wrk->s_y[imsrc];
                echogram->coords[vIdx].z = wrk->s_z[imsrc];
                vIdx++;
            }
        }

        /* Find indices to sort reflections according to propagation time (accending order) */
        sortf(echogram->time, NULL, echogram->sortedIdx, echogram->numImageSources, 0);
    }
}

void ims_shoebox_coreRecModuleSH
(
    void* hWork,
    int sh_order
)
{
    ims_core_workspace *wrk = (ims_core_workspace*)(hWork);
    echogram_data *echogram = (echogram_data*)(wrk->hEchogram);
    echogram_data *echogram_rec = (echogram_data*)(wrk->hEchogram_rec);
    int i, j, nSH;
    float aziElev_rad[2];
    float* sh_gains;

    nSH = ORDER2NSH(sh_order);

    /* Resize container (only done if needed) */
    ims_shoebox_echogramResize(wrk->hEchogram_rec, echogram->numImageSources, nSH);

    /* Copy 'time', 'coord', 'order', except in accending order w.r.t propogation time  */
    for(i=0; i<echogram_rec->numImageSources; i++){
        echogram_rec->time[i] = echogram->time[echogram->sortedIdx[i]];
        for(j=0; j<3; j++){
            echogram_rec->order[i][j] = echogram->order[echogram->sortedIdx[i]][j];
            echogram_rec->coords[i].v[j] = echogram->coords[echogram->sortedIdx[i]].v[j];
        }
        echogram_rec->sortedIdx[i] = i;
    }

    /* Copy 'value' (the core omni-pressure), except also in accending order w.r.t propogation time */
    if(sh_order==0){
        for(i=0; i<echogram_rec->numImageSources; i++)
            echogram_rec->value[i][0] = echogram->value[echogram->sortedIdx[i]][0];
    }
    /* Impose spherical harmonic directivities onto 'value', and store in accending order w.r.t propogation time */
    else{
        sh_gains = malloc1d(nSH*sizeof(float));
        for(i=0; i<echogram_rec->numImageSources; i++){
            /* Cartesian coordinates to spherical coordinates */
            unitCart2Sph(echogram_rec->coords[i].v, (float*)aziElev_rad);
            aziElev_rad[1] = SAF_PI/2.0f-aziElev_rad[1]; /* AziElev to AziInclination conversion */

            /* Apply spherical harmonic weights */
            getSHreal_recur(sh_order, (float*)aziElev_rad, 1, sh_gains);
            for(j=0; j<nSH; j++)
                echogram_rec->value[i][j] = sh_gains[j] * (echogram->value[echogram->sortedIdx[i]][0]);
        }
        free(sh_gains);
    }
}

void ims_shoebox_coreAbsorptionModule
(
    void* hWork,
    float** abs_wall
)
{
    ims_core_workspace *wrk = (ims_core_workspace*)(hWork);
    echogram_data *echogram_rec = (echogram_data*)(wrk->hEchogram_rec);
    echogram_data *echogram_abs;
    int i,band;
    float r_x[2], r_y[2], r_z[2];
    float abs_x, abs_y, abs_z, s_abs_tot;

    for(band=0; band < wrk->nBands; band++){
        echogram_abs = (echogram_data*)wrk->hEchogram_abs[band];

        /* Resize container (only done if needed) */
        ims_shoebox_echogramResize(wrk->hEchogram_abs[band], echogram_rec->numImageSources, echogram_rec->nChannels);

        /* Copy data */
        memcpy(FLATTEN2D(echogram_abs->value), FLATTEN2D(echogram_rec->value), (echogram_abs->numImageSources)*(echogram_abs->nChannels)*sizeof(float));
        memcpy(echogram_abs->time, echogram_rec->time, (echogram_abs->numImageSources)*sizeof(float));
        memcpy(FLATTEN2D(echogram_abs->order), FLATTEN2D(echogram_rec->order), (echogram_abs->numImageSources)*3*sizeof(int));
        memcpy(echogram_abs->coords, echogram_rec->coords, (echogram_abs->numImageSources)*sizeof(ims_pos_xyz));
        memcpy(echogram_abs->sortedIdx, echogram_rec->sortedIdx, (echogram_abs->numImageSources)*sizeof(int));

        /* Reflection coefficients given the absorption coefficients for x, y, z
         * walls per frequency */
        r_x[0] = sqrtf(1.0f - abs_wall[band][0]);
        r_x[1] = sqrtf(1.0f - abs_wall[band][1]);
        r_y[0] = sqrtf(1.0f - abs_wall[band][2]);
        r_y[1] = sqrtf(1.0f - abs_wall[band][3]);
        r_z[0] = sqrtf(1.0f - abs_wall[band][4]);
        r_z[1] = sqrtf(1.0f - abs_wall[band][5]);

        /* find total absorption coefficients by calculating the number of hits on
         * every surface, based on the order per dimension */
        for(i=0; i<echogram_abs->numImageSources; i++){
            /* Surfaces intersecting the x-axis */
            if(!(echogram_abs->order[i][0]%2)) //ISEVEN(echogram_abs->order[i][0]))
                abs_x = powf(r_x[0], (float)abs(echogram_abs->order[i][0])/2.0f) * powf(r_x[1], (float)abs(echogram_abs->order[i][0])/2.0f);
            else if (/* ISODD AND */echogram_abs->order[i][0]>0)
                abs_x = powf(r_x[0], ceilf((float)echogram_abs->order[i][0]/2.0f)) * powf(r_x[1], floorf((float)echogram_abs->order[i][0]/2.0f));
            else /* ISODD AND NEGATIVE */
                abs_x = powf(r_x[0], floorf((float)abs(echogram_abs->order[i][0])/2.0f)) * powf(r_x[1], ceilf((float)abs(echogram_abs->order[i][0])/2.0f));

            /* Surfaces intersecting the y-axis */
            if(!(echogram_abs->order[i][1]%2)) //ISEVEN(echogram_abs->order[i][1]))
                abs_y = powf(r_y[0], (float)abs(echogram_abs->order[i][1])/2.0f) * powf(r_y[1], (float)abs(echogram_abs->order[i][1])/2.0f);
            else if (/* ISODD AND */echogram_abs->order[i][1]>0)
                abs_y = powf(r_y[0], ceilf((float)echogram_abs->order[i][1]/2.0f)) * powf(r_y[1], floorf((float)echogram_abs->order[i][1]/2.0f));
            else /* ISODD AND NEGATIVE */
                abs_y = powf(r_y[0], floorf((float)abs(echogram_abs->order[i][1])/2.0f)) * powf(r_y[1], ceilf((float)abs(echogram_abs->order[i][1])/2.0f));

            /* Surfaces intersecting the y-axis */
            if(!(echogram_abs->order[i][2]%2)) //ISEVEN(echogram_abs->order[i][2]))
                abs_z = powf(r_z[0], (float)abs(echogram_abs->order[i][2])/2.0f) * powf(r_z[1], (float)abs(echogram_abs->order[i][2])/2.0f);
            else if (/* ISODD AND */echogram_abs->order[i][2]>0)
                abs_z = powf(r_z[0], ceilf((float)echogram_abs->order[i][2]/2.0f)) * powf(r_z[1], floorf((float)echogram_abs->order[i][2]/2.0f));
            else /* ISODD AND NEGATIVE */
                abs_z = powf(r_z[0], floorf((float)abs(echogram_abs->order[i][2])/2.0f)) * powf(r_z[1], ceilf((float)abs(echogram_abs->order[i][2])/2.0f));

            /* Apply Absorption */
            s_abs_tot = abs_x * abs_y * abs_z;
            utility_svsmul(echogram_abs->value[i], &s_abs_tot, echogram_abs->nChannels, NULL);
        }
    }
}

void ims_shoebox_renderRIR
(
    void* hWork,
    int fractionalDelayFLAG,
    float fs,
    float** H_filt,
    ims_rir* rir
)
{
    ims_core_workspace *wrk = (ims_core_workspace*)(hWork);
    echogram_data *echogram_abs;
    float* temp;
    int i, j, refl_idx, band, rir_len_samples;
    float endtime, rir_len_seconds;

    /* Render RIR for each octave band */
    for(band=0; band<wrk->nBands; band++){
        echogram_abs = (echogram_data*)wrk->hEchogram_abs[band];

        /* Determine length of rir */
        endtime = echogram_abs->time[echogram_abs->numImageSources-1];
        rir_len_samples = (int)(endtime * fs + 1.0f) + 1; /* ceil + 1 */
        rir_len_seconds = (float)rir_len_samples/fs;

        /* Render rir */
        if(fractionalDelayFLAG){
            // TODO: implement
        }
        else{
            /* Resize RIR vector */
            wrk->rir_bands[band] = (float**)realloc2d((void**)wrk->rir_bands[band], echogram_abs->nChannels, rir_len_samples, sizeof(float));
            wrk->rir_len_samples = rir_len_samples;
            wrk->rir_len_seconds = rir_len_seconds;
            memset(FLATTEN2D(wrk->rir_bands[band]), 0, (echogram_abs->nChannels)*rir_len_samples*sizeof(float)); /* flush */

            /* Accumulate 'values' for each image source */
            for(i=0; i<echogram_abs->numImageSources; i++){
                refl_idx = (int)(echogram_abs->time[i]*fs+0.5f); /* round */
                for(j=0; j<echogram_abs->nChannels; j++)
                    wrk->rir_bands[band][j][refl_idx] += echogram_abs->value[i][j];
            }
        }
    }

    temp = malloc1d((wrk->rir_len_samples+IMS_FIR_FILTERBANK_ORDER)*sizeof(float));

    /* Resize rir->data if needed, then flush with 0s */
    echogram_abs = (echogram_data*)wrk->hEchogram_abs[0];
    if( (echogram_abs->nChannels!=rir->nChannels) || (wrk->rir_len_samples !=rir->length) ){
        rir->data = realloc1d(rir->data, echogram_abs->nChannels * (wrk->rir_len_samples) * sizeof(float));
        rir->length = wrk->rir_len_samples;
        rir->nChannels = echogram_abs->nChannels;
    }
    memset(rir->data, 0, echogram_abs->nChannels * (wrk->rir_len_samples) * sizeof(float));

    /* Apply filterbank to rir_bands and sum them up */
    for(band=0; band<wrk->nBands; band++){
        echogram_abs = (echogram_data*)wrk->hEchogram_abs[band];

        /* Apply the LPF (lowest band), HPF (highest band), and BPF (all other bands) */
        for(j=0; j<echogram_abs->nChannels; j++)
            fftconv(wrk->rir_bands[band][j], H_filt[band], wrk->rir_len_samples, IMS_FIR_FILTERBANK_ORDER+1, 1, temp);

        /* Sum */
        for(i=0; i<echogram_abs->nChannels; i++)
            utility_svvadd( &(rir->data[i*(wrk->rir_len_samples)]), wrk->rir_bands[band][i], wrk->rir_len_samples, &(rir->data[i*(wrk->rir_len_samples)]));
    }

    free(temp);
}
