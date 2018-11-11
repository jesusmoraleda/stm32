/**
 * @author Alexander Vassilev
 * @copyright BSD License
 */

#ifndef _TOSTRING_H
#define _TOSTRING_H

#include <type_traits>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h> //for padding calculation we need log10

static_assert(sizeof(size_t) == sizeof(void*), "size_t is not same size as void*");
static_assert(sizeof(size_t) == sizeof(ptrdiff_t), "size_t is not same size as ptrdiff_t");
static_assert(std::is_unsigned<size_t>::value, "size_t is not unsigned");

/** Various flags that specify how a value is converted to string.
 * Lower 8 bits are reserved for the numeric system base (for integer numbers)
 * or precision (for floating point numbers)
 */
enum: uint16_t {
    kFlagsBaseMask = 0xff,
    kFlagsPrecMask = 0xff,
    kLowerCase = 0x0, kUpperCase = 0x1000,
    kDontNullTerminate = 0x0200, kNoPrefix = 0x0400
};

typedef uint16_t Flags;
constexpr uint8_t baseFromFlags(Flags flags)
{
    uint8_t base = flags & 0xff;
    return base ? base : 10;
}
constexpr uint8_t precFromFlags(Flags flags)
{
    uint8_t prec = flags & 0xff;
    return prec ? prec : 6;
}

template <size_t base, Flags flags=0>
struct DigitConverter;

template <Flags flags>
struct DigitConverter<10, flags>
{
    enum { digitsPerByte = 3, prefixLen = 0 };
    static char* putPrefix(char* buf) { return buf; }
    static char toDigit(uint8_t digit) { return '0'+digit; }
};

template <Flags flags>
struct DigitConverter<16, flags>
{
    enum { digitsPerByte = 2, prefixLen = 2 };
    static char* putPrefix(char* buf) { buf[0] = '0', buf[1] = 'x'; return buf+2; }
    static char toDigit(uint8_t digit)
    {
        if (digit < 10)
            return '0'+digit;
        else
            return (flags & kUpperCase ? 'A': 'a')+(digit-10);
    }
};

template <Flags flags>
struct DigitConverter<2, flags>
{
    enum { digitsPerByte = 8, prefixLen = 2 };
    static char* putPrefix(char *buf) { buf[0] = '0'; buf[1] = 'b'; return buf+2; }
    static char toDigit(uint8_t digit) { return '0'+digit; }
};

template<Flags flags=10, typename Val>
typename std::enable_if<std::is_unsigned<Val>::value
                     && std::is_integral<Val>::value
                     && !std::is_same<Val, char>::value, char*>::type
toString(char* buf, size_t bufsize, Val val, uint8_t numDigits=0)
{
    assert(buf);
    assert(bufsize);

    if ((flags & kDontNullTerminate) == 0)
        bufsize--;
    enum: uint8_t { base = baseFromFlags(flags) };
    DigitConverter<base, flags> digitConv;
    char stagingBuf[digitConv.digitsPerByte * sizeof(Val)];
    char* writePtr = stagingBuf;
    for (; val; val /= base)
    {
        Val digit = val % base;
        *(writePtr++) = digitConv.toDigit(digit);
    };

    size_t len = writePtr - stagingBuf;
    size_t padLen;
    if (!len && !numDigits)
        padLen = 1;
    else if (len < numDigits)
        padLen = numDigits - len;
    else
        padLen = 0;

    if (((flags & kNoPrefix) == 0) && digitConv.prefixLen)
    {
        if (bufsize < digitConv.prefixLen+padLen+len)
        {
            *buf = 0;
            return nullptr;
        }
        buf = digitConv.putPrefix(buf);
    }
    else
    {
        if (bufsize < padLen+len)
        {
            *buf = 0;
            return nullptr;
        }
    }
    for(;padLen; padLen--)
    {
        *(buf++) = '0';
    }
    for(; len; len--)
    {
        *(buf++) = *(--writePtr);
    }

    if ((flags & kDontNullTerminate) == 0)
        *buf = 0;
    return buf;
}

template<Flags flags=10, typename Val>
typename std::enable_if<std::is_integral<Val>::value
    && std::is_signed<Val>::value
    && !std::is_same<Val, char>::value, char*>::type
toString(char* buf, size_t bufsize, Val val)
{
    typedef typename std::make_unsigned<Val>::type UVal;
    if (val < 0)
    {
        if (bufsize < 2)
        {
            if (bufsize)
            {
                *buf = (flags & kDontNullTerminate) ? '-' : 0;
            }
            return nullptr;
        }
        *buf = '-';
        return toString<flags, UVal>(buf+1, bufsize-1, -val);
    }
    else
    {
        return toString<flags, UVal>(buf, bufsize, val);
    }
}

template <class T, class Enabled=void>
struct is_char_ptr
{
    enum: bool {value = false};
};

