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

#include "Parameters.h"

namespace icamera {

/*
 * \class ParameterGenerator
 * This class is used to generator parameter results. It updates the parameters
 * with AIQ results, sensor embedded metadata and 3A statistics.
 * The parameter results are stored with the frame sequence indicating on which
 * frame the parameters are active.
 */
#ifdef BYPASS_MODE

class ParameterGenerator {

public:
    ParameterGenerator(int cameraId) {UNUSED(cameraId);}
    ~ParameterGenerator() {}

    int reset() {return OK;}
    int saveParameters(long sequence, const Parameters &param) {return OK;}
    int getParameters(long sequence, Parameters *param, bool mergeResultOnly = false,
                      bool still = false) {return OK;}

private:
    DISALLOW_COPY_AND_ASSIGN(ParameterGenerator);

};

#else

class ParameterGenerator {

public:
    ParameterGenerator(int cameraId);
    ~ParameterGenerator();

    /**
     * \brief reset the parameters data.
     */
    int reset();

    /**
     * \brief Save parameters with sequence id indicating the active frame.
     *           And update the aiq result parameters as well.
     */
    int saveParameters(long sequence, const Parameters &param);

    /**
     * \brief Get the parameters for the frame indicated by the sequence id.
     */
    int getParameters(long sequence, Parameters *param, bool mergeResultOnly = false,
                      bool still = false);


private:
    ParameterGenerator(const ParameterGenerator& other);
    ParameterGenerator& operator=(const ParameterGenerator& other);

    int generateParametersL(long sequence, Parameters *params);
    void saveParametersL(long sequence, const Parameters &param);

    int updateWithAiqResultsL(long sequence, Parameters *params);
    // LOCAL_TONEMAP_S
    int updateWithLtmTuningDataL(Parameters *params);
    // LOCAL_TONEMAP_E
    int updateAwbGainsL(Parameters *params, const ia_aiq_awb_results &result);

    int getIndexBySequence(long sequence);

private:
    typedef enum {
        RESULT_TYPE_AIQ = 1,
        RESULT_TYPE_SENSOR_EMD = 1 << 1,
        RESULT_TYPE_STATISTICS = 1 << 2
    } ResultType;

    class UserParams {

    public:
        UserParams() : sequence(-1), user(nullptr) {
            user   = new Parameters();
        }

        ~UserParams() {
            delete user;
        }

        void reset() {
            sequence = -1;
        }

        long sequence;
        Parameters *user;

    private:
        UserParams(const UserParams& other);
        UserParams& operator=(const UserParams& other);
    };

private:
    int mCameraId;
    static const int kStorageSize = 20;

    // Guard for ParameterGenerator public API.
    Mutex mParamsLock;
    int mCurrentIndex;
    UserParams mParameters[kStorageSize];
};

#endif

} /* namespace icamera */
