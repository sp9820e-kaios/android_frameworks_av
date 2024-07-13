/*
 * Copyright (C) 2008 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "VibratorTone"
#include <utils/threads.h>

#include <stdio.h>
#include <math.h>
#include <utils/Log.h>
#include <utils/RefBase.h>
#include <utils/Timers.h>
#include <cutils/properties.h>
#include "media/ToneGenerator.h"
#include "media/VibratorTone.h"

namespace android {

//---------------------------------- public methods ----------------------------   
VibratorTone::VibratorTone(audio_stream_type_t streamType, float volume):ToneGenerator(streamType,volume,false){    
};
}

