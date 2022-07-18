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

#include <memory>
#include <queue>
#include <vector>

#include <linux/videodev2.h>

#include "api/Parameters.h"
#include "V4l2Buffer.h"

using namespace std;
namespace icamera {

class V4l2Dev;

/* CameraBuffer is the core buffers for HAL. The buffer usage is described by the
 * BufferUsage. CameraBuffer are constructed based on usage */
enum BufferUsage {
    BUFFER_USAGE_GENERAL = 0,
    BUFFER_USAGE_ISYS_STATS,
    BUFFER_USAGE_PSYS_STATS,
    BUFFER_USAGE_PSYS_INPUT,
    BUFFER_USAGE_ISA_CAPTURE,
    BUFFER_USAGE_ISA_PARAM,
    BUFFER_USAGE_MIPI_CAPTURE,
    BUFFER_USAGE_METADATA
};

class CameraBuffer {
public:
    //assist function to create frame buffers
    static shared_ptr<CameraBuffer> create(int cameraId, int usage, int memory, unsigned int size, int index,
                                           int srcFmt = -1, int srcWidth=-1, int srcHeight=-1);

public:
    CameraBuffer(int cameraId, int usage, int memory, uint32_t size, int index, int format = -1);
    virtual ~CameraBuffer();

public:
    //user buffer information
    int getWidth() const {return mU->s.width;}
    int getHeight() const {return mU->s.height;}
    int getStride() const {return mU->s.stride;}
    int getFormat() const {return mU->s.format;}
    int getFlags() const {return mU->flags;}

    //v4l2 buffer information
    int getIndex(void) const {return (int)mV.index;}
    long getSequence(void) const {return mV.sequence;}
    int getField() const {return mV.field;}
    struct timeval getTimestamp(void) const  {return mV.timestamp; }
    int getFd(int planeIndex = 0) {return mV._fd(planeIndex);}
    int getMemory(void) const { return mV.memory; }
    unsigned int numPlanes() { return mV.numPlanes(); }
    //For debug only v4l2 buffer information
    int getCsi2Port(void) const {return (int)((mV.reserved >> 4) & 0xf);}
    int getVirtualChannel(void) const {return (int)(mV.reserved & 0xf);}

    /* u buffer is used to attach user private structure pointer
     * in CameraBuffer.
     *
     * Now, one of this usage is linking camera_buffer_t to CameraBuffer
     * together, so that we can get each pointer by other.
     * Notes: Please don't abuse this. It is only used in CameraDevice for user buffer
     */
    camera_buffer_t *getUserBuffer() {return mU;}
    //update the user  buffer with latest v4l2 buffer info from driver
    void    updateUserBuffer(void);
    //Update the v4l2 flags according to user buffer flag
    void    updateFlags(void);
    //Check if the specific flag in "mU->flags" is set or not
    bool isFlagsSet(int flag);
    //The ubuffer is from application
    void setUserBufferInfo(camera_buffer_t *ubuffer);
    void setUserBufferInfo(int format, int width, int height);

    unsigned int getBufferSize(int planeIndex = 0) { return mV._length(planeIndex); }
    void setBufferSize(unsigned int size, int planeIndex = 0) { mV._length(planeIndex) = size; }

    unsigned int getBytesused(int planeIndex = 0) { return mV._bytesused(planeIndex); }
    void setBytesused(unsigned int bytes, int planeIndex = 0) { mV._bytesused(planeIndex) = bytes; }

    void* getBufferAddr(int planeIndex = 0) { return mV.getAddr(planeIndex); }
    void  setBufferAddr(void *addr, int planeIndex = 0) { return mV.setAddr(addr, planeIndex); }

    void updateV4l2Buffer(const v4l2_buffer_t& v4l2buf);
    v4l2_buffer_t& getV4l2Buffer() { return mV; }

    int getUsage() const {return mBufferUsage;}
    void setSettingSequence(long sequence) { mSettingSequence = sequence; }
    long getSettingSequence() const { return mSettingSequence; }

    //Buffers are allocated the buffers by Camera
    int allocateMemory(V4l2Dev *vDevice = nullptr);

public:
    static void* mapDmaBufferAddr(int fd, unsigned int bufferSize);
    static void unmapDmaBufferAddr(void* addr, unsigned int bufferSize);

private:
    CameraBuffer(const CameraBuffer&);
    CameraBuffer& operator=(const CameraBuffer&);

    void freeMemory();
    int exportMmapDmabuf(V4l2Dev *vDevice);

    //this could be accessed by CaptureUnit and TestBuffer
protected:
    V4l2Buffer mV;

private:
    //To tag whether the memory is allocated by CameraBuffer class. We need to free them
    bool mAllocatedMemory;

    camera_buffer_t *mU;
    int mBufferUsage;
    long mSettingSequence;
};

typedef vector<shared_ptr<CameraBuffer> > CameraBufVector;
typedef queue<shared_ptr<CameraBuffer> > CameraBufQ;

}
