// mt4curl.cpp : Defines the exported functions for the DLL application.
//

#include "mt4curl.h"
#include <curl/curl.h>
#include <mutex>
#include <vector>
#include <sstream>
#include <algorithm>

//------------------------------------------------------------------------------
struct CurlState
{
    CurlState()
        : m_handle(curl_easy_init())
        , m_headers_list(nullptr)
    {}

    ~CurlState()
    {
        if (m_headers_list) {
            curl_slist_free_all(m_headers_list);
            m_headers_list = nullptr;
        }

        if (m_handle) {
            curl_easy_cleanup(m_handle);
            m_handle = nullptr;
        }
    }

    CURL*       Handle()         { return m_handle;     }
    std::string Data()     const { return m_data.str(); }
    size_t      DataSize()
    {
        auto pos  = m_data.tellg();
        m_data.seekg(0, std::ios::end);
        auto size = m_data.tellg();
        m_data.seekg(pos, std::ios::beg);
        return size;
    }

    void  AddHeader(std::string header) { m_headers.emplace_back(header); }
    void  AddHeaders(std::vector<std::string> const& headers) { for(auto s: headers) m_headers.emplace_back(s); }

    void  AddResult(void* data, size_t sz) { m_data.write(static_cast<char*>(data), sz); }

    int   PrepHeaders() {
        if (!m_headers.size()) return 0;

        for (auto& h : m_headers)
            if (!h.empty())
                m_headers_list = curl_slist_append(m_headers_list, h.c_str());
        
        return curl_easy_setopt(m_handle, CURLOPT_HTTPHEADER, m_headers_list);
    }

    int WriteData(char* buf, int sz) const
    {
        auto n = m_data.rdbuf()->in_avail();
        auto m = std::min<int>(n, sz);
        m_data.rdbuf()->sgetn(buf, m);
        return m;
    }
private:
    CURL*                    m_handle;
    std::vector<std::string> m_headers;
    struct curl_slist*       m_headers_list;
    std::stringstream        m_data;
};

//------------------------------------------------------------------------------
std::vector<std::string> split(const char *str, char c = ' ')
{
    std::vector<std::string> result;

    do {
        const char *begin = str;

        while (*str != c && *str)
            str++;

        result.push_back(std::string(begin, str));
    } while (*str++);

    return result;
}

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
extern "C" {

void* _stdcall CurlInit()
{
    static int s_initialized;
    static std::mutex mtx;

    if (!s_initialized) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!s_initialized) {
            curl_global_init(CURL_GLOBAL_ALL);
            s_initialized = true;
        }
    }

    auto curl = new CurlState();

    if (curl == nullptr)
        return nullptr;

    curl_easy_setopt(curl->Handle(), CURLOPT_NOPROGRESS,     1L); // disable progress meter
    curl_easy_setopt(curl->Handle(), CURLOPT_FOLLOWLOCATION, 1L); // follow HTTP redirects

    return static_cast<void*>(curl);
}

MT4EXPORT void _stdcall CurlFinalize(void* handle)
{
    if (handle == nullptr) return;
    auto   curl = static_cast<CurlState*>(handle);
    delete curl;
}

MT4EXPORT int _stdcall CurlError(int err, char* errs, int max_size)
{
    auto res = curl_easy_strerror(static_cast<CURLcode>(err));
    int  n   = snprintf(errs, max_size, "%s", res);
    return n;
}

int _stdcall CurlSetURL(void* handle, const char* url)
{
    if (handle == nullptr) return false;
    auto curl  =  static_cast<CurlState*>(handle);

    return curl_easy_setopt(curl->Handle(), CURLOPT_URL, url);
}

void _stdcall CurlSetHeaders(void* handle, const char* headers)
{
    if (handle == nullptr) return;
    auto curl  =  static_cast<CurlState*>(handle);

    auto hh = split(headers, '\n');

    curl->AddHeaders(hh);
}

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *state)
{
    auto sz   = size * nmemb;
    auto curl = static_cast<CurlState*>(state);
    curl->AddResult(ptr, size);
    return size;
}

int _stdcall CurlExecute(void* handle, const char* method, int& code)
{
    if (handle == nullptr) return false;
    auto curl  =  static_cast<CurlState*>(handle);

    curl->PrepHeaders();
    curl_easy_setopt(curl->Handle(), CURLOPT_VERBOSE,       0L);
    curl_easy_setopt(curl->Handle(), CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl->Handle(), CURLOPT_WRITEDATA,     handle);
    auto res   = curl_easy_perform(curl->Handle());

    return res;
}

MT4EXPORT int _stdcall CurlGetDataSize(void* handle)
{
    if (handle == nullptr) return false;
    auto curl = static_cast<CurlState*>(handle);
    return int(curl->DataSize());
}

MT4EXPORT int _stdcall CurlGetData(void* handle, char* buf, int size)
{
    if (handle == nullptr) return false;
    auto curl = static_cast<CurlState*>(handle);
    return curl->WriteData(buf, size);
}

} // extern
