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

#ifndef ANDROID_VIBRATORTONE_H_
#define ANDROID_VIBRATORTONE_H_

#include <utils/RefBase.h>
#include <utils/KeyedVector.h>
#include <utils/threads.h>
#include <media/AudioSystem.h>
#include <media/AudioTrack.h>
#include "media/ToneGenerator.h"
namespace android {

class VibratorTone:public ToneGenerator,virtual public RefBase{
public:
    VibratorTone(audio_stream_type_t streamType, float volume);
}
;  
} // namespace android
#endif /*ANDROID_VIBRATORTONE_H_*/
