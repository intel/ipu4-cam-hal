/*
 * Copyright (C) 2016-2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <queue>

#include "ia_ltm.h"

#include "CameraEvent.h"
#include "AiqSetting.h"
#include "AiqResult.h"
#include "iutils/Thread.h"
#include "iutils/CameraDump.h"

namespace icamera {

struct ltm_result_t {
    ia_ltm_drc_params ltmDrcParams;
    ia_ltm_results ltmResults;

    camera_resolution_t yvGridInfo;

    long sequence;

    ltm_result_t() {
        CLEAR(ltmDrcParams);
        CLEAR(ltmResults);
        yvGridInfo = {0, 0};
        sequence = -1;
    }
};

struct LtmInputParams {
    ia_isp_bxt_hdr_yv_grid_t hdrYvGrid;
    ia_ltm_input_params ltmParams;
    ia_ltm_gtm_input_params gtmParams;
    long sequence;

    LtmInputParams() {
        CLEAR(hdrYvGrid);
        CLEAR(ltmParams);
        CLEAR(gtmParams);
        sequence = -1;
    }
};
/**
 * There are two SIS port, SIS_A is for preview.
 * SIS_B is for still and capture.
 */
enum SisPort
{
    SIS_A = 0,
    SIS_B,
    MAX_SIS_NUM
};

struct SisBuffer
{
    SisPort sisPort;
    ia_binary_data sisImage;

    SisBuffer() {
        sisPort = SIS_A;
        CLEAR(sisImage);
    }
};

struct LtmStatistics {
    LtmStatistics(ia_isp_bxt_hdr_yv_grid_t *hdrYv = nullptr, long seq = -1) {
        hdrYvGrid = hdrYv;
        sequence = seq;
    }
    ia_isp_bxt_hdr_yv_grid_t *hdrYvGrid;
    long sequence;
};

/**
 * \class Ltm
 * This class is used to run Local tone mapping (Ltm) algorithm.
 */
class Ltm : public EventListener {
public:
    Ltm(int cameraId);
    ~Ltm();

    int init();
    int deinit();
    int start();
    void stop();

    int configure(const vector<ConfigMode>& configModes);

    /**
     * \brief handle statistics event
     */
    void handleEvent(EventData eventData);

    int handleLtm(ia_isp_bxt_hdr_yv_grid_t* hdrYvGrid, unsigned long long timestamp = 0, long sequence = 0);
    int handleSisLtm(const shared_ptr<CameraBuffer> &cameraBuffer);
private:
    DISALLOW_COPY_AND_ASSIGN(Ltm);

    int runLtm();
    int initLtmParams();
    int runLtm(ia_aiq_ae_results* aeResult, ia_aiq_ae_results* lastAeResult,
               ltm_result_t *ltmResult, ia_ltm_input_params *ltmParams = NULL);

    int initIaLtmHandle(TuningMode tuningMode);
    int deinitIaLtmHandle();

    int updateTuningData();
    AiqResult *getAiqResult(long sequence, bool nextFrame);
    int updateParameter(const aiq_parameter_t &param, unsigned long long timestamp);

    int dumpLtmDrcParams(const ia_ltm_drc_params* ltmDrcParams);
    int dumpLtmResultsParams(const ia_ltm_results *ltmResults);

private:
    /**
     * \brief The ltm thread
     */
    class LtmThread: public Thread {
        Ltm *mLtmHandle;
        public:
        LtmThread(Ltm *pThis)
            : mLtmHandle(pThis) { }

        virtual bool threadLoop() {
            int ret = mLtmHandle->runLtm();
            return (ret == 0);
        }
    };

    // Offset to locate the LTM tuning data inside LTM handle.
    const int kLtmTuningDataOffset = sizeof(ia_mkn*) + sizeof(char*);

    int mCameraId;
    ia_ltm *mLtm;
    TuningMode mTuningMode;

    enum LtmState {
        LTM_NOT_INIT = 0,
        LTM_INIT,
        LTM_CONFIGURED,
        LTM_MAX
    } mLtmState;

    // Use it to lock mInputParamIndex and mLtmParams
    // And use it for thread condition lock
    Mutex  mLtmLock;
    LtmThread *mLtmThread;
    bool mThreadRunning;
    Condition mParamAvailableSignal;
    static const nsecs_t kWaitDuration = 2000000000; // 2000ms
    static const int kMaxLtmParamsNum = 2; // 2 ltm input params

    int mInputParamIndex;
    LtmInputParams *mLtmParams[kMaxLtmParamsNum];
    queue<LtmInputParams *> mLtmParamsQ;

    SisBuffer *mSisBuffer[kMaxLtmParamsNum];
    BinParam_t mLtmBinParam;
};

} /* namespace icamera */

