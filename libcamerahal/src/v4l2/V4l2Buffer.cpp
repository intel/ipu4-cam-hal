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

#define LOG_TAG "V4l2Buffer"

#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

#include "iutils/CameraLog.h"
#include "iutils/Errors.h"
#include "iutils/Utils.h"

#include "SysCall.h"
#include "V4l2Buffer.h"

namespace icamera {
V4l2Buffer::V4l2Buffer():
    mNumPlanes(1)
{
    v4l2_buffer_t *t = (v4l2_buffer_t *)this;
    CLEAR(*t);
    memset(mPlanes, 0, sizeof(struct v4l2_plane) * VIDEO_MAX_PLANES);
    memset(addr, 0, sizeof(void *) * VIDEO_MAX_PLANES);
    //Default skip the cache flush
    flags |= V4L2_BUF_FLAG_NO_CACHE_INVALIDATE | V4L2_BUF_FLAG_NO_CACHE_CLEAN;
}

V4l2Buffer::~V4l2Buffer()
{
}

void V4l2Buffer::init(int memType, v4l2_buf_type bufType, uint32_t size, int idx, int num_plane)
{
    memory = memType;
    type = bufType;
    length = size;
    index = idx;
    mNumPlanes = num_plane;
    if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
        //in mplane, the length filed is the number of planes instead of buffer size
        length = num_plane;
        m.planes = mPlanes;
    }

    for (int i = 0; i < mNumPlanes; i++) {
        _length(i) = size;
    }
}

unsigned int & V4l2Buffer::_length(int plane)
{
    Check(plane < 0 || plane >= mNumPlanes, length, "Wrong plane number %d", plane);
    if (plane < 0 || plane >= mNumPlanes) {
        LOGE("%s: plane %d is outof range, using plane 0", __func__, plane);
        plane = 0;
    }
    if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
        return m.planes[plane].length;
    } else {
        return length;
    }
}

unsigned int & V4l2Buffer::_bytesused(int plane)
{
    Check(plane < 0 || plane >= mNumPlanes, bytesused, "Wrong plane number %d", plane);
    if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
        return m.planes[plane].bytesused;
    } else {
        return bytesused;
    }
}

unsigned int & V4l2Buffer::_offset(int plane)
{
    Check(plane < 0 || plane >= mNumPlanes, m.offset, "Wrong plane number %d", plane);
    if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
        return m.planes[plane].m.mem_offset;
    } else {
        return m.offset;
    }
}

unsigned long & V4l2Buffer::_userptr(int plane)
{
    Check(plane < 0 || plane >= mNumPlanes, m.userptr, "Wrong plane number %d", plane);
    if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
        return m.planes[plane].m.userptr;
    } else {
        return m.userptr;
    }
}

int & V4l2Buffer::_fd(int plane)
{
    Check(plane < 0 || plane >= mNumPlanes, m.fd, "Wrong plane number %d", plane);
    Check(memory == V4L2_MEMORY_USERPTR, m.fd, "Wrong memory %d", memory);

    if (memory == V4L2_MEMORY_MMAP)
        return dmafd[plane];
    return V4L2_TYPE_IS_MULTIPLANAR(type) ? m.planes[plane].m.fd : m.fd;
}

void* V4l2Buffer::getAddr(int plane)
{
    Check(plane < 0 || plane >= mNumPlanes, nullptr, "Wrong plane number %d", plane);
    switch (memory) {
        case V4L2_MEMORY_MMAP:
            return addr[plane];
        case V4L2_MEMORY_USERPTR:
            return (void *)_userptr(plane);
        default:
            LOGE("%s: Not supported memory type %u", __func__, memory);
    }
    return nullptr;
}

void V4l2Buffer::setAddr(void *userAddr, int plane)
{
    Check(plane < 0 || plane >= mNumPlanes, VOID_VALUE, "Wrong plane number %d", plane);
    switch (memory) {
        case V4L2_MEMORY_MMAP:
            addr[plane] = userAddr;
            return ;
        case V4L2_MEMORY_USERPTR:
            _userptr(plane) = (unsigned long)userAddr;
            return ;
        default:
            LOGE("%s: Not supported memory type %u", __func__, memory);
    }
}

int V4l2Buffer::allocateUserPtr()
{
    void *buf = nullptr;
    for (int i = 0; i < mNumPlanes; i++) {
        int ret = posix_memalign(&buf, getpagesize(), _length(i));
        Check(ret != 0, -1, "%s, posix_memalign fails, ret:%d", __func__, ret);
        _userptr(i) = (unsigned long)buf;
        addr[i] = buf;
    }
    return OK;
}

void V4l2Buffer::freeUserPtr()
{
    for (int i = 0; i < mNumPlanes; i++) {
        free((void *)_userptr(i));
        _userptr(i) = 0;
    }
}

int V4l2Buffer::allocateMmap(int mapFd)
{
    for (int i = 0; i < mNumPlanes; i++) {
        void *buf = SysCall::getInstance()->mmap(nullptr, _length(i), PROT_READ | PROT_WRITE, MAP_SHARED, mapFd, _offset(i));
        Check(buf == MAP_FAILED, -1, "Failed to MMAP the buffer %s", strerror(errno));
        addr[i] = buf;
        LOG2("%s: mmap addr: %p, length: %u, offset: %u, plane:%d", __func__, buf, _length(i), _offset(i), i);
    }

    return OK;
}

void V4l2Buffer::freeMmap()
{
    int ret = OK;
    for (int i = 0; i < mNumPlanes; i++) {
        // Exported as dmabuf fd, so close this fd before munmap.
        close(dmafd[i]);
        dmafd[i] = -1;
        ret = SysCall::getInstance()->munmap(addr[i], _length(i));
        addr[i] = nullptr;
        Check (ret != 0, VOID_VALUE, "failed to munmap buffer %d", i);
    }
}

void V4l2Buffer::setFlags(int flag, bool set)
{
    flags = set ? (flags | flag) : (flags & (~flag));
}

void V4l2Buffer::dump(struct v4l2_buffer &vbuf, const char* func_name, const char *devName)
{
    if (!Log::isDebugLevelEnable(CAMERA_DEBUG_LOG_LEVEL2)) {
        return;
    }

    unsigned long userptr;
    int fd;
    unsigned long num_plane = 1;
    unsigned int length, offset;
    if (V4L2_TYPE_IS_MULTIPLANAR(vbuf.type)) {
        num_plane = vbuf.length;
    }

    for (unsigned long i = 0; i < num_plane; i++) {
        if (V4L2_TYPE_IS_MULTIPLANAR(vbuf.type)) {
            userptr = vbuf.m.planes[i].m.userptr;
            fd = vbuf.m.planes[i].m.fd;
            offset = vbuf.m.planes[i].m.mem_offset;
            length = vbuf.m.planes[i].length;
        } else {
            userptr = vbuf.m.userptr;
            fd = vbuf.m.fd;
            offset = vbuf.m.offset;
            length = vbuf.length;
        }

        LOG2("%s@%s with device: %s. index: %u, buf_type: %u, bytesused:%u, flags:%u, field:%u, "
             "sequence: %u, memory:%u, plane %lu dmafd:(%d)/userptr(%p)/offset(0x%X), length: %u",
            __func__, func_name, devName, vbuf.index, vbuf.type, vbuf.bytesused, vbuf.flags,
            vbuf.field, vbuf.sequence, vbuf.memory, i, fd, (void *)userptr, offset, length);
    }
}

} //namespace icamera
