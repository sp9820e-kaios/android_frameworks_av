/*
 * Copyright (C) 2009 The Android Open Source Project
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
#define LOG_TAG "MediaExtractor"
#include <utils/Log.h>

#include "include/AMRExtractor.h"
#include "include/MP3Extractor.h"
#include "include/MPEG4Extractor.h"
#include "include/WAVExtractor.h"
#include "include/OggExtractor.h"
#include "include/MPEG2PSExtractor.h"
#include "include/MPEG2TSExtractor.h"
#include "include/DRMExtractor.h"
#include "include/WVMExtractor.h"
#include "include/FLACExtractor.h"
#include "include/AACExtractor.h"
#include "include/MidiExtractor.h"
#include "include/AVIExtractor.h"
#include "include/FLVExtractor.h"

#include "matroska/MatroskaExtractor.h"

#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MetaData.h>
#include <utils/String8.h>
#include <media/stagefright/MP3FileSource.h>

namespace android {

sp<MetaData> MediaExtractor::getMetaData() {
    return new MetaData;
}

uint32_t MediaExtractor::flags() const {
    return CAN_SEEK_BACKWARD | CAN_SEEK_FORWARD | CAN_PAUSE | CAN_SEEK;
}

// static
sp<MediaExtractor> MediaExtractor::Create(
        const sp<DataSource> &source, const char *mime) {
    sp<AMessage> meta;
    String8 tmp;
    if (mime == NULL) {
        float confidence;
        if (!source->sniff(&tmp, &confidence, &meta)) {
            ALOGV("FAILED to autodetect media content.");
            return NULL;
        }

        mime = tmp.string();
        ALOGV("Autodetected media content as '%s' with confidence %.2f",
             mime, confidence);
    }

    bool isDrm = false;
    // DRM MIME type syntax is "drm+type+original" where
    // type is "es_based" or "container_based" and
    // original is the content's cleartext MIME type
    if (!strncmp(mime, "drm+", 4)) {
        const char *originalMime = strchr(mime+4, '+');
        if (originalMime == NULL) {
            // second + not found
            return NULL;
        }
        ++originalMime;
        if (!strncmp(mime, "drm+es_based+", 13)) {
            // DRMExtractor sets container metadata kKeyIsDRM to 1
            return new DRMExtractor(source, originalMime);
        } else if (!strncmp(mime, "drm+container_based+", 20)) {
            mime = originalMime;
            isDrm = true;
        } else {
            return NULL;
        }
    }
    MediaExtractor *ret = NULL;
    if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_MPEG4)
            || !strcasecmp(mime, "audio/mp4")) {
        ret = new MPEG4Extractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_MPEG) ||
            !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_BP3) ||
            !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_MP3) ||
            !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_X_MP3) ||
            !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_MPEG3) ||
            !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_MPG3)) {
#ifdef MP3FILESOURCE
        int fd = 0;
        int64_t offset = 0;
        int64_t length = 0;
        source->getFd(&fd,&offset);
        source->getSize(&length);
        //ALOGD("fd:%d,length:%lld,offset:%lld",fd,(long long)length,(long long)offset);
        if(fd > 0 && !isDrm)
            ret = new MP3Extractor(new MP3FileSource(fd, offset, length), meta);
        else
            ret = new MP3Extractor(source, meta);
#else
         ret = new MP3Extractor(source, meta);
#endif
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AMR_NB)
            || !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AMR_WB)
            || !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AMR)) {
        if(isDrm) {
            int fd;
            int64_t offset;
            source->getFd(&fd,&offset);
            if(fd != -1) {
               char header[9];
               int length = source->readAt(0, header, sizeof(header));
               lseek64(fd,offset,SEEK_SET);
               if (length == sizeof(header)) {
                  if((memcmp(header, "#!AMR\n", 6)!=0)&&(memcmp(header, "#!AMR-WB\n", 9)!=0)){
                    ret = new MPEG4Extractor(source);
                  }
               }
            }
        }
        if(!ret)
            ret = new AMRExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_FLAC)) {
        ret = new FLACExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_WAV) ||
    		   !strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_WAV2)) {
        ret = new WAVExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_OGG) ||
    		   !strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_OGG2)) {
        ret = new OggExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_MATROSKA) ||
           !strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_WEBM)) {
        ret = new MatroskaExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_MPEG2TS)) {
        ret = new MPEG2TSExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_AVI) ||
                    !strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_MSVIDEO)) {
        ret = new AVIExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_WVM)) {
        // Return now.  WVExtractor should not have the DrmFlag set in the block below.
        return new WVMExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_FLV)) {
        ret = new FLVExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AAC_ADTS) ||
                   !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AAC2) ||
                   !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AAC3)) {
        ret = new AACExtractor(source, meta);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_MPEG2PS)) {
        ret = new MPEG2PSExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_MIDI) ||
                   !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_MIDI1)) {
        ret = new MidiExtractor(source);
    } else {
	if (isDrm) {
	    ret = new MPEG4Extractor(source);
	}
    }

    if (ret != NULL) {
       if (isDrm) {
           ret->setDrmFlag(true);
       } else {
           ret->setDrmFlag(false);
       }
    }

    return ret;
}

}  // namespace android
