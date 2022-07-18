/*
 * Copyright (C) 2015-2018 Intel Corporation
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

#include "iutils/Errors.h"
#include "V4l2SubDev.h"
#include "V4l2DeviceFactory.h"

namespace icamera {

/**
 * Base class for sensor control might be inherited by ones have different sensor driver
 */
class SensorHwCtrl {

public:
    static SensorHwCtrl* createSensorCtrl(int cameraId);
    SensorHwCtrl(int cameraId, string pixelArraySubdevName, string sensorOutputSubdevName);
    virtual ~SensorHwCtrl();

// CRL_MODULE_S
    virtual int configure();
// CRL_MODULE_E
    virtual int getPixelRate(int &pixelRate);
    virtual int setExposure(const vector<int>& coarseExposures, const vector<int>& fineExposures);
    virtual int setGains(const vector<int>& analogGains, const vector<int>& digitalGains);
    virtual int setFrameDuration(int llp, int fll);
    virtual int getFrameDuration(int &llp, int &fll);
    virtual int getVBlank(int &vblank);
    virtual int getActivePixelArraySize(int &width, int &height, int &pixelCode);
    virtual int getExposureRange(int &exposureMin, int &exposureMax, int &exposureStep);

    /**
     * Set WDR mode to sensor which is used to select WDR sensor settings or none-WDR settings.
     * If 1 is set, WDR sensor settings will be used,
     * while if 0 is set, none-WDR sensor settings will be used.
     *
     * \param[IN] mode: WDR mode
     *
     *\return OK if successfully.
     */
    virtual int setWdrMode(int mode);

    virtual int setFrameRate(float fps);
private:
    int setLineLengthPixels(int llp);
    int getLineLengthPixels(int &llp);
    int setFrameLengthLines(int fll);
    int getFrameLengthLines(int &fll);

// CRL_MODULE_S
    int setMultiExposures(const vector<int>& coarseExposures, const vector<int>& fineExposures);
    int setDualExposuresDCGAndVS(const vector<int>& coarseExposures, const vector<int>& fineExposures);
    int setShutterAndReadoutTiming(const vector<int>& coarseExposures, const vector<int>& fineExposures);
    int setConversionGain(const vector<int>& analogGains);
    int setMultiDigitalGain(const vector<int>& digitalGains);
    int setMultiAnalogGain(const vector<int>& analogGains);
// CRL_MODULE_E

    void getPixelArraySubdev() { if (!mPixelArraySubdevName.empty())
                   mPixelArraySubdev = V4l2DeviceFactory::getSubDev(mCameraId, mPixelArraySubdevName); }
    void getSensorOutputSubdev() { if (!mSensorOutputSubdevName.empty())
               mSensorOutputSubdev = V4l2DeviceFactory::getSubDev(mCameraId, mSensorOutputSubdevName); }

private:
    // DOL sensor sink pad
    static const int SENSOR_OUTPUT_PAD = 1;

    V4l2SubDev* mPixelArraySubdev;
    V4l2SubDev* mSensorOutputSubdev;
    int mCameraId;
    string mPixelArraySubdevName;
    string mSensorOutputSubdevName;
    int mHorzBlank;
    int mVertBlank;
    int mCropWidth;
    int mCropHeight;

    int mWdrMode;

    // Current frame length lines
    int mCurFll;

    /**
     * if mCalculatingFrameDuration is true, it means sensor can't set/get llp/fll directly,
     * use HBlank/VBlank to calculate it.
     */
    bool mCalculatingFrameDuration;
}; //class SensorHwCtrl

/**
 * Dummy sensor hardware ctrl interface for those sensors cannot be controlled.
 */
class DummySensor : public SensorHwCtrl {
public:
    DummySensor(int cameraId) : SensorHwCtrl(cameraId, "", "") {}
    ~DummySensor() {}

    int setDevice(V4l2SubDev* pixelArraySubdev) { return OK; }
    int getPixelRate(int &pixelRate) { return OK; }
    int setExposure(const vector<int>& coarseExposures, const vector<int>& fineExposures) { return OK; }
    int setGains(const vector<int>& analogGains, const vector<int>& digitalGains) { return OK; }
    int setFrameDuration(int llp, int fll) { return OK; }
    int getFrameDuration(int &llp, int &fll) { return OK; }
    int getVBlank(int &vblank) { return OK; }
    int getActivePixelArraySize(int &width, int &height, int &code) { return OK; }
    int getExposureRange(int &exposureMin, int &exposureMax, int &exposureStep) { return OK; }
    int setWdrMode(int mode) { return OK; }
    int setFrameRate(float fps) { return OK; }
};

} // namespace icamera
