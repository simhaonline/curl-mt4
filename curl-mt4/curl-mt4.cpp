//------------------------------------------------------------------------------
/// \file      curl-mt4.h
/// \author    Serge Aleynikov
/// \copyright (c) 2018, Serge Aleynikov
//------------------------------------------------------------------------------
/// \brief     Implements the API of calling `libcurl` functions from `MT4`
//------------------------------------------------------------------------------

#include "curl-mt4.h"
#include <curl/curl.h>
#include <mutex>
#include <vector>
#include <sstream>
#include <algorithm>

//------------------------------------------------------------------------------
int DataSize(const std::stringstream& str)
{
  std::stringstream& s = const_cast<std::stringstream&>(str);
  int m = s.tellg();
  s.seekg(0, std::ios::end);
  int n = s.tellg();
  s.seekg(m, std::ios::beg);
  return n;
}

//------------------------------------------------------------------------------
struct CurlState
{
    CurlState()
        : m_handle(curl_easy_init())
        , m_headers_list(nullptr)
        , m_debug_level(0)
    {
        curl_easy_setopt(m_handle, CURLOPT_ERRORBUFFER, m_err);
    }

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

    void  AddHeader (std::string header)                      { m_headers.emplace_back(header); }
    void  AddHeaders(std::vector<std::string> const& headers) { for(auto s: headers) m_headers.emplace_back(s); }
    void  AddHeaders(std::vector<std::string>&& headers)      { for(auto s: headers) m_headers.emplace_back(s); }

    void  AddResult(void* data, size_t sz) { m_data.write(static_cast<char*>(data), sz); }
    void  AddRespHeader(const char* h, size_t sz) { m_resp_headers.push_back(std::string(h, sz)); }

    std::string LastError(CURLcode code) const {
        auto len = strlen(m_err);
        return len ? std::string(m_err, len) : curl_easy_strerror(code);
    }

    int   PrepHeaders() {
        if (!m_headers.size()) return 0;

        for (auto& h : m_headers)
            if (!h.empty()) {
                auto p = curl_slist_append(m_headers_list, h.c_str());
                if (!p) return -1;
                m_headers_list = p;
            }
        if (m_debug_level)
            for(auto p=m_headers_list; p; p = p->next) {
                std::stringstream str; str << "Header |" << p->data << "| (" << strlen(p->data) << " bytes)\n";
                OutputDebugStringA(str.str().c_str());
            }
        return curl_easy_setopt(m_handle, CURLOPT_HTTPHEADER, m_headers_list);
    }

    int WriteData(char* buf, int sz) const
    {
        auto n = static_cast<int>(m_data.rdbuf()->in_avail());
        auto m = std::min<int>(n, sz);
        m_data.rdbuf()->sgetn(buf, m);
        return m;
    }

    CURL*       Handle()                             { return m_handle;        }
    std::string Data()                         const { return m_data.str();    }
    int         DataSize()                     const { return ::DataSize(m_data);}

    size_t      RespHeadersCount() const             { return m_resp_headers.size(); }
    const std::string& RespHeader(int i) const       { return m_resp_headers[i]; }

    void        Debug(int level)                     { m_debug_level = level;  }
    int         Debug()    const                     { return m_debug_level;   }

    void        AddDebug(const char* buf, size_t sz) { m_debug.write(buf, sz); }
    void        AddDebug(char c)                     { m_debug << c; }

