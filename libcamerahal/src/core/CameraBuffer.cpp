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

#define LOG_TAG "CameraBuffer"

#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "iutils/CameraLog.h"
#include "iutils/Utils.h"

#include "PlatformData.h"
#include "CameraBuffer.h"
#include "V4l2Dev.h"

namespace icamera {
CameraBuffer::CameraBuffer(int cameraId, int usage, int memory, uint32_t size, int index, int format) :
    mAllocatedMemory(false),
    mU(nullptr),
    mBufferUsage(usage),
    mSettingSequence(-1)
{
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int num_plane = 1;

    LOG1("%s: construct CameraBuffer with cameraId:%d, usage:%d, memory:%d, size:%d, format:%d, index:%d",
         __func__, cameraId, usage, memory, size, format, index);

    mU = new camera_buffer_t;
    CLEAR(*mU);
    mU->flags = BUFFER_FLAG_INTERNAL;

    switch (usage) {
        case BUFFER_USAGE_PSYS_INPUT:
            //follow through
        case BUFFER_USAGE_GENERAL:
            if (PlatformData::isCSIFrontEndCapture(cameraId)) {
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                num_plane = CameraUtils::getNumOfPlanes(format);
            } else {
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            }
            break;
        case BUFFER_USAGE_ISYS_STATS:
            type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            num_plane = 2;
            break;
        case BUFFER_USAGE_PSYS_STATS:
        case BUFFER_USAGE_ISA_CAPTURE:
            type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            break;
        case BUFFER_USAGE_ISA_PARAM:
            type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            num_plane = 2;
            break;
        case BUFFER_USAGE_MIPI_CAPTURE:
        case BUFFER_USAGE_METADATA:
            type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            num_plane = CameraUtils::getNumOfPlanes(format);
            break;
        default:
            LOGE("Not supported Usage");
    }

    mV.init(memory, type, size, index, num_plane);
}

CameraBuffer::~CameraBuffer()
{
    LOG1("Free CameraBuffer");
    freeMemory();
    if (mU->flags & BUFFER_FLAG_INTERNAL) {
        delete mU;
    }
}

//Helper function to construct a Internal CameraBuffer
shared_ptr<CameraBuffer> CameraBuffer::create(int cameraId, int usage, int memory, unsigned int size, int index,
                                              int srcFmt, int srcWidth, int srcHeight)
{
    shared_ptr<CameraBuffer> camBuffer = make_shared<CameraBuffer>(cameraId, usage, memory, size, index, srcFmt);

    Check(!camBuffer, nullptr, "@%s: fail to alloc CameraBuffer", __func__);

    camBuffer->setUserBufferInfo(srcFmt, srcWidth, srcHeight);

    int ret = camBuffer->allocateMemory();
    if (ret != OK) {
        LOGE("Allocate memory failed ret %d", ret);
        return nullptr;
    }

    return camBuffer;
}

//Internal frame Buffer
void CameraBuffer::setUserBufferInfo(int format, int width, int height)
{
    LOG1("%s: format:%d, width:%d, height:%d", __func__, format, width, height);
    mU->s.width = width;
    mU->s.height = height;
    mU->s.format = format;
    if (format != -1) {
        mU->s.stride = CameraUtils::getStride(format, width);
    }
}

//Called when a buffer is from the application
void CameraBuffer::setUserBufferInfo(camera_buffer_t *ubuffer)
{
    Check(ubuffer == nullptr, VOID_VALUE, "%s: ubuffer is nullptr", __func__);

    if (mU->flags & BUFFER_FLAG_INTERNAL) delete mU;
    mU = ubuffer;

    LOG1("%s: ubuffer->s.MemType: %d, addr: %p, fd: %d", __func__, ubuffer->s.memType,
         ubuffer->addr, ubuffer->dmafd);
    //update the v4l2 buffer memory with user infro
    switch (ubuffer->s.memType) {
        case V4L2_MEMORY_USERPTR:
            mV.setAddr(ubuffer->addr, 0);
            break;
        case V4L2_MEMORY_DMABUF:
            mV._fd(0) = ubuffer->dmafd; //MPLANE is not supported by user buffer
            break;
        case V4L2_MEMORY_MMAP:
            /* do nothing */
            break;
        default:
            LOGE("iomode %d is not supported yet.", mV.memory);
            break;
    }
}

void CameraBuffer::updateV4l2Buffer(const v4l2_buffer_t& v4l2buf)
{
    mV.field = v4l2buf.field;
    mV.timestamp = v4l2buf.timestamp;
    mV.sequence = v4l2buf.sequence;
    mV.reserved = v4l2buf.reserved;
}

/*export mmap buffer as dma_buf fd stored in mV and mU*/
int CameraBuffer::exportMmapDmabuf(V4l2Dev *vDevice)
{
    for (unsigned int i=0; i < mV.numPlanes(); i++) {
        mV._fd(i) = vDevice->exportDmaBuf(mV, i);
        Check(mV._fd(i) < 0, -1, "failed to export DmaBuffer.");
    }

    if (mU->flags & BUFFER_FLAG_DMA_EXPORT)
        mU->dmafd = mV._fd(0);

    return OK;
}

int CameraBuffer::allocateMemory(V4l2Dev *vDevice)
{
    int ret = BAD_VALUE;
    LOG1("%s", __func__);
    switch(mV.memory) {
        case V4L2_MEMORY_USERPTR:
            ret = mV.allocateUserPtr();
            mAllocatedMemory = true;
            mU->addr=mV.getAddr();
            break;
        case V4L2_MEMORY_MMAP:
            exportMmapDmabuf(vDevice);
            ret = mV.allocateMmap(vDevice->getDevFd());
            mU->addr=mV.getAddr();
            mAllocatedMemory = true;
            break;
        default:
            LOGE("memory type %d is incorrect for allocateMemory.", mV.memory);
            return BAD_VALUE;
    }

    return ret;
}

void* CameraBuffer::mapDmaBufferAddr(int fd, unsigned int bufferSize)
{
    if(fd < 0 || !bufferSize) {
        LOGE("%s, fd:0x%x, bufferSize:%u", __func__, fd, bufferSize);
        return nullptr;
    }
    return mmap(nullptr, bufferSize, PROT_READ, MAP_SHARED, fd, 0);
}

void CameraBuffer::unmapDmaBufferAddr(void* addr, unsigned int bufferSize)
{
    if(addr == nullptr || !bufferSize) {
        LOGE("%s, addr:%p, bufferSize:%u", __func__, addr, bufferSize);
        return;
    }
    munmap(addr, bufferSize);
}

void CameraBuffer::freeMemory()
{
    if (!mAllocatedMemory) {
        LOG2("@%s Memory(in %p) is not allocated by CameraBuffer class. Don't free it.", __func__, this);
        return ;
    }

    switch(mV.memory) {
        case V4L2_MEMORY_USERPTR:
            mV.freeUserPtr();
        break;
        case V4L2_MEMORY_MMAP:
            mV.freeMmap();
        break;
        default:
            LOGE("Free camera buffer failed, due to memory %d type is not implemented yet.", mV.memory);
    }
}

void CameraBuffer::updateUserBuffer(void)
{
    mU->sequence = getSequence();
    mU->timestamp = getTimestamp().tv_sec * 1000000000ULL;
    mU->timestamp += getTimestamp().tv_usec * 1000ULL;
    mU->s.field = getField();
}

void CameraBuffer::updateFlags(void)
{
    int flag = V4L2_BUF_FLAG_NO_CACHE_INVALIDATE | V4L2_BUF_FLAG_NO_CACHE_CLEAN;
    bool set = true;

    //clear the flags if the buffers is accessed by the SW
    if ((mU->flags & BUFFER_FLAG_SW_READ) || (mU->flags & BUFFER_FLAG_SW_WRITE)) {
        set = false;
    }

    mV.setFlags(flag, set);
}

bool CameraBuffer::isFlagsSet(int flag)
{
    return ((mU->flags & flag) ? true : false);
}

}//namespace icamera
