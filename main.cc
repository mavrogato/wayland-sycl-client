
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <memory>
#include <coroutine>

#include <CL/sycl.hpp>

#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell-client.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>


namespace
{
    struct application_error : std::runtime_error {
        application_error(char const* msg, std::source_location location)
            : std::runtime_error(msg), location(location)
            {
            }
        std::source_location location;
    };
    using location = std::source_location;
    template <class T>
    auto safe_ptr(T* ptr, auto del, location loc = location::current()) {
        if (!ptr) {
            throw application_error("bad pointer", loc);
        }
        return std::unique_ptr<T, decltype (del)>(ptr, del);
    }
    inline auto safe_ptr(wl_display* ptr, location loc = location::current()) {
        return safe_ptr(ptr, wl_display_disconnect, loc);
    }
#define INTERN_SAFE_PTR(wl_client)                                      \
    inline auto safe_ptr(wl_client* ptr, location loc = location::current()) { \
        return safe_ptr(ptr, wl_client##_destroy, loc);                 \
    }
    INTERN_SAFE_PTR(wl_registry)
    // INTERN_SAFE_PTR(wl_compositor)
    // INTERN_SAFE_PTR(wl_seat)
    // INTERN_SAFE_PTR(wl_surface)
    // INTERN_SAFE_PTR(zxdg_shell_v6)
    // INTERN_SAFE_PTR(zxdg_surface_v6)
    // INTERN_SAFE_PTR(zxdg_toplevel_v6)
    // INTERN_SAFE_PTR(wl_egl_window)
    // INTERN_SAFE_PTR(wl_keyboard)
    // INTERN_SAFE_PTR(wl_pointer)
    // INTERN_SAFE_PTR(wl_touch)
#undef INTERN_SAFE_PTR
    inline auto safe_ptr(EGLDisplay ptr, location loc = location::current()) noexcept {
        return safe_ptr(ptr, eglTerminate, loc);
    }
} // ::

// inline auto listen(auto lambda) {
//     return (void (*)(void*, wl_registry*, uint32_t, char const*, uint32_t)) 
//         [](void* data, wl_registry*, uint32_t name, char const* interface, uint32_t version) {
//         (*reinterpret_cast<decltype (lambda)*>(data))(name, interface, version);
//     };
// }

// inline auto add_listener(wl_registry* registry, auto global, auto global_remove) {
//     wl_registry_listener listener {
//         [](auto data, auto... args) {
//             *reinterpret_cast<decltype (global)*>(data)(args...);
//         },
//         [](auto data, auto... args) {
//             *reinterpret_cast<decltype (global_remove)*>(data)(args...);
//         },
//     };
//     if (wl_registry_add_listener(registry, &listener, 
// }

int main() {
    try {
        auto display = safe_ptr(wl_display_connect(nullptr));
        auto registry = safe_ptr(wl_display_get_registry(display.get()));
        auto [compositor, shell, seat] = [&]
            {
                void* compositor = nullptr;
                void* shell = nullptr;
                void* seat = nullptr;
                // wl_registry_listener listener {
                //     .global = listen([&](uint32_t name, char const* interface, uint32_t version) {
                //     }),
                // };
                // wl_registry_add_listener(registry.get(), &listener, 
                return std::tuple(safe_ptr(compositor), safe_ptr(shell), safe_ptr(seat));
            }();
        return 0;
    }
    catch (application_error& ex) {
        std::cerr << "exception occurred: " << ex.what();
        std::cerr << ':' << ex.location.file_name();
        std::cerr << ':' << ex.location.line();
        std::cerr << ':' << ex.location.column() << std::endl;
    }
    return -1;
}
