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

extern "C" {
#include <linux/videodev2.h>
}

using namespace std;

typedef struct v4l2_buffer v4l2_buffer_t;

namespace icamera {

/* V4l2Buffer is an abstraction of the driver v4l2 buffer
 * It provides some convenient functions to
    * access the internal v4l2 field
    * userptr and mmap memory allocation
 */
class V4l2Buffer: public v4l2_buffer_t {
//CameraBuffer could access the private field of us
    friend class CameraBuffer;
public:
    static void dump(struct v4l2_buffer &vbuf, const char *func_name, const char *devName = "Anonymous");
    //This is used to construct memory mapped mplane buffer
    V4l2Buffer();
    virtual ~V4l2Buffer();

public:
    //init should be called ealier than other function.
    void init(int memType, v4l2_buf_type bufType, uint32_t size, int idx, int num_plane);
    unsigned int numPlanes() { return mNumPlanes; }

    //Memory Management
    void* getAddr(int plane = 0);
    void setAddr(void *userAddr, int plane = 0);
    int allocateUserPtr();
    int allocateMmap(int mapFd);
    void freeUserPtr();
    void freeMmap();
    void setFlags(int flag, bool set);

private:
    //Function to access the v4l2 field in MPLAN and normal case
    unsigned int & _length(int plane);
    unsigned int & _bytesused(int plane);
    unsigned int & _offset(int plane);
    unsigned long & _userptr(int plane);
    int & _fd(int plane);

private:
    V4l2Buffer(const V4l2Buffer&);
    V4l2Buffer& operator=(const V4l2Buffer&);

private:
    //The v4l2 buffer planes.
    struct v4l2_plane   mPlanes[VIDEO_MAX_PLANES];

    //The mmaped adrress
    void    *addr[VIDEO_MAX_PLANES];
    int     mNumPlanes;

    //The exported dmafd for mmap.
    int dmafd[VIDEO_MAX_PLANES];
};

} //namespace icamera
