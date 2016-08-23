#pragma once

#include <type_traits>
#include <functional>

namespace LibraryLoader
{
    struct Stdcall {};
    struct Cdecl {};
    struct DefaultCall {};

    template <typename... List>
    struct ParameterPack
    {
    };

    template <typename FunctionSignature, typename CallingConvention = DefaultCall>
    struct FunctionPointerType
    {
    };

    #define CALLING_CONVENTION_SPECIALIZATION(LOCAL, RESULT) \
    template <typename R, typename... Args> \
    struct FunctionPointerType <R(Args...), LOCAL> \
    { \
        using type = R(RESULT*)(Args...); \
        using return_type = R; \
    };

    CALLING_CONVENTION_SPECIALIZATION(Stdcall, __stdcall)
    CALLING_CONVENTION_SPECIALIZATION(Cdecl, __cdecl)
    CALLING_CONVENTION_SPECIALIZATION(DefaultCall, )
}

