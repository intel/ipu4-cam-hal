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

#define LOG_TAG "SensorHwCtrl"

#include <limits.h>

#include <linux/v4l2-controls.h>
// CRL_MODULE_S
#include <linux/crlmodule.h>
// CRL_MODULE_E

#include "iutils/CameraLog.h"

#include "SensorHwCtrl.h"
#include "V4l2DeviceFactory.h"
#include "PlatformData.h"

namespace icamera {

SensorHwCtrl::SensorHwCtrl(int cameraId, string pixelArraySubdevName, string sensorOutputSubdevName):
        mPixelArraySubdev(nullptr),
        mSensorOutputSubdev(nullptr),
        mCameraId(cameraId),
        mHorzBlank(0),
        mVertBlank(0),
        mCropWidth(0),
        mCropHeight(0),
        mWdrMode(0),
        mCurFll(0),
        mCalculatingFrameDuration(false)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    mPixelArraySubdevName = pixelArraySubdevName;
    mSensorOutputSubdevName = sensorOutputSubdevName;
// CRL_MODULE_S
    /**
     * Try to call V4L2_CID_LINE_LENGTH_PIXELS, if failed, it means llp can't
     * be read directly from sensor. Then calculate it with HBlank.
     * fll will be in the same case.
     */
    getPixelArraySubdev();
    if (mPixelArraySubdev) {
        int llp = 0;
        int status = mPixelArraySubdev->getControl(V4L2_CID_LINE_LENGTH_PIXELS, &llp);
        if (status != OK) {
            LOG1("%s, some sensors can't get llp directly, try to calculate it", __func__);
            mCalculatingFrameDuration = true;
        }
    }
// CRL_MODULE_E
}

SensorHwCtrl::~SensorHwCtrl()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
}

SensorHwCtrl* SensorHwCtrl::createSensorCtrl(int cameraId)
{
    string pixelArraySubdevName;
    string sensorOutputSubdevName;
    SensorHwCtrl* sensorCtrl = nullptr;
    pixelArraySubdevName.clear();
    int ret = PlatformData::getDevNameByType(cameraId, VIDEO_PIXEL_ARRAY, pixelArraySubdevName);
    if (ret == OK) {
        LOG1("%s ArraySubdev camera id:%d dev name:%s", __func__, cameraId, pixelArraySubdevName.c_str());

        // Binner and Scaler subdev only exits in CrlModule driver
        if (PlatformData::isUsingCrlModule(cameraId)) {
            sensorOutputSubdevName.clear();
            ret = PlatformData::getDevNameByType(cameraId, VIDEO_PIXEL_SCALER, sensorOutputSubdevName);
            if (ret == OK) {
                LOG1("%s ScalerSubdev camera id:%d dev name:%s", __func__, cameraId, sensorOutputSubdevName.c_str());
            } else {
                sensorOutputSubdevName.clear();
                ret = PlatformData::getDevNameByType(cameraId, VIDEO_PIXEL_BINNER, sensorOutputSubdevName);
                if (ret == OK) {
                    LOG1("%s BinnerSubdev camera id:%d dev name:%s", __func__, cameraId, sensorOutputSubdevName.c_str());
                }
            }
        }

        sensorCtrl = new SensorHwCtrl(cameraId, pixelArraySubdevName, sensorOutputSubdevName);
    } else {
        LOG1("%s create a dummy sensor ctrl for camera id:%d", __func__, cameraId);
        sensorCtrl = new DummySensor(cameraId);
    }
    return sensorCtrl;
}

// CRL_MODULE_S
int SensorHwCtrl::configure()
{
    int rhs1 = PlatformData::getFixedVbp(mCameraId); // VBP is rhs1 register value
    if (rhs1 >= 0) { // Fixed VBP enabled
        LOG1("%s: set fixed VBP %d", __func__, rhs1);
        getPixelArraySubdev();
        Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
        int status = mPixelArraySubdev->setControl(CRL_CID_EXPOSURE_RHS1, rhs1);
        Check(status != OK, status, "%s failed to o set exposure RHS1.", __func__);
    }
    return OK;
}
// CRL_MODULE_E

