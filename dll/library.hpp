#pragma once

#include "function_traits.hpp"

#include <windows.h>
#include <dbghelp.h>

#include <locale>
#include <codecvt>
#include <stdexcept>
#include <cstdlib>
#include <iomanip>
#include <unordered_map>
#include <iostream>

#include <boost/lexical_cast.hpp>

// boost pp
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/enum.hpp>

#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/punctuation/comma_if.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/facilities/empty.hpp>
#include <boost/preprocessor/punctuation/comma.hpp>

#include <boost/preprocessor/tuple/elem.hpp>

BOOL CALLBACK _SymProc_(
    PSYMBOL_INFO symInfo,
    ULONG smybolSize,
    PVOID context
);

namespace LibraryLoader
{
    struct Symbol
    {
        SYMBOL_INFO symbolInfo;
        unsigned int estimateSymbolSize;
    };

    class Library
	{
    public:
        friend BOOL CALLBACK ::_SymProc_(
            PSYMBOL_INFO symInfo,
            ULONG smybolSize,
            PVOID context
        );

    public:
        /**
         *  If the construction fails, GetLastError contains why.
         */
        Library(std::string const& library)
			: filename_{library}
#ifdef UNICODE
			, dll_{}
#else
			, dll_{LoadLibrary(library.c_str())}
#endif
            , symbols_{}
		{
#ifdef UNICODE
			std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
			std::wstring wlib = converter.from_bytes(library);
			dll_ = LoadLibrary(wlib.c_str());
#endif

            if (dll_ == nullptr)
			{
				auto lastError = GetLastError();
				throw std::invalid_argument(
					(std::string("could not load dll specified - code: ") + boost::lexical_cast <std::string> (lastError)).c_str()
				);
            }
        }

        /**
         *  Loads all dll symbols into an internal map.
         *  Not useful at the moment.
         */
        void loadSymbols()
        {
            symbols_.clear();

            HANDLE process = GetCurrentProcess();
            DWORD64 dllBase;
            const char* mask = "*";
            bool status;

            status = SymInitialize(process, nullptr, false);
            if (!status)
                throw std::runtime_error("call to SymInitialize failed");

            dllBase = SymLoadModuleEx(
                process,
                nullptr,
                filename_.c_str(),
                nullptr,
                0,
                0,
                nullptr,
                0
            );

            if (dllBase == 0)
            {
                SymCleanup(process);
                throw std::runtime_error("could not load library as a SymModule");
            }

            if (SymEnumSymbols(
                process,
                dllBase,
                mask,
                _SymProc_,
                this
            ))
            {
                SymCleanup(process);
                // success
            }
            else
            {
                SymCleanup(process);
                throw std::runtime_error("SymEnumSymbols failed");
            }
        }

        /**
         *  Returns a function pointer to a dll function.
         */
        template <typename T, typename CallConvention = DefaultCall>
        typename FunctionPointerType <T, CallConvention>::type get(std::string const& functionName)
        {
            return (typename FunctionPointerType <T, CallConvention>::type) (
                GetProcAddress(dll_, functionName.c_str())
            );
        }

        // 'Library' has shared semantics, do not copy.
        Library& operator=(Library const&) = delete;
        Library(Library const&) = delete;

        // but move does make sense.
        Library& operator=(Library&&) = default;
        Library(Library&&) = default;

        ~Library()
        {
            FreeLibrary(dll_);
        }

    private: // member functions
        void addSymbol(std::string const& name, Symbol&& symbol)
        {
            symbols_[name] = std::move(symbol);
        }

    private:
        std::string filename_;
        HMODULE dll_;

        std::unordered_map <std::string, Symbol> symbols_;
    };


    template <typename CallConvention>
    struct LibraryLoaderDelegate
    {
        template <typename FunctionType, bool = true>
        struct TemplateCurry
        {
        };

        template <typename R, typename... Args, bool PartialSpecializationTrick>
        struct TemplateCurry <std::function <R(Args...)>, PartialSpecializationTrick>
        {
            template <typename StringT>
            static auto extract(Library* lib) -> std::function <typename FunctionPointerType <R(Args...), CallConvention>::type(StringT&&)>
            {
                return [lib](StringT&& name) -> typename FunctionPointerType <R(Args...), CallConvention>::type {
                    return lib->get<R(Args...)>(std::forward <StringT&&>(name));
                };
            }

