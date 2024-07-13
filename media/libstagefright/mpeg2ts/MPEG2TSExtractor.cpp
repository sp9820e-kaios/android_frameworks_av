/*
 * Copyright (C) 2010 The Android Open Source Project
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
#define LOG_TAG "MPEG2TSExtractor"

#include <inttypes.h>
#include <utils/Log.h>

#include "include/MPEG2TSExtractor.h"
#include "include/NuCachedSource2.h"

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/IStreamSource.h>
#include <utils/String8.h>

#include <media/stagefright/foundation/ABitReader.h>

#include "AnotherPacketSource.h"
#include "ATSParser.h"

namespace android {

static const size_t kTSPacketSize = 188;

struct MPEG2TSSource : public MediaSource {
    MPEG2TSSource(
            const sp<MPEG2TSExtractor> &extractor,
            const sp<AnotherPacketSource> &impl,
            bool doesSeek);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

private:
    sp<MPEG2TSExtractor> mExtractor;
    sp<AnotherPacketSource> mImpl;

    // If there are both audio and video streams, only the video stream
    // will signal seek on the extractor; otherwise the single stream will seek.
    bool mDoesSeek;

    DISALLOW_EVIL_CONSTRUCTORS(MPEG2TSSource);
};

MPEG2TSSource::MPEG2TSSource(
        const sp<MPEG2TSExtractor> &extractor,
        const sp<AnotherPacketSource> &impl,
        bool doesSeek)
    : mExtractor(extractor),
      mImpl(impl),
      mDoesSeek(doesSeek) {
}

status_t MPEG2TSSource::start(MetaData *params) {
    return mImpl->start(params);
}

status_t MPEG2TSSource::stop() {
    return mImpl->stop();
}

sp<MetaData> MPEG2TSSource::getFormat() {
    return mImpl->getFormat();
}

status_t MPEG2TSSource::read(
        MediaBuffer **out, const ReadOptions *options) {
    *out = NULL;

    int64_t seekTimeUs;
    ReadOptions::SeekMode seekMode;
    if (mDoesSeek && options && options->getSeekTo(&seekTimeUs, &seekMode)) {
        // seek is needed
        status_t err = mExtractor->seek(seekTimeUs, seekMode);
        if (err != OK) {
            return err;
        }
    }

    if (mExtractor->feedUntilBufferAvailable(mImpl) != OK) {
        return ERROR_END_OF_STREAM;
    }

    return mImpl->read(out, options);
}

////////////////////////////////////////////////////////////////////////////////

MPEG2TSExtractor::MPEG2TSExtractor(const sp<DataSource> &source)
    : mDataSource(source),
      mParser(new ATSParser),
      mOffset(0),
      mEstimateDuration(-1) {
    init();
}

size_t MPEG2TSExtractor::countTracks() {
    return mSourceImpls.size();
}

sp<MediaSource> MPEG2TSExtractor::getTrack(size_t index) {
    if (index >= mSourceImpls.size()) {
        return NULL;
    }

    // The seek reference track (video if present; audio otherwise) performs
    // seek requests, while other tracks ignore requests.
    return new MPEG2TSSource(this, mSourceImpls.editItemAt(index),
            (mSeekSyncPoints == &mSyncPoints.editItemAt(index)));
}

sp<MetaData> MPEG2TSExtractor::getTrackMetaData(
        size_t index, uint32_t /* flags */) {
    return index < mSourceImpls.size()
        ? mSourceImpls.editItemAt(index)->getFormat() : NULL;
}

sp<MetaData> MPEG2TSExtractor::getMetaData() {
    sp<MetaData> meta = new MetaData;
    meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_CONTAINER_MPEG2TS);

    return meta;
}


