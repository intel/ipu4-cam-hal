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

#ifndef __AIQ_RESULT_STORAGE__
#define __AIQ_RESULT_STORAGE__

#include <map>

#include "AiqResult.h"

// LOCAL_TONEMAP_S
#include "Ltm.h"
// LOCAL_TONEMAP_E
// INTEL_DVS_S
#include "IntelDvs.h"
// INTEL_DVS_E
#include "AiqStatistics.h"

#include "iutils/Utils.h"
#include "iutils/Thread.h"
#include "iutils/RWLock.h"

namespace icamera {

/**
 * \class AiqResultStorage
 *
 * This class provides interfaces for setting and getting AiqResult, and a storage space
 * which is able to contain at most `kStorageSize` AiqResults at same time.
 *
 * It's a singleton based on camera id, and its life cycle can be maintained by
 * its static methods getInstance and releaseAiqResultStorage.
 */
class AiqResultStorage {
public:
    /**
     * \brief Get internal instance for cameraId.
     *
     * param[in] int camera id: only one instance for one particular camera id.
     *
     * return the instance of AiqResultStorage for cameraId
     */
    static AiqResultStorage* getInstance(int cameraId);

    /**
     * \brief Release the static instance of AiqResultStorage for cameraId.
     */
    static void releaseAiqResultStorage(int cameraId);

    /**
     * \brief Acquire Aiq result.
     *
     * The function will return one Aiq result pointer which is kept by Aiq algo.
     * The sequence id is set to -1 which indicates the Aiq result is invalid.
     *
     * return Aiq result pointer to be kept by Aiq algo.
     */
    AiqResult* acquireAiqResult();

    /**
     * \brief Update mCurrentIndex and set sequence id into internal storage.
     */
    void updateAiqResult(long sequence);

    /**
     * \brief Get the pointer of aiq result to internal storage by given sequence id.
     *
     * The function will return the internal pointer of AiqResult, the caller MUST use this
     * pointer quickly, let's say less than 10ms. For any time-consuming operations, it's
     * the caller's responsibility to do a deep-copy, otherwise the data in returned AiqResult
     * may not be consistent.
     *
     * param[in] long sequence: specify which aiq result is needed.
     *
     * return 1. when sequence id is -1 or not provided, the lastest result will be returned.
     *        2. when sequence id is larger than -1, the result with gaven sequence id will be returned.
     *        3. if cannot find in result storage, it means either sequence id is too old and its
     *           result was overrided, or the sequence id is too new, and its result has not been
     *           saved into storage yet. For both cases, nullptr will be returned.
     */
    const AiqResult* getAiqResult(long sequence = -1);

    // LOCAL_TONEMAP_S
    /**
     * \brief Acquire Ltm result.
     *
     * The function will return one Ltm result pointer which is kept by LTM algo.
     * The sequence id is set to -1 which indicates the Ltm result is invalid.
     *
     * return Ltm result pointer to be kept by LTM algo.
     */
    ltm_result_t* acquireLtmResult();

    /**
     * \brief Update mCurrentLtmIndex and set sequence id in internal storage.
     */
    void updateLtmResult(long sequence);

    /**
     * \brief Get the pointer of ltm_result_t.
     *
     * The function will return the latest Ltm result.
     *
     * return the latest Ltm result.
     */
    const ltm_result_t* getLtmResult(long sequence = -1);

    /**
     * \brief Acquire Ltm tuning data.
     *
     * The function will return one Ltm tuning data pointer.
     *
     * return Ltm tuning data pointer.
     */
    ltm_tuning_data* acquireLtmTuningData();

    /**
     * \brief Update mCurrentLtmTuningIndex.
     */
    void updateLtmTuningData();

    /**
     * \brief Get the pointer of ltm_tuning_data.
     *
     * The function will return the latest Ltm tuning data.
     *
     * return the latest Ltm tuning data.
     */
    const ltm_tuning_data* getLtmTuningData();
    // LOCAL_TONEMAP_E

    // INTEL_DVS_S
    /**
     * \brief Acquire Dvs result.
     *
     * The function will return one Dvs result pointer which is kept by Dvs algo.
     * The sequence id is set to -1 which indicates the Dvs result is invalid.
     *
     * return Dvs result pointer to be kept by Dvs algo.
     */
    DvsResult* acquireDvsResult();

