#ifndef UTILS_HPP
#define UTILS_HPP

template <uint32_t val>
struct CountOnes { enum: uint8_t { value = (val & 0x01) + CountOnes<(val >> 1)>::value }; };

template <>
struct CountOnes<0> { enum: uint8_t { value = 0 }; };

template <uint32_t val>
struct Right0Count{ enum: uint8_t { value = (val & 1) ? 0 : 1+Right0Count<(val >> 1)>::value }; };

template <>
struct Right0Count<0>{ enum: uint8_t { value = 0 }; };

//returns 0 for 0b00, 1 for 0b01, 4 for 0b1000
template <uint32_t val>
struct HighestBitIdx { enum: uint8_t { value = 1 + HighestBitIdx<(val >> 1)>::value }; };

template<>
struct HighestBitIdx<0> { enum: uint8_t { value = 0 }; };

struct IntDisable
{
protected:
    bool mWasDisabled;
public:
    IntDisable()
    : mWasDisabled(cm_is_masked_interrupts())
    {
        if (!mWasDisabled)
            cm_disable_interrupts();
    }
    ~IntDisable()
    {
        if (!mWasDisabled)
            cm_enable_interrupts();
    }
};

template <typename R, typename... Args>
R interruptSafe(R(*func)(Args...), Args... args)
{
    IntDisable id;
    return func(args...);
}

template <typename... Args>
void interruptSafe(void(*func)(Args...), Args... args)
{
    IntDisable id;
    func(args...);
}

#endif // UTILS_HPP