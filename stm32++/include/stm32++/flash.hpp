#ifndef FLASH_HPP_INCLUDED
#define FLASH_HPP_INCLUDED

#ifdef __arm__
#include <libopencm3/stm32/flash.h>

#define FLASH_LOG(fmtString,...) tprintf("FLASH: " fmtString "\n", ##__VA_ARGS__)

typedef uint32_t Addr;
struct DefaultFlashDriver
{
    class WriteUnlocker
    {
    protected:
        bool isUpperBank;
        bool wasLocked;
        bool isLocked() const { return (FLASH_CR & FLASH_CR_LOCK) != 0; }
        bool isUpperLocked() const { return (FLASH_CR2 & FLASH_CR_LOCK) != 0; }
    public:
        WriteUnlocker(Addr page)
        : isUpperBank((DESIG_FLASH_SIZE > 512) && (page >= FLASH_BASE+0x00080000))
        {
            if (isUpperBank)
            {
                wasLocked = isUpperLocked();
                if (wasLocked) flash_unlock_upper();
            }
            else
            {
                wasLocked = isLocked();
                if (wasLocked) flash_unlock();
            }
            clearStatusFlags();
        }
        ~WriteUnlocker()
        {
            if (isUpperBank)
            {
                if (wasLocked) flash_lock_upper();
            }
            else
            {
                if (wasLocked) flash_lock();
            }
        }
    };
    enum: uint32_t { kFlashWriteErrorFlags = FLASH_SR_WRPRTERR
#ifdef FLASH_SR_PGERR
    | FLASH_SR_PGERR
#endif
#ifdef FLASH_SR_PGAERR
    | FLASH_SR_PGAERR
#endif
#ifdef FLASH_SR_PGPERR
    | FLASH_SR_PGPERR
#endif
#ifdef FLASH_SR_ERSERR
    | FLASH_SR_ERSERR
#endif
    };
    uint16_t pageSize() const
    {
        static const uint16_t flashPageSize = DESIG_FLASH_SIZE << 10;
        return flashPageSize;
    }
    bool write16(Addr addr, uint16_t data)
    {
        assert((addr & 0x1) == 0);
        flash_program_half_word(addr, data);
        return (*(uint16_t*)(addr) == data);
    }
    bool write16Block(Addr dest, const uint16_t* src, uint16_t wordCnt)
    {
        assert((dest & 0x1) == 0);
        assert((src & 0x1) == 0);
        uint16_t* wptr = (uint16_t*)dest;
        uint16_t* end = data + wordCnt;
        for (; data < end; data++, wptr++)
        {
            flash_program_half_word(wptr, *data);
            if (*wptr != *data)
            {
                return false;
            }
        }
        return true;
    }
    void clearStatusFlags()
    {
        flash_clear_status_flags();
    }
    uint32_t errorFlags()
    {
        uint32_t flags = FLASH_SR;
        if (DESIG_FLASH_SIZE > 512)
        {
            flags |= FLASH_SR2;
        }
        return flags & kFlashWriteErrorFlags;
    }
    bool erasePage(Addr pageAddr)
    {
        assert(pageAddr % 4 == 0);
        flash_erase_page(pageAddr);
        auto err = errorFlags();
        if (err)
        {
            FLASH_LOG_ERROR("erasePage: Error flag(s) set after erase: %", fmtBin(err));
            return false;
        }
        uint32_t* pageEnd = (uint32_t*)(pageAddr + pageSize());
        for (uint32_t* ptr = (uint32_t*)pageAddr; ptr < pageEnd; ptr++)
        {
            if (*ptr != 0xffffffff)
            {
                FLASH_LOG_ERROR("erasePage: Page contains a byte that is not 0xff after erase");
                return false;
        }
        return true;
    }
};
#else
// x86, test mode
#define FLASH_LOG(fmtString,...) printf("FLASH: " fmtString "\n", ##__VA_ARGS__)

typedef size_t Addr;
struct DefaultFlashDriver
{
    class WriteUnlocker
    { WriteUnlocker(Addr addr){} };
    enum: uint32_t { kFlashWriteErrorFlags = 0x1 };
    void write16(Addr addr, uint16_t data)
    {
        *((uint16_t*)addr) = data;
    }
    void write16Block(Addr addr, const uint16_t* data, uint16_t wordCnt)
    {
        assert((addr & 0x1) == 0);
        assert((data & 0x1) == 0);
        memcpy((void*)addr, data, wordCnt * 2);
    }
    uint32_t errorFlags() { return 0; }
    void clearStatusFlags() {}
    bool erasePage(Addr addr)
    {
        memset((void*)addr, pageSize(), 0xff);
        return true;
    }
};
#endif

#define FLASH_LOG_ERROR(fmtString,...) FLASH_LOG("ERROR: " fmtString, ##_VA_ARGS__)

template <class FlashDriver>
struct FlashPageInfo
{
    enum { kMagicLen = 6 };
    enum ValidateError: uint8_t {
           kErrNone = 0, kErrMagic = 1,
           kErrCounter = 2, kErrDataEndAlign = 3,
           kErrData = 4
    };
    uint8_t* dataEnd;
    uint16_t pageCtr;
    ValidateError validateError;

