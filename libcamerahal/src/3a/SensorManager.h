/*
 * Copyright (C) 2015-2018 Intel Corporation.
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

#include "ia_aiq.h"

#include "CameraEvent.h"
#include "SensorHwCtrl.h"

namespace icamera {

/*
 * \struct WdrModeSetting
 *
 * This struct is used to control wdr mode switching.
 */
typedef struct {
    long sequence;
    TuningMode tuningMode;
} WdrModeSetting;

typedef struct {
    unsigned short realDigitalGain;
    ia_aiq_exposure_sensor_parameters sensorParam;
} SensorExposure;

typedef struct {
    long sequence;
    uint64_t timestamp;
} SofEventInfo;

typedef vector <SensorExposure> SensorExpGroup;
/*
 * \class SensorManager
 *
 * This class is used to control exposure and gain synchronization mechanism
 * and get some sensor info.
 */
class SensorManager : public EventListener {

public:
    SensorManager(int cameraId, SensorHwCtrl *sensorHw);
    ~SensorManager();

    int init();
    int deinit();
    void reset();

    // get EventListener
    EventListener *getSofEventListener();

    void handleEvent(EventData eventData);
    uint32_t updateSensorExposure(SensorExpGroup sensorExposures, bool useSof = true);
    int getSensorInfo(ia_aiq_frame_params &frameParams,
                      ia_aiq_exposure_sensor_descriptor &sensorDescriptor);

    int setWdrMode(TuningMode tuningMode, long sequence);
    int setFrameRate(float fps);
    int getCurrentExposureAppliedDelay();
    uint64_t getSofTimestamp(long sequence);
private:
    DISALLOW_COPY_AND_ASSIGN(SensorManager);

    void handleSensorExposure();
    void handleSensorModeSwitch(long sequence);
    int getSensorModeData(ia_aiq_exposure_sensor_descriptor& sensorData);
    void setSensorExposureAndGains(SensorExpGroup sensorExposures);
    int setFrameDuration(int lineLengthPixels, int frameLengthLines);
    int convertTuningModeToWdrMode(TuningMode tuningMode);
    void setSensorGains(int index);

private:
    static const int kMaxSensorExposures = 10;
    static const int kMaxExposureHistory = 5;
    static const int kMaxSofEventInfo = 10;

    int mCameraId;
    SensorHwCtrl *mSensorHwCtrl;

    bool    mModeSwitched;         // Whether the TuningMode get updated
    WdrModeSetting mWdrModeSetting;

    bool mPerframeControl;
    vector<SensorExpGroup> mSensorExposures;

    long mLastSofSequence;

    // Guard for SensorManager public API.
    Mutex mLock;

    int mGainDelay;
    SensorExpGroup mSensorExposureHistory[kMaxExposureHistory];
    int64_t mSensorExposureHistoryIndex;

    vector<SofEventInfo> mSofEventInfo;
};

} /* namespace icamera */