void MPEG2TSExtractor::init() {
    bool haveAudio = false;
    bool haveVideo = false;
    int64_t startTime = ALooper::GetNowUs();

    while (feedMore() == OK) {
        if (haveAudio && haveVideo) {
            break;
        }
        if (!haveVideo) {
            sp<AnotherPacketSource> impl =
                (AnotherPacketSource *)mParser->getSource(
                        ATSParser::VIDEO).get();

            if (impl != NULL) {
                haveVideo = true;
                mSourceImpls.push(impl);
                mSyncPoints.push();
                mSeekSyncPoints = &mSyncPoints.editTop();
            }
        }

        if (!haveAudio) {
            sp<AnotherPacketSource> impl =
                (AnotherPacketSource *)mParser->getSource(
                        ATSParser::AUDIO).get();
	    /*sprd add we now can not support AC-3*/
	    if(impl != NULL) {
                sp<MetaData> meta = impl->getFormat();
                const char *mime;
                CHECK(meta->findCString(kKeyMIMEType, &mime));
                if (!strncasecmp("audio/ac3", mime, 9)) {
                    impl = NULL;
                }
	    }

            if (impl != NULL) {
                haveAudio = true;
                mSourceImpls.push(impl);
                mSyncPoints.push();
                if (!haveVideo) {
                    mSeekSyncPoints = &mSyncPoints.editTop();
                }
            }
        }

        // Wait only for 2 seconds to detect audio/video streams.
        if (ALooper::GetNowUs() - startTime > 2000000ll) {
            break;
        }
    }

    off64_t size;
    if (mDataSource->getSize(&size) == OK && (haveAudio || haveVideo)) {
        sp<AnotherPacketSource> impl = haveVideo
                ? (AnotherPacketSource *)mParser->getSource(
                        ATSParser::VIDEO).get()
                : (AnotherPacketSource *)mParser->getSource(
                        ATSParser::AUDIO).get();
         size_t prevSyncSize = 1;
        int64_t durationUs = -1;
        List<int64_t> durations;
        // Estimate duration --- stabilize until you get <500ms deviation.
        while (feedMore() == OK
                && ALooper::GetNowUs() - startTime <= 3000000ll) {

                if (mSeekSyncPoints->size() > prevSyncSize) {
                    prevSyncSize = mSeekSyncPoints->size();
                    int64_t diffUs = mSeekSyncPoints->keyAt(prevSyncSize - 1)
                            - mSeekSyncPoints->keyAt(0);
                    off64_t diffOffset = mSeekSyncPoints->valueAt(prevSyncSize - 1)
                            - mSeekSyncPoints->valueAt(0);
                    durationUs = size * diffUs / diffOffset;
                    durations.push_back(durationUs);
                    if (durations.size() > 5) {
                         durations.erase(durations.begin());
                         int64_t min = *durations.begin();
                         int64_t max = *durations.begin();
                         for (List<int64_t>::iterator i = durations.begin();
                                 i != durations.end();
                                 ++i) {
                             if (min > *i) {
                                 min = *i;
                             }
                             if (max < *i) {
                                 max = *i;
                             }
                         }
                         if (max - min < 500 * 1000) {
                             break;
                         }
                 }
           }
        }

    ALOGI(" android calculate durationUs=%" PRId64, durationUs);

    if(durationUs < 0){
        while (feedMore() == OK
                && ALooper::GetNowUs() - startTime <= 3000000ll) {
            if(mEstimateDuration < 0 ){
                    int64_t end_PTS = estimateDuration(true);
                    ALOGD("end_PTS = %f", end_PTS / 90000.0);
                    int64_t start_PTS = estimateDuration(false);
                    ALOGD("start_PTS = %f", start_PTS / 90000.0);
                    durationUs = end_PTS - start_PTS;
                    mEstimateDuration = durationUs;
            }else{
               durationUs = mEstimateDuration;
            }
            durations.push_back(durationUs);
            break;
	}
    }

        status_t err;
        int64_t bufferedDurationUs;
        bufferedDurationUs = impl->getBufferedDurationUs(&err);
        if (err == ERROR_END_OF_STREAM) {
            durationUs = bufferedDurationUs;
        }
        if (durationUs > 0) {
            const sp<MetaData> meta = impl->getFormat();
            meta->setInt64(kKeyDuration, durationUs);
            impl->setFormat(meta);
        }
    }

    ALOGI("haveAudio=%d, haveVideo=%d, elaspedTime=%" PRId64,
            haveAudio, haveVideo, ALooper::GetNowUs() - startTime);
}

