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

#define LOG_TAG "IntelMkn"

#include "IntelMkn.h"

#include "iutils/Errors.h"
#include "iutils/CameraLog.h"

namespace icamera {

map<int, IntelMkn*> IntelMkn::sInstances;
Mutex IntelMkn::sLock;

IntelMkn* IntelMkn::getInstance(int cameraId)
{
    AutoMutex lock(sLock);
    return getInstanceLocked(cameraId);
}

void IntelMkn::releaseIntelMkn(int cameraId)
{
    AutoMutex lock(sLock);
    IntelMkn* note = getInstanceLocked(cameraId);

    sInstances.erase(cameraId);
    delete note;
}

IntelMkn* IntelMkn::getInstanceLocked(int cameraId)
{
    if (sInstances.find(cameraId) != sInstances.end()) {
        return sInstances[cameraId];
    }

    sInstances[cameraId] = new IntelMkn();
    LOG1("%s create IntelMkn instance %d", __func__, cameraId);
    return sInstances[cameraId];
}

IntelMkn::IntelMkn() :
    mMknState(UNINIT),
    mMkn(nullptr)
{
    LOG1("%s", __func__);

    init();
}

IntelMkn::~IntelMkn()
{
    LOG1("%s", __func__);

    deinit();
}

int IntelMkn::init()
{
    LOG1("%s", __func__);
    AutoMutex lock(mMknLock);
    Check(mMknState != UNINIT, BAD_VALUE, "mkn has initialized");

    mMkn = ia_mkn_init(ia_mkn_cfg_compression,
                      MAKERNOTE_SECTION1_SIZE,
                      MAKERNOTE_SECTION2_SIZE);
    Check(mMkn == nullptr, UNKNOWN_ERROR, "init mkn failed");

    ia_err ret = ia_mkn_enable(mMkn, true);
    Check(ret != ia_err_none, ret, "failed to enable mkn ret %d", ret);

    for (int i = 0; i < MAX_MAKER_NOTE_LIST_SIZE; i++) {
        MakernoteData *data = new MakernoteData;
        mMakernoteDataList.push_back(data);
    }

    mMknState = INIT;
    return OK;
}

void IntelMkn::deinit()
{
    LOG1("%s", __func__);
    AutoMutex lock(mMknLock);

    if (mMkn) {
        ia_mkn_uninit(mMkn);
        mMkn = nullptr;
    }

    for (auto item : mMakernoteDataList) {
        delete item;
    }
    mMakernoteDataList.clear();

    mMknState = UNINIT;
}

int IntelMkn::acquireMakernoteData(long sequence, Parameters *param)
{
    LOG1("%s", __func__);
    AutoMutex lock(mMknLock);
    Check(mMknState != INIT, BAD_VALUE, "mkn isn't initialized");
    Check(param == nullptr, BAD_VALUE, "param pointer is nullptr");

    for (auto rit = mMakernoteDataList.rbegin(); rit != mMakernoteDataList.rend(); ++rit) {
        if ((*rit)->sequence >= 0 && sequence >= (*rit)->sequence) {
            LOG2("%s, found sequence %ld for request sequence %ld", __func__,
                  (*rit)->sequence, sequence);
            param->setMakernoteData((*rit)->section, (*rit)->size);
            return OK;
        }
    }

    return UNKNOWN_ERROR;
}

int IntelMkn::saveMakernoteData(camera_makernote_mode_t makernoteMode, long sequence)
{
    LOG1("%s", __func__);
    if (makernoteMode == MAKERNOTE_MODE_OFF) return OK;

    AutoMutex lock(mMknLock);
    Check(mMknState != INIT, BAD_VALUE, "mkn isn't initialized");

    ia_mkn_trg mknTrg = (makernoteMode == MAKERNOTE_MODE_JPEG ? ia_mkn_trg_section_1
                                                              : ia_mkn_trg_section_2);
    ia_binary_data makerNote = ia_mkn_prepare(mMkn, mknTrg);
    Check(makerNote.data == nullptr, UNKNOWN_ERROR, "invalid makernote data pointer");
    Check(makerNote.size == 0, UNKNOWN_ERROR, "0 size makernote data saved");

    MakernoteData *data = mMakernoteDataList.front();
    mMakernoteDataList.pop_front();

    MEMCPY_S(data->section, sizeof(char) * (MAKERNOTE_SECTION1_SIZE + MAKERNOTE_SECTION2_SIZE),
             makerNote.data, makerNote.size);

    data->size = makerNote.size;
    data->sequence = sequence;
    LOG2("%s, saved makernote %d for sequence %ld", __func__, makernoteMode, sequence);

    mMakernoteDataList.push_back(data);
    return OK;
}

ia_mkn* IntelMkn::getMknHandle()
{
    LOG1("%s", __func__);
    AutoMutex lock(mMknLock);
    Check(mMknState != INIT, nullptr, "mkn isn't initialized");

    return mMkn;
}

} // namespace icamera
