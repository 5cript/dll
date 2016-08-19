#pragma once

#include "function_traits.hpp"

#include <windows.h>
#include <dbghelp.h>

#include <stdexcept>
#include <iomanip>
#include <unordered_map>
#include <functional>

// boost pp
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/punctuation/comma_if.hpp>

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
            , dll_{LoadLibrary(library.c_str())}
            , symbols_{}
        {
            if (!dll_)
            {
                throw std::invalid_argument("could not load dll specified");
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

/* (free_buffer, void(char*))
   (wiki_markup, int32_t(const char*, char**))

*/
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