/* sprd add for Bug543653 {@ */
int64_t MPEG2TSExtractor::estimateDuration(bool flip) {
    Mutex::Autolock autoLock(mLock);
    int64_t  filesize;
    int64_t  offset = 0;
    uint64_t PTS = 0;
    int retry=1;
    status_t ret = mDataSource->getSize(&filesize);
    if (ret != OK) {
        ALOGE("Failed to get file size");
        return ERROR_MALFORMED;
    }
    uint8_t  packet[kTSPacketSize];
    unsigned payload_unit_start_indicator = 0;
    unsigned PID = 0;
    unsigned adaptation_field_control = 0;
    int64_t startTime = ALooper::GetNowUs();
    while (ALooper::GetNowUs() - startTime < 4000000ll) {
        if(flip == true){
            ALOGV("-------Calculate end_PTS-------");
            offset = filesize - kTSPacketSize*retry;
        }else{
            offset += kTSPacketSize;
            ALOGV("-------Calculate start_PTS-------");
        }
        ssize_t n = mDataSource->readAt(offset, packet, kTSPacketSize);
        if (n < (ssize_t)kTSPacketSize) {
            return (n < 0) ? (status_t)n : ERROR_END_OF_STREAM;
        }
        ABitReader* br = new ABitReader((const uint8_t *)packet, kTSPacketSize);
        unsigned sync_byte = br->getBits(8);
        CHECK_EQ(sync_byte, 0x47u);
        sync_byte++;
        br->skipBits(1);
        payload_unit_start_indicator = br->getBits(1);
        br->skipBits(1);
        PID = br->getBits(13);
        br->skipBits(2);
        adaptation_field_control = br->getBits(2);
        ALOGV("%s %d payload_unit_start_indicator = %d", __FUNCTION__, __LINE__,
            payload_unit_start_indicator);
        ALOGV("%s %d adaptation_field_control = %d PID = %d", __FUNCTION__, __LINE__,
            adaptation_field_control,PID);
        if ((payload_unit_start_indicator == 1) && ((adaptation_field_control == 1) || (adaptation_field_control == 3)) &&
            (PID != 0x00u) && (PID != 0x01u) && (PID != 0x02u)&& (PID != 0x11u)) {
            delete br;
            br = new ABitReader((const uint8_t *)packet, kTSPacketSize);
            br->skipBits(8 + 3 + 13);
            br->skipBits(2);
            adaptation_field_control = br->getBits(2);
            ALOGV("%s %d adaptation_field_control = %u",  __FUNCTION__, __LINE__, adaptation_field_control);
            br->skipBits(4);
            if (adaptation_field_control == 2 || adaptation_field_control == 3) {
               unsigned adaptation_field_length = br->getBits(8);
               if (adaptation_field_length > 0) {
                   br->skipBits(adaptation_field_length * 8);
               }
               ALOGV("%s %d adaptation_field_length = %u",  __FUNCTION__, __LINE__, adaptation_field_length);
            }
            if (adaptation_field_control == 1 || adaptation_field_control == 3) {
                unsigned packet_start_code_prefix = br->getBits(24);

                ALOGV("%s %d packet_start_code_prefix = 0x%08x",  __FUNCTION__, __LINE__, packet_start_code_prefix);

                if(packet_start_code_prefix != 0x000001u){
                    delete br;
                    ALOGV("packet_start_code_prefix = %d [Warning Not equal 0x000001u]",packet_start_code_prefix);
                    continue;
                }

                unsigned stream_id = br->getBits(8);
                ALOGV("%s %d stream_id = 0x%02x",  __FUNCTION__, __LINE__, stream_id);
                br->skipBits(16);

                if (stream_id != 0xbc            // program_stream_map
                        && stream_id != 0xbe     // padding_stream
                        && stream_id != 0xbf     // private_stream_2
                        && stream_id != 0xf0     // ECM
                        && stream_id != 0xf1     // EMM
                        && stream_id != 0xff     // program_stream_directory
                        && stream_id != 0xf2     // DSMCC
                        && stream_id != 0xf8) {  // H.222.1 type E
                    CHECK_EQ(br->getBits(2), 2u);
                    br->skipBits(6);
                    unsigned PTS_DTS_flags = br->getBits(2);
                    ALOGV("%s %d PTS_DTS_flags = %u",  __FUNCTION__, __LINE__, PTS_DTS_flags);
                    br->skipBits(6);

                    unsigned PES_header_data_length = br->getBits(8);

                    unsigned optional_bytes_remaining = PES_header_data_length;

                    if (PTS_DTS_flags == 2 || PTS_DTS_flags == 3) {
                        ALOGV("%s %d optional_bytes_remaining = %x", __FUNCTION__, __LINE__, optional_bytes_remaining);
                        CHECK_GE(optional_bytes_remaining, 5u);
                        CHECK_EQ(br->getBits(4), PTS_DTS_flags);
                        PTS = ((uint64_t)br->getBits(3)) << 30;
                        CHECK_EQ(br->getBits(1), 1u);

                        PTS |= ((uint64_t)br->getBits(15)) << 15;
                        CHECK_EQ(br->getBits(1), 1u);

                        PTS |= br->getBits(15);
                        CHECK_EQ(br->getBits(1), 1u);

                        ALOGV("%s %d PTS = %.2f secs",  __FUNCTION__, __LINE__, PTS / 90000.0f);
                        PTS = ((PTS / 90000.0f)) * 90000.0f;
                        PTS = (PTS * 1000 * 1000ll) / 90000;
                    }
                }
            }

            if (PTS > 0){
                delete br;
                break;
            }
        }
        retry++;
        delete br;
    }
    return PTS;
}
/* @} */

