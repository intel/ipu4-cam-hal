/*
 * Copyright (C) 2016-2018 Intel Corporation.
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

namespace icamera {

/**
 * enum type definition
 *
 * It contains NONE, ULL and HDR pipe.
 */
typedef enum {
    CUSTOM_AIC_PIPE_NONE = 0,
    CUSTOM_AIC_PIPE_ULL,
    CUSTOM_AIC_PIPE_HDR
} CustomAicPipe;

/**
 * struct type definition Custom Aic Param
 */
typedef struct {
    void* data;        /*!< The pointer of the parameter data. */
    int length;        /*!< The data length of the parameter. */
} CustomAicParam;

/* The Macro is defined Custom Aic Library and as a symbol to find struct CustomAicModule */
#define CUSTOMIZE_AIC_MODULE_INFO_SYM CAMI

/* string is used to get the address of the struct CustomAicModule. */
#define CUSTOMIZE_AIC_MODULE_INFO_SYM_AS_STR "CAMI"

} /* namespace icamera */
