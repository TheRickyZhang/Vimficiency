// Debug.h

#pragma once
#include <sstream>
#include <string>

#ifdef VIMFICIENCY_DEBUG
constexpr bool DEBUG_ENABLED = true;
#else
constexpr bool DEBUG_ENABLED = false;
#endif

inline std::ostringstream& dout() {
    static std::ostringstream stream;
    return stream;
}

// Space between elements, and new line
template<typename... Args>
inline void debug([[maybe_unused]] Args&&... args){
    if constexpr(DEBUG_ENABLED){
        auto& os = dout();
        const char* sep = "";
        ((os<<sep<<std::forward<Args>(args), sep=" "), ...);
        os<<'\n';
    }
}

inline std::string get_debug_output() {
    return dout().str();
}

inline void clear_debug_output() {
    dout().str("");
    dout().clear();
}