    /**
     * \brief Update mCurrentDvsIndex and set sequence id in internal storage.
     */
    void updateDvsResult(long sequence);

    /**
     * \brief Get the pointer of DvsResult to internal storage.
     *
     * The function will return the latest DVS result.
     *
     * return the latest dvs result.
     */
    const DvsResult* getDvsResult();
    // INTEL_DVS_E

    /**
     * \brief Acquire AIQ statistics.
     *
     * The function will return one AIQ statistics pointer which is kept by AIQ statistics decoder.
     * The sequence id is set to -1 which indicates the AIQ statistics is invalid.
     *
     * return AIQ statistics pointer to be kept by AIQ statistics decoder..
     */
    AiqStatistics* acquireAiqStatistics();

    /**
     * \brief Update mCurrentAiqStatsIndex and set sequence id in internal storage.
     */
    void updateAiqStatistics(long sequence);

    /**
     * \brief Get the pointer of AIQ statistics to internal storage.
     *
     * The function will return the latest AIQ statistics, and set the mInUse flag to true.
     *
     * return the latest AIQ statistics.
     */
    const AiqStatistics* getAndLockAiqStatistics();

    /**
     * \brief Clear the mInUse flag of all the AIQ statitics in internal storage.
     */
    void unLockAiqStatistics();

    /**
     * DVS statistics storage.
     * A pair of {pointer to ia_dvs_statistics, sequence} is stored.
     * The function updateDvsStatistics() is called by PipeExecutor, while getDvsStatistics()
     * called by IntelDvs. They are called in the same thread on PSys statistics available.
     */
    /**
     * \brief Update the dvs statistics in internal storage.
     */
    void updateDvsStatistics(const DvsStatistics &dvsStats) { mDvsStatistics = dvsStats; }
    /**
     * \brief Get the pointer of dvs statistics to internal storage.
     */
    DvsStatistics* getDvsStatistics() { return &mDvsStatistics; }

    /**
     * LTM statistics storage.
     * A pair of {pointer to ia_isp_bxt_hdr_yv_grid_t, sequence} is stored.
     * The function updateLtmStatistics() is called by PipeExecutor, while getLtmStatistics()
     * called by Ltm. They are called in the same thread on PSys statistics available.
     */
    /**
     * \brief Update the ltm statistics in internal storage.
     */
    void updateLtmStatistics(const LtmStatistics &ltmStats) { mLtmStatistics = ltmStats; }
    /**
     * \brief Get the pointer of ltm statistics to internal storage.
     */
    LtmStatistics* getLtmStatistics() { return &mLtmStatistics; }

private:
    AiqResultStorage(int cameraId);
    ~AiqResultStorage();

    static AiqResultStorage* getInstanceLocked(int cameraId);

private:
    static std::map<int, AiqResultStorage*> sInstances;
    // Guard for singleton creation.
    static Mutex sLock;

    int mCameraId;
    RWLock mDataLock;   // lock for all the data storage below

    static const int kStorageSize = 20; // Should > MAX_BUFFER_COUNT + sensorLag
    int mCurrentIndex = -1;
    AiqResult* mAiqResults[kStorageSize];

    // LOCAL_TONEMAP_S
    static const int kLtmStorageSize = 10; //For per frame LTM, should > MAX_BUFFER_COUNT + ltmLag
    int mCurrentLtmIndex = -1;
    ltm_result_t mLtmResult[kLtmStorageSize];

    static const int kLtmTuningStorageSize = 2; // Always return the latest, 2 instances are enough.
    int mCurrentLtmTuningIndex = -1;
    ltm_tuning_data mLtmTuningData[kLtmTuningStorageSize];
    // LOCAL_TONEMAP_E

    // INTEL_DVS_S
    static const int kDvsStorageSize = 2; // DVS only be used to run AIC, so 2 instances are enough.
    int mCurrentDvsIndex = -1;
    DvsResult* mDvsResults[kDvsStorageSize];
    // INTEL_DVS_E

    static const int kAiqStatsStorageSize = 3; // Always use the latest, but may hold for long time
    int mCurrentAiqStatsIndex = -1;
    AiqStatistics mAiqStatistics[kAiqStatsStorageSize];

    DvsStatistics mDvsStatistics;
    LtmStatistics mLtmStatistics;
};

} //namespace icamera
#endif //__AIQ_RESULT_STORAGE__