int SensorHwCtrl::getActivePixelArraySize(int &width, int &height, int &pixelCode)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");

    int status = mPixelArraySubdev->getPadFormat(0, width, height, pixelCode);
    mCropWidth = width;
    mCropHeight = height;

    LOG2("@%s, width:%d, height:%d, status:%d", __func__, width, height, status);
    return status;
}

int SensorHwCtrl::getPixelRate(int &pixelRate)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");

    int ret = mPixelArraySubdev->getControl(V4L2_CID_PIXEL_RATE, &pixelRate);

    LOG2("@%s, pixelRate:%d, ret:%d", __func__, pixelRate, ret);

    return ret;
}

int SensorHwCtrl::setExposure(const vector<int>& coarseExposures, const vector<int>& fineExposures)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
    Check((coarseExposures.empty() || fineExposures.empty()), BAD_VALUE, "No exposure data!");

// CRL_MODULE_S
    if (coarseExposures.size() > 1) {
        if (PlatformData::getHDRExposureType(mCameraId) == HDR_RELATIVE_MULTI_EXPOSURES) {
            return setShutterAndReadoutTiming(coarseExposures, fineExposures);
        } else if (PlatformData::getHDRExposureType(mCameraId) == HDR_MULTI_EXPOSURES) {
            return setMultiExposures(coarseExposures, fineExposures);
        } else if (PlatformData::getHDRExposureType(mCameraId) == HDR_DUAL_EXPOSURES_DCG_AND_VS) {
            return setDualExposuresDCGAndVS(coarseExposures, fineExposures);
        }
    }
// CRL_MODULE_E

    LOG2("%s coarseExposure=%d fineExposure=%d", __func__, coarseExposures[0], fineExposures[0]);
    LOG2("SENSORCTRLINFO: exposure_value=%d", coarseExposures[0]);
    return mPixelArraySubdev->setControl(V4L2_CID_EXPOSURE, coarseExposures[0]);
}

// CRL_MODULE_S
int SensorHwCtrl::setMultiExposures(const vector<int>& coarseExposures, const vector<int>& fineExposures)
{
    int status = BAD_VALUE;
    int shortExp = coarseExposures[0];
    int longExp = coarseExposures[1];

    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
    if (coarseExposures.size() > 2) {
        LOG2("coarseExposure[0]=%d fineExposure[0]=%d", coarseExposures[0], fineExposures[0]);
        // The first exposure is very short exposure if larger than 2 exposures.
        status = mPixelArraySubdev->setControl(CRL_CID_EXPOSURE_SHS2, coarseExposures[0]);
        Check(status != OK, status, "failed to set exposure SHS2 %d.", coarseExposures[0]);

        shortExp = coarseExposures[1];
        longExp = coarseExposures[2];

        LOG2("SENSORCTRLINFO: exposure_long=%d", coarseExposures[2]);   // long
        LOG2("SENSORCTRLINFO: exposure_med=%d", coarseExposures[1]);    // short
        LOG2("SENSORCTRLINFO: exposure_short=%d", coarseExposures[0]);  // very short
    }

    LOG2("shortExp=%d longExp=%d", shortExp, longExp);
    status = mPixelArraySubdev->setControl(CRL_CID_EXPOSURE_SHS1, shortExp);
    Check(status != OK, status, "failed to set exposure SHS1 %d.", shortExp);

    status = mPixelArraySubdev->setControl(V4L2_CID_EXPOSURE, longExp);
    Check(status != OK, status, "failed to set long exposure %d.", longExp);
    LOG2("SENSORCTRLINFO: exposure_value=%d", longExp);

    return status;
}