    bool isPageValid() const { return validateError == kErrNone; }
    static const char* magic()
    {
        alignas(2) static const char magic[] = "nvstor";
        static_assert(sizeof(magic) == kPageMagicSigLen, "");
        return magic;
    }
    static uint16_t getPageCounter(uint8_t* page)
    {
        return *((uint16_t*)(page + Driver::pageSize() - kPageMagicSigLen - sizeof(uint16_t)));
    }
    /**
     * @brief findDataEnd Finds where the data in a page ends, by scanning the page backwards
     * and finding the last byte with value 0xff
     * @param page The start of the page which to scan
     * @return A pointer to the first byte after the last data entry (first byte with 0xff value)
     * If the pointer is not an even address, then the page content is not valid, and nullptr
     * is returned
     */
    static uint8_t* findDataEnd(uint8_t* page)
    {
        assert((page & 0x1) == 0);
        for (uint8_t* ptr = page + PageSize - 9; ptr >= page; ptr--)
        {
            if (*ptr != 0xff)
            {
                ptr++;
                return (ptr & 0x1) ? nullptr : ptr;
            }
        }
        // page is completely empty
        return page;
    }

    PageInfo(uint8_t* page): pageCtr(getPageCounter(page))
    {
        if (memcmp(page-kMagicLen, kMagicLen, magic()) != 0)
        {
            validateError = kErrMagic;
            return;
        }
        //magic is ok
        dataEnd = findDataEnd(page);
        if (!dataEnd)
        {
            validateError = kErrDataEndAlign;
            return;
        }
        if (pageCtr == 0xffff)
        {
            validateError = kErrCounter;
            return;
        }
        if (!FlashValueStore::verifyAllEntries(dataEnd, page))
        {
            validateError = kErrData;
            return;
        }
        validateError = kErrNone;
    }
};

template<Addr Page1Addr, Addr Page2Addr, Driver=DefaultFlashDriver>
class FlashValueStore
{
protected:
    using PageInfo = FlashPageInfo<Driver>;
    const uint8_t* Page1 = (uint8_t*)Page1Addr;
    const uint8_t* Page2 = (uint8_t*)Page2Addr;

