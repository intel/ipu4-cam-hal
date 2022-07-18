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

#include "api/Parameters.h"
#include "CameraBuffer.h"
#include "IspSettings.h"

namespace icamera {

/*
 * The class for handling sensor optical black data
 */
class SensorOB {

public:
    SensorOB(int cameraId);
    ~SensorOB();
    /**
     * run optical black value calculation based on frame OB section
     *
     * \param configMode:config mode
     * \param frameBuf: frame buffer
     * \param ispSettings: output isp settings
     * \return int: status
     */
    int runOB(ConfigMode configMode, const shared_ptr<CameraBuffer> frameBuf,
              IspSettings* ispSettings);

private:
    int mCameraId;
    ia_ob *mOB;
};
}