template<class T>
struct is_char_ptr<T, typename
  std::enable_if<std::is_pointer<T>::value
    && std::is_same<
      typename std::remove_const<typename std::remove_pointer<T>::type>::type,char>::value
        ,void>::type>
{
    enum: bool { value = true };
};

template <class T, size_t Size=sizeof(T)>
struct UnsignedEquiv{ enum: bool {invalid = true}; };

template <class T>
struct UnsignedEquiv<T, 1> { typedef uint8_t type; };

template <class T>
struct UnsignedEquiv<T, 2> { typedef uint16_t type; };

template <class T>
struct UnsignedEquiv<T, 4> { typedef uint32_t type; };

template <class T>
struct UnsignedEquiv<T, 8> { typedef uint64_t type; };

template <typename T, Flags aFlags>
struct IntFmt
{
    typedef typename UnsignedEquiv<T>::type ScalarType;
    enum: uint8_t { base = baseFromFlags(aFlags) };
    static constexpr Flags flags = aFlags & kFlagsBaseMask;
    ScalarType value;
    uint8_t padding;
    explicit IntFmt(T aVal, uint8_t aPad=0): value((ScalarType)(aVal)), padding(aPad){}
    template <class U=T, class=typename std::enable_if<!std::is_same<ScalarType, U>::value, void>::type>
    explicit IntFmt(ScalarType aVal, uint8_t aPad=0): value(aVal), padding(aPad){}
};

template <typename T, uint8_t base>
struct NumLenForBase
{
  enum: uint8_t { value = sizeof(T)*(uint8_t)(log10f(256)/log10f(base)+0.9) };
};

template <Flags flags=0, class T>
IntFmt<T, flags> fmtInt(T aVal, uint8_t aPad=NumLenForBase<T,baseFromFlags(flags)>::value)
{ return IntFmt<T, flags>(aVal, aPad); }

template <Flags flags=0, class T>
auto fmtHex(T aVal, uint8_t aPad=NumLenForBase<T,16>::value)
{ return IntFmt<T, (flags&~kFlagsBaseMask)|16>(aVal, aPad); }

template <Flags flags=0, class T>
auto fmtBin(T aVal, uint8_t aPad=NumLenForBase<T,2>::value)
{ return IntFmt<T, (flags&~kFlagsBaseMask)|2>(aVal, aPad); }

template <Flags flags=0>
auto fmtHex8(uint8_t aVal, uint8_t aPad=2)
{ return IntFmt<uint8_t, (flags&~kFlagsBaseMask)|16>(aVal, aPad); }

template <Flags flags=0>
auto fmtBin8(uint8_t aVal, uint8_t aPad=8)
{ return IntFmt<uint8_t, (flags&~kFlagsBaseMask)|2>(aVal, aPad); }

template <Flags flags=0>
auto fmtHex16(uint16_t aVal, uint8_t aPad=4)
{ return IntFmt<uint16_t, (flags&~kFlagsBaseMask)|16>(aVal, aPad); }

template <Flags flags=0>
auto fmtBin16(uint16_t aVal, uint8_t aPad=16)
{ return IntFmt<uint16_t, (flags&~kFlagsBaseMask)|2>(aVal, aPad); }

template <Flags flags=16, class T>
IntFmt<T, flags> fmtStruct(T aVal)
{
    typedef IntFmt<T, flags> Fmt;
    return Fmt(*((typename Fmt::ScalarType*)&aVal));
}

template <Flags flags=0, class P>
typename std::enable_if<std::is_pointer<P>::value && !is_char_ptr<P>::value, char*>::type
toString(char *buf, size_t bufsize, P ptr)
{
    return toString(buf, bufsize, fmtHex<flags>(ptr));
}

template<Flags flags=0, Flags fmtFlags, typename Val>
char* toString(char *buf, size_t bufsize, IntFmt<Val, fmtFlags> num)
{
    return toString<num.flags | (flags & ~kFlagsBaseMask)>(buf, bufsize, num.value, num.padding);
}

template<Flags flags=0>
typename std::enable_if<(flags & kDontNullTerminate) == 0, char*>::type
toString(char* buf, size_t bufsize, const char* val)
{
    if (!bufsize)
        return nullptr;
    auto bufend = buf+bufsize-1; //reserve space for the terminating null
    while(*val)
    {
        if(buf >= bufend)
        {
            assert(buf == bufend);
            *buf = 0;
            return nullptr;
        }
        *(buf++) = *(val++);
    }
    *buf = 0;
    return buf;
}

template<Flags flags=0>
typename std::enable_if<(flags & kDontNullTerminate), char*>::type
toString(char* buf, size_t bufsize, const char* val)
{
    auto bufend = buf+bufsize;
    while(*val)
    {
        if(buf >= bufend)
            return nullptr;
        *(buf++) = *(val++);
    }
    return buf;
}