    bool mIsShuttingDown = false;
    uint8_t* mActivePage = nullptr;
    uint8_t* mDataEnd;
    uint16_t mReserveBytes;
public:
    template <typename T>
    static inline T roundToNextEven(T x) { return (x + 1) & (~0x1); }
    FlashValueStore()
    {
        static_assert(Page1Addr % 4 == 0, "Page1 is not on a 32 bit word boundary");
        static_assert(Page2Addr % 4 == 0, "Page2 is not on a 32 bit word boundary");
    }
    bool init(uint16_t reserveBytes=0)
    {
        mReserveBytes = reserveBytes;
        PageInfo info1(Page1);
        PageInfo info2(Page2);
        if (info1.isPageValid)
        {
            if (info2.isPageValid)
            {
                // both contain data, the one with bigger counter wins
                if (info1.pageCtr >= info2.pageCtr)
                {
                    if (info1.pageCtr == info2.pageCtr)
                    {
                        FLASH_LOG_WARNING("FlashValueStore: Both pages have the same erase counter, using page1");
                    }
                    mActivePage = info1.page;
                    mDataEnd = info1.dataEnd;
                }
                else if (info2.pageCtr > info1.pageCtr)
                {
                    mActivePage = info2.page;
                    mDataEnd = info2.dataEnd;
                }
            }
            else // page1 valid, page2 invalid
            {
                mActivePage = info1.page;
                mDataEnd = info1.dataEnd;
            }
        }
        else // page1 invalid
        {
            if (info2.isPageValid)
            {
                mActivePage = info2.page;
                mDataEnd = info2.dataEnd;
            }
            else // both pages invalid
            {
                FLASH_LOG_WARNING("No page is initialized, initializing and using page1");
                Driver::erasePage(info1.page);
                writeMagicAndPageCtr(info1.page, 1);
                mActivePage = mDataEnd = info1.page;
            }
        }
    }
    /**
     * @brief getRawValue
     * @param key - The id of the value
     * @param size - Outputs the size of the returned data.
     * @return
     * - If a value with the specified key was found, returns a pointer to the data and
     * \c size is set to the size of the data
     * - If the value was not found, \c nullptr is returned and \c size is set to zero
     * - If an error occurred during the search, \c nullptr is returned and \c size is set
     * to a nonzero error code
     */
    uint8_t* getRawValue(uint8_t key, uint8_t& size)
    {
        uint8_t* ptr = mDataEnd; // equal to mActivePage if page is empty
        while (ptr > page)
        {
            auto entryKey = *(ptr - 1);
            if (key == entryKey)
            {
                uint8_t len = *(ptr-2);
                return len ? (ptr - 2 - roundToNextEven(len)) : nullptr;
            }
            ptr = getPrevEntryEnd(ptr, mActivePage);
            if (ptr == (uint8_t*)-1)
            {
                size = 1;
                return nullptr;
            }
        }
        size = 0;
        return nullptr;
    }
    uint16_t pageBytesFree() const
    {
        return Driver::pageSize() - (mDataEnd - mActivePage) - kPageMagicSigLen - 2;
    }
    bool setValue(uint8_t key, uint8_t* data, uint8_t len, bool isEmergency=false)
    {
        if (key == 0xff)
        {
            FLASH_LOG_ERROR("setValue: Invalid key 0xff provided");
            return false;
        }
        auto bytesFree = pageBytesFree();
        if (!isEmergency)
        {
            if (mIsShuttingDown)
            {
                FLASH_LOG_ERROR("setValue: Refusing to write, system is shutting down");
                return false;
            }
            bytesFree -= mReserveBytes;
        }
        uint16_t bytesNeeded = 2 + roundToNextEven(len);
        if (bytesNeeded > bytesFree)
        {
            if (!compactPage()) // should log error message
            {
                return false;
            }
            bytesFree = pageBytesFree();
            if (bytesNeeded > bytesFree)
            {
                FLASH_LOG_ERROR("Not enough space to write value even after compacting: available: %, required % bytes", bytesFree, bytesNeeded);
                return false;
            }
        }
        Driver::WriteUnlocker unlocker(mActivePage);
        // data.len [pad.1] len.1 key.1
        bool ok = true;
        // First write the trailer, so that if we are are interrupted while writing
        // the actual length, we can still have the record boundary
        ok &= Driver::write16(mDataEnd + roundToNextEven(len), (key << 8) | len);

        if ((len & 1) == 0) // even number of bytes
        {
            if (len)
            {
                ok &= Driver::write16Block(mDataEnd, data, len / 2);
            }
            mDataEnd += (len + 2);
        }
        else
        { // len is odd
            uint8_t even = len - 1;
            if (even)
            {
                Driver::write16Block(mDataEnd, data, even / 2);
                mDataEnd += even;
            }
            // write last (odd) data byte and a zero padding byte
            ok &= Driver::write16(mDataEnd, data[even]);
            mDataEnd += 2;
        }
        auto err = Driver::errorFlags();
        if (err)
        {
            FLASH_LOG_ERROR("Error writing value: %", fmtHex(err));
            return false;
        }
        return true;
    }
protected:
    /**
     * @brief verifyAllEntries Parses all entries
     * @return false if an error was detected, true otherwise
     */
    static bool verifyAllEntries(uint8_t* dataEnd, uint8_t* page)
    {
        uint8_t* ptr = dataEnd; // equal to mActivePage if page is empty
        while (ptr > page)
        {
            if (*(ptr - 1) == 0xff) // key can't be 0xff
            {
                FLASH_LOG_ERROR("verifyAllEntries: Found a key with value 0xff");
                return false;
            }
            ptr = getPrevEntryEnd(ptr, page);
            if (ptr == (uint8_t*)-1)
            {
                return false;
            }
        }
        if (ptr != page)
        {
            FLASH_LOG_ERROR("verifyAllEntries: backward scan did not end at page start");
            return false;
        }
        return true;
    }

