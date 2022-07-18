/*
 * INTEL CONFIDENTIAL
 * Copyright (c) 2017-2018 Intel Corporation. All Rights Reserved.
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

#ifndef _CISU_I_LOG_H_
#define _CISU_I_LOG_H_

#include <utility>
#include <ia_cipr/ia_cipr_log.h>

namespace cisu {

/** \addtogroup cisu-logging
 *  @{
 */

void initIaLog();
void deinitIaLog();
void cisuLogOsSpecificInit();

enum CisuLogScope {
    CISU_LOG_SCOPE_ALL         = -1,
    CISU_LOG_SCOPE_3A          = (1 << 0),
    CISU_LOG_SCOPE_SENSOR      = (1 << 1),
    CISU_LOG_SCOPE_PIPELINE    = (1 << 2),
    CISU_LOG_SCOPE_3A_INTERNAL = (1 << 3), /* "internal" really means logs from CCA */
    CISU_LOG_SCOPE_REQUEST     = (1 << 4)
};

#ifdef RNDBUILD
ia_cipr_log_level_t levelForScope(int /*CisuLogScope bits*/ scope, ia_cipr_log_level_t level);

#define CISU_SCOPED_LOG_TAG(level, scope, tag, ...) \
    _CSS_DEBUG(levelForScope(scope, level), __VA_ARGS__);

#define CISU_SCOPED_LOG(level, scope, ...) \
    _CSS_DEBUG(levelForScope(scope, level), __VA_ARGS__);

#define CISU_SCOPED_LOGD(scope, ...) \
    _CSS_DEBUG(levelForScope(scope, IA_CSS_LOG_LEVEL_D), __VA_ARGS__);

#define CISU_SCOPED_LOGV(scope, ...) \
    _CSS_DEBUG(levelForScope(scope, IA_CSS_LOG_LEVEL_V), __VA_ARGS__);

#define CISU_SCOPED_VLOG(level, scope, fmt, args) \
    _CSS_DEBUG_VFMT(levelForScope(scope, level), fmt, args)

#define CISU_LOG(level, ...) \
    _CSS_DEBUG(level, __VA_ARGS__);

#define CISU_LOGD(...) \
    _CSS_DEBUG(IA_CSS_LOG_LEVEL_D, __VA_ARGS__);

#define CISU_LOGW(...) \
    _CSS_DEBUG(IA_CSS_LOG_LEVEL_WARN, __VA_ARGS__);

#define CISU_LOGE(...) \
    _CSS_DEBUG(IA_CSS_LOG_LEVEL_ERR, __VA_ARGS__);

#define CISU_LOGV(...) \
    _CSS_DEBUG(IA_CSS_LOG_LEVEL_V, __VA_ARGS__);

#else
#define CISU_SCOPED_LOG_TAG(level, scope, tag, ...) ((void)0)
#define CISU_SCOPED_LOG(level, scope, ...) ((void)0)
#define CISU_SCOPED_LOGD(scope, ...) ((void)0)
#define CISU_SCOPED_LOGV(scope, ...) ((void)0)
#define CISU_SCOPED_VLOG(level, scope, fmt, args) ((void)0)
#define CISU_LOG(level, ...) ((void)0)
#define CISU_LOGD(...) ((void)0)
#define CISU_LOGW(...) ((void)0)
#define CISU_LOGE(...) ((void)0)
#define CISU_LOGV(...) ((void)0)
#endif

/**
 * \class CisuScopedTrace
 *
 * Objects of this class produce logs on construction and destruction.
 * They are good to get traces in a given scope.
 */
class CisuScopedTrace {
public:
    inline CisuScopedTrace(ia_cipr_log_level_t level,
                           int32_t scope,
                           const char *tag,
                           const char *name) :
                           mLevel(level),
                           mScope(scope),
                           mTag(tag),
                           mName(name)
    {
        CISU_SCOPED_LOG_TAG(mLevel, mScope, mTag, "ENTER-%s", mName);
    }

    inline ~CisuScopedTrace()
    {
        CISU_SCOPED_LOG_TAG(mLevel, mScope, mTag, "EXIT-%s", mName);
    }

private:
    ia_cipr_log_level_t mLevel;
    int32_t mScope;
    const char *mTag;
    const char *mName;
};

#ifdef RNDBUILD
#define CISU_TRACE_NAME(level, scope, name) \
    cisu::CisuScopedTrace __scopedTracer(level, scope, LIBIACSS_TAG, name)
#define CISU_TRACE_CALLD(scope) \
    CISU_TRACE_NAME(IA_CSS_LOG_LEVEL_D, scope, __PRETTY_FUNCTION__)
#define CISU_TRACE_CALLV(scope) \
    CISU_TRACE_NAME(IA_CSS_LOG_LEVEL_V, scope, __PRETTY_FUNCTION__)
#else
#define CISU_TRACE_NAME(...) ((void)0)
#define CISU_TRACE_CALLD(...) ((void)0)
#define CISU_TRACE_CALLV(...) ((void)0)
#endif

/** @} */
} // namespace cisu

#endif /* _CISU_I_LOG_H_ */