// CRL_MODULE_S
int SensorHwCtrl::setDualExposuresDCGAndVS(const vector<int>& coarseExposures, const vector<int>& fineExposures)
{
    int status = BAD_VALUE;
    int longExp = coarseExposures[1];

    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
    if (coarseExposures.size() > 2) {
        LOG2("coarseExposure[0]=%d fineExposure[0]=%d", coarseExposures[0], fineExposures[0]);
        // The first exposure is very short exposure for DCG + VS case.
        status = mPixelArraySubdev->setControl(CRL_CID_EXPOSURE_SHS1, coarseExposures[0]);
        Check(status != OK, status, "failed to set exposure SHS1 %d.", coarseExposures[0]);

        longExp = coarseExposures[2];
        LOG2("SENSORCTRLINFO: exposure_long=%d", coarseExposures[2]);   // long
    }

    status = mPixelArraySubdev->setControl(V4L2_CID_EXPOSURE, longExp);
    Check(status != OK, status, "failed to set long exposure %d.", longExp);
    LOG2("SENSORCTRLINFO: exposure_value=%d", longExp);

    return status;
}

int SensorHwCtrl::setShutterAndReadoutTiming(const vector<int>& coarseExposures,
                                             const vector<int>& fineExposures)
{
    // DOL sensor exposure setting
    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
    getSensorOutputSubdev();
    Check(!mSensorOutputSubdev, NO_INIT, "sensor output sub device is not set");


    int width, height = 0;
    int pixelCode = 0;
    int status = mSensorOutputSubdev->getPadFormat(SENSOR_OUTPUT_PAD, width, height, pixelCode);
    Check(status != OK, status, "%s failed to get sensor output resolution.", __func__);
    LOG2("%s Sensor output width=%d height=%d", __func__, width, height);

    vector<MultiExpRange> ExpRanges = PlatformData::getMultiExpRanges(mCameraId);
    for (auto range : ExpRanges) {
        if (range.Resolution.width == width && range.Resolution.height == height) {
            int shs1, rhs1, shs2 = 0;

            if (coarseExposures.size() > 2) {
                // LEF(coarseExposures[2]) = SHS3.max + SHS3.upperBound - SHS3 - OFFSET
                int shs3 = range.SHS3.max + range.SHS3.upperBound
                       - coarseExposures[2] - 1;
                // SHS3 range [RHS2 + RHS2.upperBound ~ SHS3.max]
                CheckWarning((shs3 < range.SHS3.min || shs3 > range.SHS3.max),
                            NO_INIT, "%s : SHS3 not match %d [%d ~ %d]", __func__,
                            shs3, range.SHS3.min, range.SHS3.max);
                status = mPixelArraySubdev->setControl(CRL_CID_EXPOSURE_SHS3, shs3);
                Check(status != OK, status, "%s failed to set exposure SHS3.", __func__);

                // RHS2 range [SHS2 + upperBound ~ SHS3 - lowerBound] and should = min + n * step
                int rhs2 = shs3 - range.RHS2.upperBound -
                       ((shs3 - range.RHS2.upperBound) % range.RHS2.step);
                CheckWarning((rhs2 < range.RHS2.min || rhs2 > range.RHS2.max),
                             NO_INIT, "%s : RHS2 not match %d [%d ~ %d]", __func__,
                             rhs2, range.RHS2.min, range.RHS2.max);
                status = mPixelArraySubdev->setControl(CRL_CID_EXPOSURE_RHS2, rhs2);
                Check(status != OK, status, "%s failed to set exposure RHS2.", __func__);

                // SEF2(coarseExposures[1]) = RHS2 - SHS2 - OFFSET
                shs2 = rhs2 - coarseExposures[1] - 1;
            } else {
                // LEF(coarseExposures[2]) = FLL + SHS2.upperBound - SHS2 - OFFSET
                shs2 = mCurFll + range.SHS2.upperBound - coarseExposures[1] - 1;
            }

            // SHS2 range [RHS1 + RHS1.upperBound ~ SHS2.max]
            CheckWarningNoReturn((shs2 < range.SHS2.min || shs2 > std::max(range.SHS2.max, mCurFll)),
                         "%s : SHS2 not match %d [%d ~ %d]", __func__,
                         shs2, range.SHS2.min, std::max(range.SHS2.max, mCurFll));
            shs2 = CLIP(shs2, std::max(range.SHS2.max, mCurFll), range.SHS2.min);
            status = mPixelArraySubdev->setControl(CRL_CID_EXPOSURE_SHS2, shs2);
            Check(status != OK, status, "%s failed to set exposure SHS2.", __func__);

            // RHS1 range [SHS1 + upperBound ~ SHS2 - lowerBound] and should = min + n * step
            rhs1 = shs2 - range.RHS1.upperBound - ((shs2 - range.RHS1.upperBound) % range.RHS1.step);

            // Set RHS1(VBP) when fixed VBP is not enabled
            int fixedVbp = PlatformData::getFixedVbp(mCameraId);
            if (fixedVbp < 0) {
                CheckWarningNoReturn((rhs1 < range.RHS1.min || rhs1 > range.RHS1.max),
                             "%s : RHS1 not match %d [%d ~ %d]", __func__,
                             rhs1, range.RHS1.min, range.RHS1.max);
                rhs1 = CLIP(rhs1, range.RHS1.max, range.RHS1.min);
                // Set RHS1 if not using fixed VBP
                LOG2("%s: set dynamic VBP %d", __func__, rhs1);
                status = mPixelArraySubdev->setControl(CRL_CID_EXPOSURE_RHS1, rhs1);
                Check(status != OK, status, "%s failed to set exposure RHS1.", __func__);
            } else {
                // Use fixed VBP for RHS1 value
                LOG2("%s: calculated RHS1 vs. fixed VBP [%d vs. %d], use fixed VBP for RHS1 value",
                     __func__, rhs1, fixedVbp);
                rhs1 = fixedVbp;
                CheckWarning((rhs1 < range.RHS1.min || rhs1 > range.RHS1.max),
                             NO_INIT, "%s : RHS1 not match %d [%d ~ %d]", __func__,
                             rhs1, range.RHS1.min, range.RHS1.max);
                CheckWarning(((shs2 - range.RHS1.upperBound) % range.RHS1.step != 0),
                              NO_INIT, "%s: fixed VBP(RHS1) do not devided by RHS1 step", __func__);
            }

            // SEF1(coarseExposures[0]) = RHS1 - SHS1 - OFFSET
            shs1 = rhs1 - coarseExposures[0] - 1;
            // SHS1 range [min ~ max]
            CheckWarningNoReturn((shs1 < range.SHS1.min || shs1 > range.SHS1.max),
                         "%s : SHS1 not match %d [%d ~ %d]", __func__,
                         shs1, range.SHS1.min, range.SHS1.max);
            shs1 = CLIP(shs1, range.SHS1.max, range.SHS1.min);
            status = mPixelArraySubdev->setControl(CRL_CID_EXPOSURE_SHS1, shs1);
            Check(status != OK, status, "%s failed to set exposure SHS1.", __func__);

            LOG2("%s: set exposures done.", __func__);
            return status;
        }
    }

    LOGE("%s No matching resolution for exposure range", __func__);
    return NO_INIT;
}
// CRL_MODULE_E