    int         DebugInfoSize()                const { return ::DataSize(m_debug); }
    std::string DebugInfo()                    const { return m_debug.str(); }
    int         DebugInfo(char* buf, size_t sz) const {
        int n = std::min<int>(DebugInfoSize(), sz-1);
        m_debug.rdbuf()->sgetn(buf, n);
        return n;
    }

private:
    CURL*                    m_handle;
    std::vector<std::string> m_headers;
    struct curl_slist*       m_headers_list;
    std::vector<std::string> m_resp_headers;
    std::stringstream        m_data;
    std::stringstream        m_debug;
    int                      m_debug_level;
    char                     m_err[CURL_ERROR_SIZE];
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
std::string wstr2str(const wchar_t* ws, int len = -1)
{
  return ws ? std::string(ws, ws + (len < 0 ? wcslen(ws) : len)) : std::string();
}

size_t str2wstr(const char* str, int size, wchar_t* out, size_t max_out_len)
{
  size_t n;
  int new_len = std::min<size_t>(max_out_len, size + 1);
  mbstowcs_s(&n, out, new_len, str, size);
  return n - 1;
}

/*
std::string wstr2str(const wchar_t* buffer, int len = -1)
{
  if (!buffer) return std::string();
  if (len < 0) len = wcslen(buffer);

  int n = ::WideCharToMultiByte(CP_UTF8, 0, buffer, len, nullptr, 0, nullptr, nullptr);
  if (n == 0) return "";

  std::string newbuffer;
  newbuffer.resize(n);
  ::WideCharToMultiByte(CP_UTF8, 0, buffer, len,
    const_cast<char*>(newbuffer.c_str()), n, nullptr, nullptr);

  return newbuffer;
}

int write_utf8(std::string s, wchar_t* buffer, int len)
{
int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
if (n == 0) return n;

::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), s.size(), buffer, 0);
::WideCharToMultiByte(CP_UTF8, 0, buffer, len,
const_cast<char*>(newbuffer.c_str()), n, nullptr, nullptr);

return newbuffer;
}

size_t str2wstr(const char* str, size_t size, wchar_t* out, size_t max_out_size)
{
  if (size) {
    int num_chars = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, str, size, nullptr, 0);
    if (num_chars) {
      if (!out) return num_chars;
      int n = std::min<size_t>(max_out_size, num_chars);
      if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, str, size, out, n))
        return n;
    }
  }
  return 0;
}
*/

std::wstring str2wstr(const char* str, size_t size)
{
  int  n = str2wstr(str, size, nullptr, 0);
  if (!n)
    return std::wstring();

  std::wstring wstr;
  wstr.resize(n);
  str2wstr(str, size, &wstr[0], n);
  return wstr;
}


//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
extern "C" {

void* __stdcall CurlInit()
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

    return static_cast<void*>(curl);
}

void __stdcall CurlFinalize(CurlHandle handle)
{
    if (handle == nullptr) return;
    auto   curl = static_cast<CurlState*>(handle);
    delete curl;
}

int __stdcall CurlLastError(CurlHandle handle, int err, char* errs, int max_size)
{
    auto e = static_cast<CURLcode>(err);
    auto s = handle ? static_cast<CurlState*>(handle)->LastError(e) : curl_easy_strerror(e);
    int  n = snprintf(errs, max_size, "%s", s.c_str());
    return n;
}

int __stdcall CurlSetURL(CurlHandle handle, const char* url)
{
    if (handle == nullptr) return -1;
    auto curl  =  static_cast<CurlState*>(handle);

    return curl_easy_setopt(curl->Handle(), CURLOPT_URL, url);
}

int __stdcall CurlSetTimeout(CurlHandle handle, int timeout_secs)
{
    if (handle == nullptr) return CURLE_OK;
    auto curl = static_cast<CurlState*>(handle);
    return curl_easy_setopt(curl->Handle(), CURLOPT_TIMEOUT, timeout_secs);
}

void __stdcall CurlAddHeader(CurlHandle handle, const char* header)
{
    if (handle == nullptr) return;
    auto curl = static_cast<CurlState*>(handle);

    curl->AddHeader(header);
}

void __stdcall CurlAddHeaders(CurlHandle handle, const char* headers)
{
    if (handle == nullptr) return;
    auto curl  =  static_cast<CurlState*>(handle);

    auto hh = split(headers, '\n');

    curl->AddHeaders(hh);
}

size_t __stdcall CurlTotRespHeaders(CurlHandle handle)
{
    if (handle == nullptr) return 0;
    return static_cast<CurlState*>(handle)->RespHeadersCount();
}

int __stdcall CurlGetRespHeader(CurlHandle handle, int idx, char* buf, size_t buflen)
{
    if (handle == nullptr) return -1;
    auto curl = static_cast<CurlState*>(handle);
    auto n = int(curl->RespHeadersCount());
    if (idx < 0 || idx >= n) return -1;
    auto& h = curl->RespHeader(idx);
    if (h.size() < buflen)
        strncpy_s(buf, buflen, h.c_str(), h.size());
    return int(h.size());
}

int __stdcall CurlGetRespHeaderW(CurlHandle handle, int idx, wchar_t* buf, size_t buflen)
{
    if (handle == nullptr) return -1;
    auto curl = static_cast<CurlState*>(handle);
    auto n    = int(curl->RespHeadersCount());
    if (idx < 0 || idx >= n) return -1;
    auto& h = curl->RespHeader(idx);
    return h.size() >= buflen
         ? int(h.size())
         : buf ? str2wstr(h.c_str(), h.size(), buf, buflen) : -1;
}


static size_t write_data(void* ptr, size_t size, size_t nmemb, void* state)
{
    auto sz   = size*nmemb;
    auto curl = static_cast<CurlState*>(state);
    curl->AddResult(ptr, sz);
    return sz;
}


static size_t read_header(char* val, size_t size, size_t nmemb, void* state)
{
    auto sz = size*nmemb;
    auto curl = static_cast<CurlState*>(state);
    auto end  = val+sz;
    auto ok   = std::find(val, end, ':');
    if  (ok < end) {
        auto p = end-1;
        for(; p >= val && (*p == '\r' || *p == '\n'); --p);
        auto n = p - val + 1;
        if (n)
            curl->AddRespHeader(val, n);
    }
    return sz;
}

static void dump(CurlState* curl, const unsigned char *ptr, size_t size, bool nohex)
{
    unsigned int width = nohex ? 120 : 32;

    for (auto i = 0u; i<size; i += width) {
        size_t c;
        char pfx[16];
        auto n = snprintf(pfx, sizeof(pfx), "%4.4lx: ", (unsigned long)i);
        curl->AddDebug(pfx, n);
        
        if (!nohex) {
            char buf[4];
            /* hex not disabled, show it */
            for (c = 0; c < width; c++)
                if (i + c < size) {
                    snprintf(buf, sizeof(buf), "%02x ", ptr[i + c]);
                    curl->AddDebug(buf, 3);
                } else
                    curl->AddDebug("   ", 3);
        }

        for (c = 0; (c < width) && (i + c < size); c++) {
            /* check for 0D0A; if found, skip past and start a new line of output */
            if (nohex && (i + c + 1 < size) && ptr[i + c] == 0x0D &&
                ptr[i + c + 1] == 0x0A) {
                i += (c + 2 - width);
                break;
            }
            auto ch = (ptr[i + c] >= 0x20) && (ptr[i + c]<0x80) ? ptr[i + c] : '.';
            curl->AddDebug(ch);

            /* check again for 0D0A, to avoid an extra \n if it's at width */
            if (nohex && (i + c + 2 < size) && ptr[i + c + 1] == 0x0D &&
                ptr[i + c + 2] == 0x0A) {
                i += (c + 3 - width);
                break;
            }
        }
        curl->AddDebug('\n');
    }
}

static int trace_curl(CURL* handle, curl_infotype type, char* data, size_t size, void* userp)
{
    auto curl = static_cast<CurlState*>(userp);

    char buf[1024];
    int  n;
    (void)handle; /* prevent compiler warning */

    switch (type) {
        case CURLINFO_TEXT:         n = snprintf(buf, sizeof(buf), "= Info.........: %s", data); break;
        case CURLINFO_HEADER_OUT:   n = snprintf(buf, sizeof(buf), "> Send header..: (%d bytes)\n", size); break;
        case CURLINFO_DATA_OUT:     n = snprintf(buf, sizeof(buf), "> Send data....: (%d bytes)\n", size); break;
        case CURLINFO_SSL_DATA_OUT: n = snprintf(buf, sizeof(buf), "> Send SSL data: (%d bytes)\n", size); break;
        case CURLINFO_HEADER_IN:    n = snprintf(buf, sizeof(buf), "< Recv header..: (%d bytes)\n", size); break;
        case CURLINFO_DATA_IN:      n = snprintf(buf, sizeof(buf), "< Recv data....: (%d bytes)\n", size); break;
        case CURLINFO_SSL_DATA_IN:  n = snprintf(buf, sizeof(buf), "< Recv SSL data: (%d bytes)\n", size); break;
        default: /* in case a new one is introduced to shock us */
            return 0;
    }

    curl->AddDebug(buf, n);
    if (type != CURLINFO_TEXT && curl->Debug() > 1)
        dump(curl, (const unsigned char*)data, size, true);

    return 0;
}

int __stdcall CurlExecute(CurlHandle handle, int* code, int* res_length, CurlMethod method,
                          unsigned int opts, const char* post_data, int timeout_secs)
{
    if (handle == nullptr) return -1;
    auto curl  =  static_cast<CurlState*>(handle);
    auto h     =  curl->Handle();

    if (curl->Debug()) opts |= OPT_DEBUG;

    curl_easy_setopt(h, CURLOPT_NOPROGRESS,     1L); // disable progress meter
    if ((OPT_FOLLOW_REDIRECTS & opts) == OPT_FOLLOW_REDIRECTS)
        curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L); // follow HTTP redirects
    if ((CURL_OPT_NOBODY & opts) == CURL_OPT_NOBODY || !post_data)
        curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
    if ((OPT_DEBUG & opts) == OPT_DEBUG) {
        curl_easy_setopt(h, CURLOPT_DEBUGFUNCTION, trace_curl);
        curl_easy_setopt(h, CURLOPT_DEBUGDATA,     curl);
    }

