
#ifndef FMHALSOURCE_H
#define FMHALSOURCE_H


#include <stdint.h>
#include <sys/types.h>
#include <limits.h>

#include <utils/Atomic.h>
#include <utils/Errors.h>
#include <utils/threads.h>

#include <utils/SortedVector.h>
#include <utils/Vector.h>
#include <utils/KeyedVector.h>
#include <utils/RefBase.h>


namespace android {

class RingBuffer
{

public:

    struct Buffer {
        void *mData;
        size_t mSize;
    };

    RingBuffer(size_t size);
    ~RingBuffer();

    int32_t init();

    int getBuf( Buffer * buf, uint32_t wait);

    void putData(Buffer *buf,int wake);

    int getData(Buffer * buf ,uint32_t wait);

    void putBuf(Buffer *buf, int wake);

    size_t dataCount();

    void reset();

    size_t dataRelease(size_t size);

    int dataCopy(uint32_t offset, Buffer *buf, int wait);

    void wakeUpForGetBuf();
    void  wakeUpForDataCopy();
    void  wakeUpForGetData();
    void  cleanNoMoreData();
    void  wakeUpAndSetNoMoreData();
private:
    bool isFull();
    bool isEmpty();

    uint8_t * buf_addr;
    size_t rd_index;
    size_t wr_index;
    size_t  mSize;
    int getbuf_wakeup;
    int getdata_wakeup;
    int no_more_data;
    int datacopy_wakeup;
    mutable     Mutex                   mLock;
    mutable     Mutex                   mLock_rd;
    mutable     Mutex                   mLock_wr;
    Mutex mDataLock;
    Condition mDataCond;
    Mutex mBufLock;
    Condition mBufCond;


};


class FileCache : public Thread
{
public:
    enum type_t {
        NOP,
        READ,
        SEEK,
        STOP
    };

    enum state_t {
        OK = 0 ,
        SEEK_ERROR = -1
    };

    FileCache(int mFd , int64_t length, size_t cacheSize, size_t maxPerRead,size_t preCahceSize);
     ~FileCache();
    int32_t    init();
    virtual status_t    readyToRun() { return NO_ERROR; }
    virtual bool        threadLoop();
    void  wakeUpForThreadLoop();
    void      exit();

    ssize_t readAt(off64_t offset, void *data, size_t size);

private:
    void commandProcess();
    void seekTo(off64_t offset);
    int mFd;
    int64_t mOffset;
    int64_t mLength;

    size_t mCacheSize;
    size_t mPreCacheSize;

    int mCommand;
    int mState;
    int errorCount;
    bool mThreadLoop_wakeup;

    mutable     Mutex       mLock;
    Condition               mWaitCond;

    mutable     Mutex       mLock_thread;
    Condition               mCond_thread;

    uint32_t                mMaxPerRead;
    int  count;
    static int mcount;
    class RingBuffer *      mRingBuffer;
};

}
#endif