int SensorHwCtrl::setGains(const vector<int>& analogGains, const vector<int>& digitalGains)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
    Check((analogGains.empty() || digitalGains.empty()), BAD_VALUE, "No gain data!");

// CRL_MODULE_S
    if (analogGains.size() > 1) {
        int status = BAD_VALUE;

        if (PlatformData::getHDRGainType(mCameraId) == HDR_MULTI_DG_AND_CONVERTION_AG) {
            status = setMultiDigitalGain(digitalGains);
            status |= setConversionGain(analogGains);

            return status;
        } else if (PlatformData::getHDRGainType(mCameraId) == HDR_MULTI_DG_AND_DIRECT_AG) {
            LOG2("HDR multi conversion gain");
            status = setMultiDigitalGain(digitalGains);
            status |= setMultiAnalogGain(analogGains);

            return status;
        }
    }

    LOG2("%s analogGain=%d digitalGain=%d", __func__, analogGains[0], digitalGains[0]);
    if (mWdrMode && PlatformData::getHDRGainType(mCameraId) == HDR_ISP_DG_AND_SENSOR_DIRECT_AG) {
        LOG2("%s: WDR mode, skip sensor DG, all digital gain is passed to ISP", __func__);
    } else if (PlatformData::isUsingSensorDigitalGain(mCameraId)) {
        if (mPixelArraySubdev->setControl(V4L2_CID_GAIN, digitalGains[0]) != OK) {
             LOGW("set digital gain failed");
        }
    }