    switch (method) {
        case CurlMethod::GET:
            break;
        case CurlMethod::POST: {
            curl_easy_setopt(h, CURLOPT_POST, 1);
            curl->AddHeader("Expect:");
            if (post_data) {
                curl_easy_setopt(h, CURLOPT_POSTFIELDS,    post_data);
                curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, -1L);
            } else
                curl->AddHeader("Content-Type:");
            break;
        }
        case CurlMethod::POST_JSON: {
            curl->AddHeader("Content-Type: application/json");
            if (post_data == nullptr)
              return -2;
            curl_easy_setopt(h, CURLOPT_POSTFIELDS,    post_data);
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, -1L);
            break;
        }
        case CurlMethod::POST_FORM: {
            if (post_data == nullptr)
                return -2;

            //curl->AddHeader("Expect:");
            curl->AddHeader("Content-Type: application/x-www-form-urlencoded");
            curl_easy_setopt(h, CURLOPT_POSTFIELDS, post_data);
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, -1L);
            break;
        }
        case CurlMethod::DEL:
            curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, "DELETE");
            break;
        case CurlMethod::PUT:
            curl_easy_setopt(h, CURLOPT_PUT, 1L);
            break;
    }
    curl->PrepHeaders();
    curl_easy_setopt(h, CURLOPT_VERBOSE,        OPT_DEBUG & opts);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION,  write_data);
    curl_easy_setopt(h, CURLOPT_WRITEDATA,      handle);
    curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, read_header);
    curl_easy_setopt(h, CURLOPT_HEADERDATA,     handle);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 7);
    curl_easy_setopt(h, CURLOPT_TIMEOUT,        timeout_secs);  // 10 sec timeout
    curl_easy_setopt(h, CURLOPT_TCP_KEEPALIVE,  1L);

    if (curl->Debug() > 1) {
        std::stringstream str; str << "Method --> " << method << "\n";
        OutputDebugStringA(str.str().c_str());
    }

    auto res = curl_easy_perform(h);

    if (res == CURLE_OK) {
        if (res_length) *res_length = curl->DataSize()+1;
        if (code)       curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, code);
    } else {
        if (res_length) *res_length = 0;
        if (code)       *code       = 0;
    }
    return res;
}