template<Flags flags=0, typename Val>
typename std::enable_if<std::is_same<Val, char>::value
    && (flags & kDontNullTerminate), char*>::type
toString(char* buf, size_t bufsize, Val val)
{
    if(!bufsize)
        return nullptr;
    *(buf++) = val;
    return buf;
}

template<Flags flags=0, typename Val>
typename std::enable_if<std::is_same<Val, char>::value
    && (flags & kDontNullTerminate) == 0, char*>::type
toString(char* buf, size_t bufsize, Val val)
{
    if (bufsize >= 2)
    {
        *(buf++) = val;
        *buf = 0;
        return buf;
    }
    else if (bufsize == 1)
    {
        *buf = 0;
        return nullptr;
    }
    else
    {
        return nullptr;
    }
}
template <size_t base, uint8_t p>
struct Pow
{  enum: size_t { value = base * Pow<base, p-1>::value  }; };

template <size_t base>
struct Pow<base, 1>
{ enum: size_t { value = base }; };

template<Flags flags=6, typename Val>
typename std::enable_if<std::is_floating_point<Val>::value, char*>::type
toString(char* buf, size_t bufsize, Val val, uint8_t padding=0)
{
    enum: uint8_t { prec = precFromFlags(flags) };
    if (!bufsize)
        return nullptr;
    char* bufRealEnd = buf+bufsize;
    if ((flags & kDontNullTerminate) == 0)
        bufsize--;

    char* bufend = buf+bufsize;
    if (val < 0)
    {
        if (bufsize < 4) //at least '-0.0'
        {
            *buf = 0;
            return nullptr;
        }
        *(buf++) = '-';
        val = -val;
    }
    else
    {
        if (bufsize < 3)
        {
            *buf = 0;
            return nullptr;
        }
    }
    size_t whole = (size_t)(val);

    enum: uint32_t { mult = Pow<10, prec>::value };
    size_t decimal = (val-whole)*mult+0.5;
    if (decimal >= mult) //the part after the dot overflows to >= 1 due to rounding
    {
        //move the overflowed unit to the whole part and subtract it from
        //the decimal
        whole++;
        decimal-=mult;
    }
    //we have some minimum space for null termination even if buffer is not enough
    buf = toString<(flags&~kFlagsBaseMask)|10>(buf, bufRealEnd-buf, whole, padding);
    if (!buf)
    {
        assert(*(bufRealEnd-1)==0); //assert null termination
        return nullptr;
    }
    assert(buf < bufRealEnd);
    if (bufend-buf < 2) //must have space at least for '.0' and optional null terminator
    {
        *buf = 0;
        return nullptr;
    }
    *(buf++) = '.';
    return toString(buf, bufRealEnd-buf, decimal, prec);
}

template <class T, Flags aFlags>
struct FpFmt
{
    enum: uint8_t { prec = precFromFlags(aFlags) };
    constexpr static Flags flags = aFlags & kFlagsPrecMask;
    T value;
    uint8_t padding;
    FpFmt(T aVal, uint8_t aPad): value(aVal), padding(aPad){}
};

template <Flags aFlags=6, class T>
auto fmtFp(T val, uint8_t pad=0)
{
    return FpFmt<T, aFlags>(val, pad);
}

template <Flags generalFlags, Flags fpFlags, typename Val>
char* toString(char *buf, size_t bufsize, FpFmt<Val, fpFlags> fp)
{
    // Extract precision and padding, merge other flags from fpFlags to aFlags to
    // and forward to the toString(float) version
    // General flags filtered out from fpFlags and fp formatting flags filtered out from general flags
    return toString<fp.flags | (generalFlags & ~kFlagsPrecMask), Val>(buf, bufsize, fp.value, fp.padding);
}

template <uint8_t aFlags=0>
struct RptChar
{
    char mChar;
    uint16_t mCount;
public:
    RptChar(char ch, uint16_t count): mChar(ch), mCount(count){}
    char ch() const { return mChar; }
    uint16_t count() const { return mCount; }
};

template <Flags aFlags = 0>
RptChar<aFlags> rptChar(char ch, uint16_t count)
{
    return RptChar<aFlags>(ch, count);
}

template <Flags aFlags=0, uint8_t rptFlags>
char* toString(char* buf, size_t bufsize, RptChar<rptFlags> val)
{
    if (!bufsize)
        return nullptr;
    if ((aFlags & kDontNullTerminate) == 0)
        bufsize--;
    if (val.count() > bufsize)
        return nullptr;
    char* end = buf + val.count();
    char ch = val.ch();
    while (buf < end)
    {
        *(buf++) = ch;
    }
    if ((aFlags & kDontNullTerminate) == 0)
        *buf = 0;
    return buf;
}

#endif