// CRL_MODULE_E

    LOG2("SENSORCTRLINFO: gain_value=%d", analogGains[0]);
    return mPixelArraySubdev->setControl(V4L2_CID_ANALOGUE_GAIN, analogGains[0]);
}

// CRL_MODULE_S
int SensorHwCtrl::setMultiDigitalGain(const vector<int>& digitalGains)
{
    int status = BAD_VALUE;
    int shortDg = digitalGains[0];
    int longDg = digitalGains[1];

    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
    if (digitalGains.size() > 2) {
        LOG2("digitalGains[0]=%d", digitalGains[0]);
        status = mPixelArraySubdev->setControl(CRL_CID_DIGITAL_GAIN_VS, digitalGains[0]);
        Check(status != OK, status, "failed to set very short DG %d.", digitalGains[0]);

        shortDg = digitalGains[1];
        longDg = digitalGains[2];
    }

    LOG2("shortDg=%d longDg=%d", shortDg, longDg);
    status = mPixelArraySubdev->setControl(CRL_CID_DIGITAL_GAIN_S, shortDg);
    Check(status != OK, status, "failed to set short DG %d.", shortDg);

    status = mPixelArraySubdev->setControl(V4L2_CID_GAIN, longDg);
    Check(status != OK, status, "failed to set long DG %d.", longDg);

    return status;
}

int SensorHwCtrl::setMultiAnalogGain(const vector<int>& analogGains)
{
    int status = BAD_VALUE;
    int shortAg = analogGains[0];
    int longAg = analogGains[1];

    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
    if (analogGains.size() > 2) {
        LOG2("VS AG %d", analogGains[0]);
        int status = mPixelArraySubdev->setControl(CRL_CID_ANALOG_GAIN_VS, analogGains[0]);
        Check(status != OK, status, "failed to set VS AG %d", analogGains[0]);

        shortAg = analogGains[1];
        longAg = analogGains[2];

        LOG2("SENSORCTRLINFO: gain_long=%d", analogGains[2]);   // long
        LOG2("SENSORCTRLINFO: gain_med=%d", analogGains[1]);    // short
        LOG2("SENSORCTRLINFO: gain_short=%d", analogGains[0]);  // very short
    }

    LOG2("shortAg=%d longAg=%d", shortAg, longAg);
    status = mPixelArraySubdev->setControl(CRL_CID_ANALOG_GAIN_S, shortAg);
    Check(status != OK, status, "failed to set short AG %d.", shortAg);

    status = mPixelArraySubdev->setControl(V4L2_CID_ANALOGUE_GAIN, longAg);
    Check(status != OK, status, "failed to set long AG %d.", longAg);

    return status;
}