            /*
            template <typename StringT>
            auto extract(Library* lib, StringT&& name) -> typename FunctionPointerType <R(Args...), CallConvention>::type
            {
                return lib->get<R(Args...)>(std::forward <StringT&&> (name));
            }
            */
        };
    };
}

BOOL CALLBACK _SymProc_(
    PSYMBOL_INFO symInfo,
    ULONG symbolSize,
    PVOID context)
{
    auto* ctx = static_cast <LibraryLoader::Library*>(context);
    ctx->addSymbol(symInfo->Name, {*symInfo, symbolSize});
    return true;
}

#define DLL_FUNCTION std::function

#define SEQUENCE_FACTORY_0(...) \
     ((__VA_ARGS__)) SEQUENCE_FACTORY_1
#define SEQUENCE_FACTORY_1(...) \
     ((__VA_ARGS__)) SEQUENCE_FACTORY_0
#define SEQUENCE_FACTORY_0_END
#define SEQUENCE_FACTORY_1_END

#define DLL_FIELD_DECLARATOR(r, data, field_tuple) \
    BOOST_PP_TUPLE_ELEM(0, field_tuple) BOOST_PP_TUPLE_ELEM(1, field_tuple);

#define DLL_FIELD_CONSTRUCTOR(r, data, field_tuple) \
    (BOOST_PP_TUPLE_ELEM(0, field_tuple) const& BOOST_PP_CAT(BOOST_PP_TUPLE_ELEM(1, field_tuple), _))

#define DLL_FIELD_INITIALIZER(r, data, field_tuple) \
    ( \
        BOOST_PP_TUPLE_ELEM(1, field_tuple) { \
            LibraryLoader::LibraryLoaderDelegate < \
                LibraryLoader::DefaultCall \
            >::template TemplateCurry < \
                BOOST_PP_TUPLE_ELEM(0, field_tuple) \
            >::extract<std::string&&>(data)( \
                BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(1, field_tuple)) \
            ) \
        } \
    )

/*
#define DLL_DECLARE_INTERFACE_IMPL(name, fields) \
    struct name \
    { \
        BOOST_PP_SEQ_FOR_EACH(DLL_FIELD_DECLARATOR,, fields) \
        \
        name(BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_FOR_EACH(DLL_FIELD_CONSTRUCTOR,, fields))) \
            : BOOST_PP_IF(1, BOOST_PP_SEQ_ENUM( \
                  BOOST_PP_SEQ_FOR_EACH(DLL_FIELD_INITIALIZER,, fields) \
              ), BOOST_PP_EMPTY)() \
        { \
        } \
    };
*/

#define DLL_DECLARE_INTERFACE_IMPL(name, fields) \
    struct name \
    { \
        BOOST_PP_SEQ_FOR_EACH(DLL_FIELD_DECLARATOR,, fields) \
        \
        name(LibraryLoader::Library* library) \
            : BOOST_PP_SEQ_ENUM( \
                  BOOST_PP_SEQ_FOR_EACH(DLL_FIELD_INITIALIZER, library, fields) \
              ) \
        { \
        } \
    };

#define DLL_DECLARE_INTERFACE(name, fields) \
    DLL_DECLARE_INTERFACE_IMPL(name, BOOST_PP_CAT(SEQUENCE_FACTORY_0 fields, _END))

/*
#define DLL_FUNC_DEF(r, data, elem) \
    std::function <BOOST_PP_SEQ_ELEM(1, elem)> BOOST_PP_SEQ_ELEM(0, elem);

#define DLL_FUNC_INIT(r, data, i, elem) \
    BOOST_PP_COMMA_IF(i) BOOST_PP_SEQ_ELEM(0, elem) {l->get <BOOST_PP_SEQ_ELEM(1, elem)> (BOOST_PP_STRINGIZE(BOOST_PP_SEQ_ELEM(0, elem)))}

#define DLL_DECLARE_INTERFACE(INTERFACE_NAME, FUNCTION_SEQ) \
class INTERFACE_NAME \
{ \
public: \
    BOOST_PP_SEQ_FOR_EACH(DLL_FUNC_DEF, _, FUNCTION_SEQ) \
    \
public: \
    INTERFACE_NAME(LibraryLoader::Library* l) \
        : BOOST_PP_SEQ_FOR_EACH_I(DLL_FUNC_INIT, _, FUNCTION_SEQ) \
    { \
    } \
};
*/