int __stdcall CurlGetDataSize(CurlHandle handle)
{
    if (handle == nullptr) return -1;
    auto curl = static_cast<CurlState*>(handle);
    return int(curl->DataSize());
}

int __stdcall CurlGetData(CurlHandle handle, char* buf, int size)
{
    if (handle == nullptr) return -1;
    auto curl = static_cast<CurlState*>(handle);
    return curl->WriteData(buf, size);
}

void __stdcall CurlDbgLevel(void * handle, int level)
{
    if (!handle) return;
    static_cast<CurlState*>(handle)->Debug(level);
}

int __stdcall CurlDbgInfoSize(CurlHandle handle)
{
    return handle ? static_cast<CurlState*>(handle)->DebugInfoSize() : 0;
}

int __stdcall CurlDbgInfo(CurlHandle handle, char* buf, int size)
{
    if (!handle) return 0;
    return static_cast<CurlState*>(handle)->DebugInfo(buf, size);
}

#ifndef NO_CURLMT4_UNICODE_API

int __stdcall CurlLastErrorW(CurlHandle handle, int err, wchar_t* errs, int max_size)
{
    char s[512];
    int n = CurlLastError(handle, err, s, sizeof(errs));
    return str2wstr(s, n, errs, max_size);
}

int __stdcall CurlSetURLW(CurlHandle handle, const wchar_t* urlw)
{
    auto url = wstr2str(urlw);
    return CurlSetURL(handle, url.c_str());
}

void __stdcall CurlAddHeaderW(CurlHandle handle, const wchar_t* header)
{
    auto s = wstr2str(header);

    CurlAddHeader(handle, s.c_str());
}

void __stdcall CurlAddHeadersW(CurlHandle handle, const wchar_t* headers)
{
    auto s = wstr2str(headers);
    CurlAddHeaders(handle, s.c_str());
}

int __stdcall CurlExecuteW(CurlHandle handle, int* code, int* res_length, CurlMethod method,
    unsigned int opts, const wchar_t* post_data, int timeout_secs)
{
    auto post = wstr2str(post_data);
    auto data = post_data ? post.c_str() : nullptr;
    return CurlExecute(handle, code, res_length, method, opts, data, timeout_secs);
}

int __stdcall CurlGetDataW(CurlHandle handle, wchar_t* buf, int size)
{
    if (handle == nullptr) return -1;
    auto curl = static_cast<CurlState*>(handle);
    auto data = curl->Data();
    return str2wstr(data.c_str(), data.size(), buf, size);
}

int __stdcall CurlDbgInfoW(CurlHandle handle, wchar_t* buf, int size)
{
    if (handle == nullptr) return 0;
    auto curl = static_cast<CurlState*>(handle);
    auto dbg = curl->DebugInfo();
    return str2wstr(dbg.c_str(), dbg.size(), buf, size);
}

#endif

} // extern