int SensorHwCtrl::setConversionGain(const vector<int>& analogGains)
{
    Check(analogGains.size() < 2, BAD_VALUE, "Gain data error!");

    /* [0, 1] bits are long AG, [2, 3] bits are short AG, [4, 5] bits are very short AG.
       [6] bit is long conversion gain, [7] bit is very short conversion gain.
       Long AG:       0x0X0000XX
       Short AG:      0x0000XX00
       Very Short AG: 0xX0XX0000 */
    int value = analogGains[0] | analogGains[1] | analogGains[2];
    LOG2("very short AG %d, short AG %d, long AG %d, conversion value %d",
          analogGains[0], analogGains[1], analogGains[2], value);

    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
    int status = mPixelArraySubdev->setControl(V4L2_CID_ANALOGUE_GAIN, value);
    Check(status != OK, status, "failed to set AG %d", value);

    return OK;
}
// CRL_MODULE_E

int SensorHwCtrl::setLineLengthPixels(int llp)
{
    int status = OK;
    LOG2("@%s, llp:%d", __func__, llp);

    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
    if (mCalculatingFrameDuration) {
        int horzBlank = llp - mCropWidth;
        if (mHorzBlank != horzBlank) {
            status = mPixelArraySubdev->setControl(V4L2_CID_HBLANK, horzBlank);
        }
    }
// CRL_MODULE_S
    else {
        status = mPixelArraySubdev->setControl(V4L2_CID_LINE_LENGTH_PIXELS, llp);
    }
// CRL_MODULE_E

    Check(status != OK, status, "failed to set llp.");

    mHorzBlank = llp - mCropWidth;
    return status;
}

int SensorHwCtrl::setFrameLengthLines(int fll)
{
    int status = OK;
    LOG2("@%s, fll:%d", __func__, fll);

    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
    if (mCalculatingFrameDuration) {
        int vertBlank = fll - mCropHeight;
        if (mVertBlank != vertBlank) {
            status = mPixelArraySubdev->setControl(V4L2_CID_VBLANK, vertBlank);
        }
    }
// CRL_MODULE_S
    else {
        status = mPixelArraySubdev->setControl(V4L2_CID_FRAME_LENGTH_LINES, fll);
    }
// CRL_MODULE_E

    mCurFll = fll;

    Check(status != OK, status, "failed to set fll.");

    mVertBlank = fll - mCropHeight;
    return status;
}

int SensorHwCtrl::setFrameDuration(int llp, int fll)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");

    int status = OK;
    LOG2("@%s, llp:%d, fll:%d", __func__, llp, fll);

    /* only set them to driver when llp or fll is not 0 */
    if (llp) {
        status = setLineLengthPixels(llp);
    }

    if (fll) {
        status |= setFrameLengthLines(fll);
    }

    return status;
}

int SensorHwCtrl::getLineLengthPixels(int &llp)
{
    int status = OK;

    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
    if (mCalculatingFrameDuration) {
        int horzBlank = 0;
        status = mPixelArraySubdev->getControl(V4L2_CID_HBLANK, &horzBlank);
        if (status == OK) {
            mHorzBlank = horzBlank;
            llp = horzBlank + mCropWidth;
        }
    }
// CRL_MODULE_S
    else {
        status = mPixelArraySubdev->getControl(V4L2_CID_LINE_LENGTH_PIXELS, &llp);
        if (status == OK) {
            mHorzBlank = llp - mCropWidth;
        }
    }
// CRL_MODULE_E

    LOG2("@%s, llp:%d", __func__, llp);
    Check(status != OK, status, "failed to get llp.");

    return status;
}

int SensorHwCtrl::getFrameLengthLines(int &fll)
{
    int status = OK;

    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");
    if (mCalculatingFrameDuration) {
        int vertBlank = 0;
        status = mPixelArraySubdev->getControl(V4L2_CID_VBLANK, &vertBlank);
        if (status == OK) {
            mVertBlank = vertBlank;
            fll = vertBlank + mCropHeight;
        }
    }
// CRL_MODULE_S
    else {
        status = mPixelArraySubdev->getControl(V4L2_CID_FRAME_LENGTH_LINES, &fll);
        if (status == OK) {
            mVertBlank = fll - mCropHeight;
        }
    }
// CRL_MODULE_E

    LOG2("@%s, fll:%d", __func__, fll);
    Check(status != OK, status, "failed to get fll.");

    return status;
}