status_t MPEG2TSExtractor::feedMore() {
    Mutex::Autolock autoLock(mLock);

    uint8_t packet[kTSPacketSize];
    ssize_t n = mDataSource->readAt(mOffset, packet, kTSPacketSize);

    if (n < (ssize_t)kTSPacketSize) {
        if (n >= 0) {
            mParser->signalEOS(ERROR_END_OF_STREAM);
        }
        return (n < 0) ? (status_t)n : ERROR_END_OF_STREAM;
    }

    ATSParser::SyncEvent event(mOffset);
    mOffset += n;
    status_t err = mParser->feedTSPacket(packet, kTSPacketSize, &event);
    if (event.isInit()) {
        for (size_t i = 0; i < mSourceImpls.size(); ++i) {
            if (mSourceImpls[i].get() == event.getMediaSource().get()) {
                KeyedVector<int64_t, off64_t> *syncPoints = &mSyncPoints.editItemAt(i);
                syncPoints->add(event.getTimeUs(), event.getOffset());
                // We're keeping the size of the sync points at most 5mb per a track.
                size_t size = syncPoints->size();
                if (size >= 327680) {
                    int64_t firstTimeUs = syncPoints->keyAt(0);
                    int64_t lastTimeUs = syncPoints->keyAt(size - 1);
                    if (event.getTimeUs() - firstTimeUs > lastTimeUs - event.getTimeUs()) {
                        syncPoints->removeItemsAt(0, 4096);
                    } else {
                        syncPoints->removeItemsAt(size - 4096, 4096);
                    }
                }
                break;
            }
        }
    }
    return err;
}

uint32_t MPEG2TSExtractor::flags() const {
    return CAN_PAUSE | CAN_SEEK_BACKWARD | CAN_SEEK_FORWARD;
}

status_t MPEG2TSExtractor::seek(int64_t seekTimeUs,
        const MediaSource::ReadOptions::SeekMode &seekMode) {
    if (mSeekSyncPoints == NULL || mSeekSyncPoints->isEmpty()) {
        ALOGW("No sync point to seek to.");
        // ... and therefore we have nothing useful to do here.
        return OK;
    }

    // Determine whether we're seeking beyond the known area.
    bool shouldSeekBeyond =
            (seekTimeUs > mSeekSyncPoints->keyAt(mSeekSyncPoints->size() - 1));

    // Determine the sync point to seek.
    size_t index = 0;
    for (; index < mSeekSyncPoints->size(); ++index) {
        int64_t timeUs = mSeekSyncPoints->keyAt(index);
        if (timeUs > seekTimeUs) {
            break;
        }
    }

    switch (seekMode) {
        case MediaSource::ReadOptions::SEEK_NEXT_SYNC:
            if (index == mSeekSyncPoints->size()) {
                ALOGW("Next sync not found; starting from the latest sync.");
                --index;
            }
            break;
        case MediaSource::ReadOptions::SEEK_CLOSEST_SYNC:
        case MediaSource::ReadOptions::SEEK_CLOSEST:
            ALOGW("seekMode not supported: %d; falling back to PREVIOUS_SYNC",
                    seekMode);
            // fall-through
        case MediaSource::ReadOptions::SEEK_PREVIOUS_SYNC:
            if (index == 0) {
                ALOGW("Previous sync not found; starting from the earliest "
                        "sync.");
            } else {
                --index;
            }
            break;
    }
    if (!shouldSeekBeyond || mOffset <= mSeekSyncPoints->valueAt(index)) {
        int64_t actualSeekTimeUs = mSeekSyncPoints->keyAt(index);
        mOffset = mSeekSyncPoints->valueAt(index);
        status_t err = queueDiscontinuityForSeek(actualSeekTimeUs);
        if (err != OK) {
            return err;
        }
    }

    if (shouldSeekBeyond) {
        status_t err = seekBeyond(seekTimeUs);
        if (err != OK) {
            return err;
        }
    }

    // Fast-forward to sync frame.
    for (size_t i = 0; i < mSourceImpls.size(); ++i) {
        const sp<AnotherPacketSource> &impl = mSourceImpls[i];
        status_t err;
        feedUntilBufferAvailable(impl);
        while (impl->hasBufferAvailable(&err)) {
            sp<AMessage> meta = impl->getMetaAfterLastDequeued(0);
            sp<ABuffer> buffer;
            if (meta == NULL) {
                return UNKNOWN_ERROR;
            }
            int32_t sync;
            if (meta->findInt32("isSync", &sync) && sync) {
                break;
            }
            err = impl->dequeueAccessUnit(&buffer);
            if (err != OK) {
                return err;
            }
            feedUntilBufferAvailable(impl);
        }
    }

    return OK;
}

