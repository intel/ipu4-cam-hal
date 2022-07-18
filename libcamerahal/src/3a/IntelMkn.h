/*
 * Copyright (C) 2018 Intel Corporation.
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

#include <map>
#include <list>

#include "ia_mkn_types.h"
#include "ia_mkn_encoder.h"

#include "iutils/Utils.h"
#include "iutils/Thread.h"

#include "Parameters.h"

namespace icamera {

struct MakernoteData {
    long sequence;
    unsigned int size;
    char section[MAKERNOTE_SECTION1_SIZE + MAKERNOTE_SECTION2_SIZE];

    MakernoteData() {
        sequence = -1;
        size = 0;
        CLEAR(section);
    }
};

/**
 * \class IntelMkn
 *
 * This class encapsulates Intel Makernotes function, and provides interface
 * for enabling and acquiring Makenotes which is called by AiqEngine, Ltm
 * and AiqPlus.
 *
 * It's a singleton based on camera id, and its life cycle can  be maintained
 * by its static methods getInstance and releaseIntelMkn.
 */
class IntelMkn {
public:
    /**
     * \brief Get instance for cameraId.
     *
     * param[in] int camera id.
     *
     * return the instance of IntelMkn for cameraId.
     */
    static IntelMkn* getInstance(int cameraId);

    /**
     * \brief Release the static instance of IntelMkn for cameraId.
     */
    static void releaseIntelMkn(int cameraId);

    /**
     * \brief acquire Makernote data.
     *
     * param[in] long sequence: the sequence in frame buffer;
     * param[out] param: Makernote data will be saved in Parameters as metadata.
     *
     * return OK if acquire Makernote successfully, otherwise return ERROR.
     */
    int acquireMakernoteData(long sequence, Parameters *param);

    /**
     * \brief Save Makernote by ia_mkn_trg mode
     *
     * param[in] camera_makernote_mode_t: MAKERNOTE_MODE_JPEG is corresponding
     *           to ia_mkn_trg_section_1 for Normal Jpeg capture;
     *           MAKERNOTE_MODE_RAW is corresponding to ia_mkn_trg_section_2
     *           for Raw image capture.
     * param[in] long sequence: the sequence in latest AiqResult
     *
     * return OK if get Makernote successfully, otherwise return ERROR.
     */
    int saveMakernoteData(camera_makernote_mode_t makernoteMode, long sequence);

    /**
     * \brief Get ia_mkn (Makernote) handle.
     */
    ia_mkn *getMknHandle();
private:
    IntelMkn();
    ~IntelMkn();

    static IntelMkn* getInstanceLocked(int cameraId);

    /**
     * \brief Initialize and enable Intel Makernote.
     */
    int init();

    /**
     * \brief Deinitialize Intel Makernote.
     */
    void deinit();

private:
    static const int MAX_MAKER_NOTE_LIST_SIZE = 10; // Should > max request number in processing

    enum MknState {
        UNINIT,
        INIT
    } mMknState;

    static std::map<int, IntelMkn*> sInstances;
    // Guard for singleton creation
    static Mutex sLock;

    // Guard for IntelMkn API
    Mutex mMknLock;
    ia_mkn *mMkn;

    std::list<MakernoteData*> mMakernoteDataList;
};

} // namespace icamera
