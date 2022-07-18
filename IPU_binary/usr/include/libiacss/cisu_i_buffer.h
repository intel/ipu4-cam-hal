/*
 * INTEL CONFIDENTIAL
 * Copyright (c) 2018 Intel Corporation. All Rights Reserved.
 *
 * The source code contained or described herein and all documents related to
 * the source code ("Material") are owned by Intel Corporation or its suppliers
 * or licensors. Title to the Material remains with Intel Corporation or its
 * suppliers and licensors. The Material contains trade secrets and proprietary
 * and confidential information of Intel or its suppliers and licensors. The
 * Material is protected by worldwide copyright and trade secret laws and
 * treaty provisions. No part of the Material may be used, copied, reproduced,
 * modified, published, uploaded, posted, transmitted, distributed, or
 * disclosed in any way without Intel's prior express written permission.
 *
 * No license under any patent, copyright, trade secret or other intellectual
 * property right is granted to or conferred upon you by disclosure or delivery
 * of the Materials, either expressly, by implication, inducement, estoppel or
 * otherwise. Any license under such intellectual property rights must be
 * express and approved by Intel in writing.
 */
#ifndef LIBCISU_INCLUDE_CISU_I_BUFFER_H_
#define LIBCISU_INCLUDE_CISU_I_BUFFER_H_

#include <cisu_types.h>
#include <gcss_formats.h>
#include <memory>
#include <cisu_dll.h>

namespace cisu {

//forward declaration
class ICisuBuffer;
class ICisuStream;

/** \addtogroup cisu_buffer
 *  @{
 */

/**
 * \enum CisuMemoryFlags
 * Memory bitfield definitions to specify type, allocation and usage
 */
enum CisuMemoryFlags {
    /**
     * memory type: CPU Pointer
     */
    CISU_MEMORY_CPU_PTR =              (uint32_t)(1 << 0),
    /**
     * memory type: OS specific handle
     */
    CISU_MEMORY_HANDLE =               (uint32_t)(1 << 1),
    /**
     * Memory operations: allocate from heap
     */
    CISU_MEMORY_ALLOCATE_CPU_PTR  =    (uint32_t)(1 << 2),
    /**
     * Memory access : used by HW only
     */
    CISU_MEMORY_HW_ONLY =              (uint32_t)(1 << 3),
    /**
     * Memory access : SW only reads
     */
    CISU_MEMORY_READ_ONLY =            (uint32_t)(1 << 4),
    /**
     * Memory access : SW only writes
     */
    CISU_MEMORY_WRITE_ONLY =           (uint32_t)(1 << 5),
    /**
     * Memory properties : un-cached
     */
    CISU_MEMORY_UNCACHED =             (uint32_t)(1 << 6),
    /**
     * Memory properties : uninitialized after allocation
     */
    CISU_MEMORY_ALLOC_UNINITIALIZED =  (uint32_t)(1 << 7),
    /**
     * Memory state: allocated
     */
    CISU_MEMORY_ALLOCATED =            (uint32_t)(1 << 8),
    /**
     * Memory state: taken from user
     */
    CISU_MEMORY_FROM_USER =            (uint32_t)(1 << 9),
    /**
     * Memory state: migrated
     */
    CISU_MEMORY_MIGRATED =             (uint32_t)(1 << 10)
};

/**
 * \struct CisuMemory
 *
 *
 * Generic memory abstraction
 */

struct CisuMemory {
    size_t size;                /**< size in bytes */
    void    *cpu_ptr;           /**< host pointer to allocated data */
    uint64_t handle;            /**< implementation defined handle to
                                     allocated data */
    uint32_t flags;             /**< bitfield specifying allocation
                                     and usage information \sa CisuMemoryFlags */
    std::shared_ptr<ICisuBuffer> anchor;    /**< anchor to origin buffer object */

    CisuMemory() :
        size(0),
        cpu_ptr(0),
        handle(0),
        flags(0) {}
};

/**
 * \struct BufferGeometry
 *
 *
 * Definition of buffer geometry in pixels and lines.
 */

struct BufferGeometry {
    int32_t width;      /**< width in pixels */
    int32_t height;     /**< height in lines */
    int32_t stride;     /**< stride in pixels */
};

/**
 * \class CisuBuffer
 *
 * Buffer type enumeration for poor man's RTTI.
 */
enum CisuBufferType {
    CISU_BUFFER_TYPE_BUFFER,
    CISU_BUFFER_TYPE_IMAGE_BUFFER
};

/**
 * \class CisuBuffer
 *
 *
 * Generic buffer abstraction. A buffer holds a memory region.
 * A buffer object can be created empty via the default constructor or from
 * an existing memory region.
 * This class offers the capability to allocate the memory given a set of memory
 * requirements.
 */
class CISU_EXPORT_TO_DLL ICisuBuffer {
public:
    virtual CisuError allocate(CisuMemory &memRequirement) = 0;
    virtual CisuError getHandle(uint64_t &handle) const = 0;
    virtual size_t    getSize()  const = 0;
    virtual uint32_t  getFlags() const = 0;
    virtual void*     getCpuPtr() const = 0;
    virtual CisuBufferType getType() const = 0;
    virtual void destroy() = 0;
};

/**
 * \class CisuImageBuffer
 *
 *
 * Memory buffer abstraction that holds an image of a given geometry and format
 *
 */
class CISU_EXPORT_TO_DLL ICisuImageBuffer : public ICisuBuffer {
public:
    virtual CisuError initCheck() = 0;
    virtual CisuError allocateImage(CisuMemory &memRequirement,
                            BufferGeometry &geo,
                            const GCSSFormats::ia_format_t &format) = 0;
    /* getters */
    virtual const BufferGeometry& getGeometry() const = 0;
    virtual const GCSSFormats::ia_format_t& getFormat() const = 0;

    /* Support for stream buffers */
    virtual CisuError assignToStream(const ICisuStream *stream) = 0;
    virtual const ICisuStream * getStream() const = 0;
    virtual void setTimeStamp(uint64_t timeStamp) = 0;
    virtual uint64_t getTimeStamp() const = 0;
    virtual CisuBufferType getType() const = 0;
};

extern "C" CISU_EXPORT_TO_DLL CisuError CISU_DLL_API CisuImageBufferAllocator(size_t capacity, std::vector<ICisuImageBuffer*> &pointers);

/** @}*/
} // namespace cisu
#endif /* LIBCISU_INCLUDE_CISU_I_BUFFER_H_ */
