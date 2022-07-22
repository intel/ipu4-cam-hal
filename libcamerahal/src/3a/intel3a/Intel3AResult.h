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

#include "ia_aiq.h"

namespace icamera {

/*
 * \class Intel3AResult
 * This class is used to do result conversion and result dumping.
 * It is an example for third party 3A.
 */
class Intel3AResult {

public:
    Intel3AResult();
    ~Intel3AResult();

    int deepCopyAeResults(const ia_aiq_ae_results &src, ia_aiq_ae_results *dst);
    int deepCopyAfResults(const ia_aiq_af_results &src, ia_aiq_af_results *dst);
    int deepCopyAwbResults(const ia_aiq_awb_results &src, ia_aiq_awb_results *dst);

private:
    int dumpAeResults(const ia_aiq_ae_results &aeResult);
    int dumpAfResults(const ia_aiq_af_results &afResult);
    int dumpAwbResults(const ia_aiq_awb_results &awbResult);

};

} /* namespace icamera */