int SensorHwCtrl::getFrameDuration(int &llp, int &fll)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");

    int status = getLineLengthPixels(llp);

    status |= getFrameLengthLines(fll);
    LOG2("@%s, llp:%d, fll:%d", __func__, llp, fll);

    return status;
}

int SensorHwCtrl::getVBlank(int &vblank)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    vblank = mVertBlank;
    LOG2("@%s, vblank:%d", __func__, vblank);

    return OK;
}

/**
 * get exposure range value from sensor driver
 *
 * \param[OUT] coarse_exposure: exposure min value
 * \param[OUT] fine_exposure: exposure max value
 * \param[OUT] exposure_step: step of exposure
 * V4L2 does not support FINE_EXPOSURE setting
 *
 * \return OK if successfully.
 */
int SensorHwCtrl::getExposureRange(int &exposureMin, int &exposureMax, int &exposureStep)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    getPixelArraySubdev();
    Check(!mPixelArraySubdev, NO_INIT, "pixel array sub device is not set");

    v4l2_queryctrl exposure;
    CLEAR(exposure);

    int status = mPixelArraySubdev->queryControl(V4L2_CID_EXPOSURE, &exposure);
    Check(status != OK, status, "Couldn't get exposure Range status:%d", status);

    exposureMin = exposure.minimum;
    exposureMax = exposure.maximum;
    exposureStep = exposure.step;
    LOG2("@%s, exposureMin:%d, exposureMax:%d, exposureStep:%d",
        __func__, exposureMin, exposureMax, exposureStep);

    return status;
}

int SensorHwCtrl::setWdrMode(int mode)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    getSensorOutputSubdev();
    Check(!mSensorOutputSubdev, NO_INIT, "sensor output sub device is not set");

    LOG2("%s WDR Mode=%d", __func__, mode);
    int ret = OK;

    mWdrMode = mode;

    if (PlatformData::getHDRExposureType(mCameraId) != HDR_RELATIVE_MULTI_EXPOSURES
        && PlatformData::getHDRExposureType(mCameraId) != HDR_DUAL_EXPOSURES_DCG_AND_VS)
    {
        LOG2("%s: set WDR mode for non-DOL sensor", __func__);
        ret = mSensorOutputSubdev->setControl(V4L2_CID_WDR_MODE, mode);
    }

    return ret;
}

int SensorHwCtrl::setFrameRate(float fps)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    getSensorOutputSubdev();
    Check(!mSensorOutputSubdev, NO_INIT, "sensor output sub device is not set");

    LOG2("%s FPS is: %f", __func__, fps);

    struct v4l2_queryctrl query;
    CLEAR(query);
    int status = mSensorOutputSubdev->queryControl(V4L2_CID_LINK_FREQ, &query);
    Check(status != OK, status, "Couldn't get V4L2_CID_LINK_FREQ, status:%d", status);

    LOG2("@%s, query V4L2_CID_LINK_FREQ:, default_value:%d, maximum:%d, minimum:%d, step:%d",
        __func__, query.default_value, query.maximum, query.minimum, query.step);

    int mode = 0;
    if (query.maximum == query.minimum) {
        mode = query.default_value;
    } else {
        /***********************************************************************************
         * WA: This heavily depends on sensor driver implementation, need to find a graceful
         * solution.
         * imx185:
         * When fps larger than 30, should switch to high speed mode, currently only
         * 0, 1, 2 are available. 0 means 720p 30fps, 1 means 2M 30fps, and 2 means 2M 60fps.
         * imx290:
         * 0 and 1 available, for 30 and higher FPS.
         ***********************************************************************************/
        mode = (fps > 30) ? query.maximum : (query.maximum - 1);
    }
    LOG2("@%s, set V4L2_CID_LINK_FREQ to %d", __func__, mode);
    return mSensorOutputSubdev->setControl(V4L2_CID_LINK_FREQ, mode);
}

} // namespace icamera