status_t MPEG2TSExtractor::queueDiscontinuityForSeek(int64_t actualSeekTimeUs) {
    // Signal discontinuity
    sp<AMessage> extra(new AMessage);
    extra->setInt64(IStreamListener::kKeyMediaTimeUs, actualSeekTimeUs);
    mParser->signalDiscontinuity(ATSParser::DISCONTINUITY_TIME, extra);

    // After discontinuity, impl should only have discontinuities
    // with the last being what we queued. Dequeue them all here.
    for (size_t i = 0; i < mSourceImpls.size(); ++i) {
        const sp<AnotherPacketSource> &impl = mSourceImpls.itemAt(i);
        sp<ABuffer> buffer;
        status_t err;
        while (impl->hasBufferAvailable(&err)) {
            if (err != OK) {
                return err;
            }
            err = impl->dequeueAccessUnit(&buffer);
            // If the source contains anything but discontinuity, that's
            // a programming mistake.
            CHECK(err == INFO_DISCONTINUITY);
        }
    }

    // Feed until we have a buffer for each source.
    for (size_t i = 0; i < mSourceImpls.size(); ++i) {
        const sp<AnotherPacketSource> &impl = mSourceImpls.itemAt(i);
        sp<ABuffer> buffer;
        status_t err = feedUntilBufferAvailable(impl);
        if (err != OK) {
            return err;
        }
    }

    return OK;
}

status_t MPEG2TSExtractor::seekBeyond(int64_t seekTimeUs) {
    // If we're seeking beyond where we know --- read until we reach there.
    size_t syncPointsSize = mSeekSyncPoints->size();

    while (seekTimeUs > mSeekSyncPoints->keyAt(
            mSeekSyncPoints->size() - 1)) {
        status_t err;
        if (syncPointsSize < mSeekSyncPoints->size()) {
            syncPointsSize = mSeekSyncPoints->size();
            int64_t syncTimeUs = mSeekSyncPoints->keyAt(syncPointsSize - 1);
            // Dequeue buffers before sync point in order to avoid too much
            // cache building up.
            sp<ABuffer> buffer;
            for (size_t i = 0; i < mSourceImpls.size(); ++i) {
                const sp<AnotherPacketSource> &impl = mSourceImpls[i];
                int64_t timeUs;
                while ((err = impl->nextBufferTime(&timeUs)) == OK) {
                    if (timeUs < syncTimeUs) {
                        impl->dequeueAccessUnit(&buffer);
                    } else {
                        break;
                    }
                }
                if (err != OK && err != -EWOULDBLOCK) {
                    return err;
                }
            }
        }
        if (feedMore() != OK) {
            return ERROR_END_OF_STREAM;
        }
    }

    return OK;
}

status_t MPEG2TSExtractor::feedUntilBufferAvailable(
        const sp<AnotherPacketSource> &impl) {
    status_t finalResult;
    while (!impl->hasBufferAvailable(&finalResult)) {
        if (finalResult != OK) {
            return finalResult;
        }

        status_t err = feedMore();
        if (err != OK) {
            impl->signalEOS(err);
        }
    }
    return OK;
}

////////////////////////////////////////////////////////////////////////////////

bool SniffMPEG2TS(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *) {
    for (int i = 0; i < 5; ++i) {
        char header;
        if (source->readAt(kTSPacketSize * i, &header, 1) != 1
                || header != 0x47) {
            return false;
        }
    }

    *confidence = 0.1f;
    mimeType->setTo(MEDIA_MIMETYPE_CONTAINER_MPEG2TS);

    return true;
}

}  // namespace android