    /**
     * @brief getPrevEntryEnd - returns a pointer past the end of the entry preceding the specified one
     * @param entryEnd - pointer past the last byte of an entry
     * @return Pointer to the first byte of this entry, i.e. points past the end of the previous entry
     */
    static uint8_t* getPrevEntryEnd(uint8_t* entryEnd, uint8_t* page)
    {
         // data.len [align.1] len.1 key.1
        if (entryEnd - page < 2)
        {
            if (entryEnd == page)
            {
                return nullptr; // we have reached the start of the page
            }
            else
            {
                FLASH_LOG_ERROR("getPrevEntryEnd: provided entryEnd is less than 2 bytes past the start of the page");
                return (uint8_t*)-1; // 0xffffffff pointer means error
            }
        }
        uint8_t len = *(entryEnd - 2);
        // if len is odd, round it to the next even number, i.e. we have an extra
        // padding byte after the data, if len is odd
        auto ret = entryEnd - 2 + roundToNextEven(len);
        if (ret < page)
        {
            FLASH_LOG_ERROR("getPrevEntryEnd: provided entry spans before page start");
            return (uint8_t*)-1;
        }
        return ret;
    }
    static bool writePageCtrAndMagic(Addr page, uint16_t ctr)
    {
        bool ok = true;
        ok &= Driver::write16Block(page - kPageMagicSigLen, pageMagicSig(), kPageMagicSigLen);
        ok &= Driver::write16(page - kPageMagicSigLen - sizeof(uint16_t), ctr);
        return ok;
    }
    bool compact()
    {
        if (mIsShuttingDown)
        {
            FLASH_LOG_ERROR("compactPage: System is shutting down");
            return false;
        }
        if (mDataEnd == mActivePage)
        {
            FLASH_LOG_DEBUG("compactPage: Page is empty, nothing to compact");
            return true;
        }
        auto otherPage = (mActivePage == Page1) ? Page2 : Page1;
        auto srcPage = mActivePage;
        uint8_t* srcEnd = mDataEnd; // equal to mActivePage if page is empty

        mActivePage = mDataEnd = otherPage;
        Driver::writeUnlocker unlocker(mActivePage);
        Driver::erasePage(mActivePage);
        uint32_t hadKey[8] = { 0 };
        while (srcEnd > srcPage)
        {
            uint8_t len = *(srcEnd - 2);
            uint8_t* data = srcEnd - 2 - roundToNextEven(len);

            uint8_t key = *(srcEnd - 1);
            assert(key != 0xff);
            uint8_t idx = key >> 5;
            uint32_t mask = 1 << (key & 0b00011111);
            auto& flags = hadKey[idx];
            if ((flags & mask) == 0)
            {
                flags |= mask;
                setValue(key, data, len, true);
            }
            srcEnd = data;
        }
        if (srcEnd != srcPage)
        {
            FLASH_LOG_ERROR("compactPage: backward scan did not end at page start, still continuing");
        }
        writePageCtrAndMagic(mActivePage, PageInfo::getPageCounter(srcPage) + 1);
        // Done writing compacted page to the new page
    }
};

#endif
