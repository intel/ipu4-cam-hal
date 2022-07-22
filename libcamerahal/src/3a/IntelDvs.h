/*
 * Copyright (C) 2017-2018 Intel Corporation
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

#include "iutils/Thread.h"
#include "iutils/Errors.h"

#include "AiqSetting.h"
#include "CameraEvent.h"

#include "ia_dvs.h"
#include "ia_dvs_types.h"
#include "ia_isp_bxt.h"

namespace icamera {

class DvsResult {
public:
    DvsResult();
    ~DvsResult();

    DvsResult& operator=(const DvsResult& other);

    ia_dvs_morph_table mMorphTable;

    ia_dvs_image_transformation mTransformation;

    long mSequence;

private:
    static const int MAX_DVS_COORDS_Y_SIZE = 33 * 69;
    static const int MAX_DVS_COORDS_UV_SIZE = 33 * 69;
    uint32_t mDvsXcoordsY[MAX_DVS_COORDS_Y_SIZE];
    uint32_t mDvsYcoordsY[MAX_DVS_COORDS_Y_SIZE];
    uint32_t mDvsXcoordsUV[MAX_DVS_COORDS_UV_SIZE];
    uint32_t mDvsYcoordsUV[MAX_DVS_COORDS_UV_SIZE];
    float mDvsXcoordsUVFloat[MAX_DVS_COORDS_UV_SIZE];
    float mDvsYcoordsUVFloat[MAX_DVS_COORDS_UV_SIZE];
};

struct DvsStatistics {
    DvsStatistics(ia_dvs_statistics *dvs = nullptr, long seq = -1) {
        dvsStats = dvs;
        sequence = seq;
    }
    ia_dvs_statistics *dvsStats;
    long sequence;
};

/**
 * \class IntelDvs
 * Wrapper of the DVSx, provide 2 basic functionalities in video mode:
 * 1. zoom (including center and freeform)
 * 2. DVS
 * The algorithm should generate the morph table to support the
 * above functionalities.
 */
class IntelDvs : public EventListener {
public:
    IntelDvs(int cameraId, AiqSetting *setting = nullptr);
    ~IntelDvs();

    int init();
    int deinit();
    int configure(const vector<ConfigMode>& configMode, uint32_t kernelId = 0,
                  int srcWidth = 0, int srcHeight = 0, int dstWidth = 0, int dstHeight = 0);

    int configure(TuningMode tuningMode, uint32_t kernelId, int srcWidth, int srcHeight,
                            int dstWidth, int dstHeight);

    int configureDigitalZoom(ia_dvs_zoom_mode zoom_mode, ia_rectangle &zoom_region,
                                  ia_coordinate &zoom_coordinate);
    int setZoomRatio(float zoom);

    void handleEvent(EventData eventData);

    int setStats(ia_dvs_statistics* statistics);
    int run(const ia_aiq_ae_results &ae_results,
                 DvsResult *result,
                 long sequence = 0,
                 uint16_t focus_position = 0);

    int updateParameter(const aiq_parameter_t &param);

// prevent copy constructor and assignment operator
private:
    DISALLOW_COPY_AND_ASSIGN(IntelDvs);

private:
    int initDvsHandle(TuningMode tuningMode);
    int deinitDvsHandle();
    int initDVSTable();
    int deInitDVSTable();
    int reconfigure();
    int runImpl(const ia_aiq_ae_results &ae_results,
                     uint16_t focus_position);
    int getMorphTable(long sequence, DvsResult *result);
    int getImageTrans(long sequence, DvsResult *result);
    int setDVSConfiguration(uint32_t kernelId, ia_dvs_configuration &config);
    int dumpDVSTable(ia_dvs_morph_table *table, long sequence);
    int dumpDVSTable(ia_dvs_image_transformation *trans, long sequence);
    int dumpConfiguration(ia_dvs_configuration config);

private:
    // Guard for class IntelDvs public API
    Mutex mLock;
    ia_dvs_state *mDvsHandle;
    bool mDvsEnabled;
    bool mLdcEnabled;
    bool mRscEnabled;
    float mDigitalZoomRatio;
    int mCameraId;
    float mFps;
    ConfigMode mConfigMode;
    TuningMode mTuningMode;
    AiqSetting *mAiqSetting;

    uint32_t mKernelId;
    camera_resolution_t mSrcResolution;
    camera_resolution_t mDstResolution;

    ia_dvs_morph_table *mMorphTable;
    ia_dvs_image_transformation mImage_transformation;
    ia_dvs_statistics *mStatistics;
};

} /* namespace icamera */
