
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <memory>
#include <coroutine>

#include <CL/sycl.hpp>

#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell-v6-client.h"
#include "zwp-tablet-v2-client.h"

#include <EGL/egl.h>
#include <CL/cl_gl.h>
#include <GLES3/gl3.h>

template <class T>
class task {
public:
    struct promise_type {
        std::coroutine_handle<> continuation;
        T result;
        void unhandled_exception() { throw; }
        auto get_return_object() noexcept { return task{*this}; }
        auto initial_suspend() noexcept { return std::suspend_always{}; }
        auto final_suspend() noexcept {
            struct awaiter {
                bool await_ready() noexcept { return false; }
                void await_resume() noexcept { }
                auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                    std::coroutine_handle<> continuation = handle.promise().continuation;
                    if (!continuation) {
                        continuation = std::noop_coroutine();
                    }
                    return continuation;
                }
            };
            return awaiter{};
        }
        void return_value(T value) noexcept { this->result = std::move(value); }
    };
    ~task() noexcept { if (this->handle) this->handle.destroy(); }
    task() : handle{nullptr} { }
    task(task&& rhs) noexcept : handle{std::exchange(rhs.handle, nullptr)} { }
    auto operator co_await() noexcept {
        struct awaiter {
            std::coroutine_handle<promise_type> handle;
            bool await_ready() noexcept { return false; }
            T await_resume() noexcept { return std::move(this->handle.promise().result); }
            auto await_suspend(std::coroutine_handle<> handle) noexcept {
                this->handle.promise().continuation = handle;
                return this->handle;
            }
        };
        return awaiter{this->handle};
    }
    T operator()() {
        this->handle.resume();
        return std::move(this->handle.promise().result);
    }
private:
    explicit task(promise_type& p)
        : handle{std::coroutine_handle<promise_type>::from_promise(p)}
        {
        }
    std::coroutine_handle<promise_type> handle;
};

namespace
{
    template <size_t...> struct seq { };
    template <size_t N, size_t... I> struct gen_seq : gen_seq<N-1, N-1, I...> { };
    template <size_t... I> struct gen_seq<0, I...> : seq<I...> { };
    template <class Ch, class Tuple, size_t ...I>
    void print(std::basic_ostream<Ch>& output, Tuple const& t, seq<I...>) noexcept {
        using swallow = int[];
        (void) swallow{0, (void(output << (I==0? "" : " ") << std::get<I>(t)), 0)...};
    }
    template <class Ch, class... Args>
    auto& operator<<(std::basic_ostream<Ch>& output, std::tuple<Args...> const& t) noexcept {
        output.put('(');
        print(output, t, gen_seq<sizeof... (Args)>());
        output.put(')');
        return output;
    }

    struct fatal_error : std::runtime_error {
        fatal_error(char const* msg, std::source_location location)
            : std::runtime_error(msg), location(location)
            {
            }
        std::source_location location;
    };
    template <class Ch>
    auto& operator<<(std::basic_ostream<Ch>& output, std::source_location const& loc) {
        return output << loc.file_name() << ':'
                      << loc.line() << ':'
                      << loc.column() << ':'
                      << loc.function_name() << ':';
    }
    template <class Ch>
    auto& operator<<(std::basic_ostream<Ch>& output, fatal_error const& x) {
        return output << x.location << x.what();
    }

    using location = std::source_location;
    template <class T>
    auto safe_ptr(T* ptr, auto del, location loc = location::current()) {
        if (!ptr) {
            throw fatal_error("bad pointer", loc);
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
    INTERN_SAFE_PTR(wl_compositor)
    INTERN_SAFE_PTR(wl_seat)
    INTERN_SAFE_PTR(wl_surface)
    INTERN_SAFE_PTR(zxdg_shell_v6)
    INTERN_SAFE_PTR(zxdg_surface_v6)
    INTERN_SAFE_PTR(zxdg_toplevel_v6)
    INTERN_SAFE_PTR(zwp_tablet_manager_v2)
    INTERN_SAFE_PTR(zwp_tablet_seat_v2)
    INTERN_SAFE_PTR(zwp_tablet_tool_v2)
    INTERN_SAFE_PTR(wl_egl_window)
    INTERN_SAFE_PTR(wl_keyboard)
    INTERN_SAFE_PTR(wl_pointer)
    INTERN_SAFE_PTR(wl_touch)
#undef INTERN_SAFE_PTR

    template <class Callback, size_t I>
    void func(void* data, auto, auto... args) {
        std::get<I>(*reinterpret_cast<Callback*>(data))(args...);
    }

#define INTERN_ADD_LISTENER(wl_client)                                  \
    template <class Callback, size_t... I>                              \
    auto add_listener_impl(wl_client* ptr, Callback&& callback, seq<I...>) { \
        static Callback backup = std::move(callback);                   \
        static wl_client##_listener listener { func<Callback, I>... };  \
        wl_client##_add_listener(ptr, &listener, &backup);              \
    }                                                                   \
    inline auto add_listener(wl_client* ptr, auto&&... callback) {      \
        add_listener_impl(ptr, std::tuple{callback...}, gen_seq<sizeof ...(callback)>()); \
    }
    INTERN_ADD_LISTENER(wl_registry)
    INTERN_ADD_LISTENER(zxdg_shell_v6)
    INTERN_ADD_LISTENER(zxdg_surface_v6)
    INTERN_ADD_LISTENER(zxdg_toplevel_v6)
    INTERN_ADD_LISTENER(zwp_tablet_seat_v2)
    INTERN_ADD_LISTENER(zwp_tablet_tool_v2)
    INTERN_ADD_LISTENER(wl_keyboard)
    INTERN_ADD_LISTENER(wl_pointer)
    INTERN_ADD_LISTENER(wl_touch)
#undef INTERN_ADD_LISTENER
} // :: (anonymous)

// int main() {
//     if (auto display = wl_display_connect(nullptr)) {
//         if (auto registry = wl_display_get_registry(display)) {
//             void* compositor = nullptr;
//             auto callback = [&](uint32_t name, std::string_view interface, uint32_t version) {
//                 if (interface == wl_compositor_interface.name) {
//                     compositor = wl_registry_bind(registry,
//                                                   name,
//                                                   &wl_compositor_interface,
//                                                   version);
//                 }
//             };
//             wl_registry_listener listener = {
//                 .global = [](auto data, wl_registry*, auto... rest) {
//                     (*reinterpret_cast<decltype (callback)*>(data))(rest...);
//                 },
//             };
//             wl_registry_add_listener(registry, &listener, &callback);
//             wl_display_roundtrip(display);
//             std::cout << compositor << std::endl;
//             wl_registry_destroy(registry);
//         }
//         wl_display_disconnect(display);
//     }
// }

int main() {
    try {
        auto display = safe_ptr(wl_display_connect(nullptr));
        auto registry = safe_ptr(wl_display_get_registry(display.get()));

        void* compositor_raw = nullptr;
        void* shell_raw = nullptr;
        void* seat_raw = nullptr;
        void* tablet_raw = nullptr;
        add_listener(registry.get(),
                     [&](uint32_t name, std::string_view interface, uint32_t version) {
                         if (interface == wl_compositor_interface.name) {
                             compositor_raw = wl_registry_bind(registry.get(),
                                                               name,
                                                               &wl_compositor_interface,
                                                               version);
                         }
                         else if (interface == zxdg_shell_v6_interface.name) {
                             shell_raw = wl_registry_bind(registry.get(),
                                                          name,
                                                          &zxdg_shell_v6_interface,
                                                          version);
                         }
                         else if (interface == wl_seat_interface.name) {
                             seat_raw = wl_registry_bind(registry.get(),
                                                         name,
                                                         &wl_seat_interface,
                                                         version);
                         }
                         else if (interface == zwp_tablet_manager_v2_interface.name) {
                             tablet_raw = wl_registry_bind(registry.get(),
                                                               name,
                                                               &zwp_tablet_manager_v2_interface,
                                                               version);
                         }
                     },
                     [](auto...) noexcept { });
        wl_display_roundtrip(display.get());

        auto compositor = safe_ptr(reinterpret_cast<wl_compositor*>(compositor_raw));
        auto shell = safe_ptr(reinterpret_cast<zxdg_shell_v6*>(shell_raw));
        auto seat = safe_ptr(reinterpret_cast<wl_seat*>(seat_raw));
        auto tablet = safe_ptr(reinterpret_cast<zwp_tablet_manager_v2*>(tablet_raw));

        add_listener(shell.get(),
                     [&](uint32_t serial) noexcept {
                         zxdg_shell_v6_pong(shell.get(), serial);
                     });

        auto surface = safe_ptr(wl_compositor_create_surface(compositor.get()));
        auto xsurface = safe_ptr(zxdg_shell_v6_get_xdg_surface(shell.get(), surface.get()));
        add_listener(xsurface.get(),
                     [&](uint32_t serial) noexcept {
                         zxdg_surface_v6_ack_configure(xsurface.get(), serial);
                     });

        auto egl_display = safe_ptr(eglGetDisplay(display.get()), eglTerminate);
        eglInitialize(egl_display.get(), nullptr, nullptr);
        eglBindAPI(EGL_OPENGL_ES_API);

        float resolution_vec[2] = { 640, 480 };
        auto egl_window = safe_ptr(wl_egl_window_create(surface.get(),
                                                        resolution_vec[0],
                                                        resolution_vec[1]));

        auto toplevel = safe_ptr(zxdg_surface_v6_get_toplevel(xsurface.get()));
        add_listener(toplevel.get(),
                     [&](int width, int height, auto) noexcept {
                         if (width * height) {
                             wl_egl_window_resize(egl_window.get(), width, height, 0, 0);
                             glViewport(0, 0, width, height);
                             resolution_vec[0] = width;
                             resolution_vec[1] = height;
                         }
                     },
                     [&]() noexcept { });
        wl_surface_commit(surface.get());

        EGLConfig config;
        EGLint num_config;
        eglChooseConfig(egl_display.get(),
                        std::array<EGLint, 15>(
                            {
                                EGL_LEVEL, 0,
                                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                EGL_RED_SIZE, 8,
                                EGL_GREEN_SIZE, 8,
                                EGL_BLUE_SIZE, 8,
                                EGL_ALPHA_SIZE, 8,
                                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                EGL_NONE,
                            }
                        ).data(),
                        &config, 1, &num_config);
        auto egl_context = safe_ptr(eglCreateContext(egl_display.get(),
                                                     config,
                                                     EGL_NO_CONTEXT,
                                                     std::array<EGLint, 3>(
                                                         {
                                                             EGL_CONTEXT_CLIENT_VERSION, 2,
                                                             EGL_NONE,
                                                         }
                                                     ).data()),
                                    [&egl_display](auto ptr) noexcept {
                                        eglDestroyContext(egl_display.get(), ptr);
                                    });
        auto egl_surface = safe_ptr(eglCreateWindowSurface(egl_display.get(),
                                                           config,
                                                           egl_window.get(),
                                                           nullptr),
                                    [&egl_display](auto ptr) noexcept {
                                        eglDestroySurface(egl_display.get(), ptr);
                                    });
        eglMakeCurrent(egl_display.get(),
                       egl_surface.get(), egl_surface.get(),
                       egl_context.get());

        // {
        //     cl_platform_id platform_id = nullptr;
        //     cl_uint ret_num_platforms;
        //     clGetPlatformIDs(1, &platform_id, &ret_num_platforms);
        //     cl_device_id device_id = nullptr;
        //     clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_GPU, 1, &device_id, nullptr);
        //     cl_int ret = 0;
        //     auto context = clCreateContext(std::array<cl_context_properties, 7> {
        //             CL_CONTEXT_PLATFORM, (cl_context_properties) platform_id,
        //             CL_GL_CONTEXT_KHR, (cl_context_properties) eglGetCurrentContext(),
        //             CL_EGL_DISPLAY_KHR, (cl_context_properties) eglGetCurrentDisplay(),
        //             0 }.data(),
        //         1, &device_id, nullptr, nullptr, &ret);
        //     std::cout << ret << std::endl;
        //     std::cout << context << std::endl;
        // }

        auto keyboard = safe_ptr(wl_seat_get_keyboard(seat.get()));
        uint32_t key = 0;
        uint32_t state = 0;
        add_listener(keyboard.get(),
                     [](auto...) noexcept { }, // keymap
                     [](auto...) noexcept { }, // enter
                     [](auto...) noexcept { }, // leave
                     [&](auto, auto, uint32_t k, uint32_t s) noexcept {
                         key = k;
                         state = s;
                     },
                     [](auto...) noexcept { }, // modifier
                     [](auto...) noexcept { });// repeat_info

        auto pointer = safe_ptr(wl_seat_get_pointer(seat.get()));
        float pointer_vec[16][2] = { };
        for (auto& item : pointer_vec) { item[0] = -256; item[1] = -256; }
        add_listener(pointer.get(),
                     [](auto...) noexcept { }, // enter
                     [](auto...) noexcept { }, // leave
                     [&](auto, wl_fixed_t x, wl_fixed_t y) noexcept {
                         pointer_vec[0][0] = static_cast<float>(wl_fixed_to_int(x));
                         pointer_vec[0][1] = resolution_vec[1]; 
                         pointer_vec[0][1] -= static_cast<float>(wl_fixed_to_int(y));
                     },
                     [](auto...) noexcept { }, // button
                     [](auto... args) noexcept {
                         std::cout << "axis: " << std::tuple(args...) << std::endl;
                     }, // axis
                     [](auto...) noexcept { }, // frame
                     [](auto... args) noexcept {
                         std::cout << "axis_source: " << std::tuple(args...) << std::endl;
                     }, // axis_source
                     [](auto... args) noexcept {
                         std::cout << "axis_stop: " << std::tuple(args...) << std::endl;
                     }, // axis_stop
                     [](auto... args) noexcept {
                         std::cout << "axis_discrete: " << std::tuple(args...) << std::endl;
                     });// axis_discrete

        auto touch = safe_ptr(wl_seat_get_touch(seat.get()));
        add_listener(touch.get(),
                     [](auto...) noexcept { }, // down
                     [](auto...) noexcept { }, // up
                     [&](uint32_t, int32_t id, wl_fixed_t x, wl_fixed_t y) noexcept {
                         int i = id % 10 + 1;
                         pointer_vec[i][0] = static_cast<float>(wl_fixed_to_int(x));
                         pointer_vec[i][1] = resolution_vec[1]; 
                         pointer_vec[i][1] -= static_cast<float>(wl_fixed_to_int(y));
                     },
                     [](auto...) noexcept { }, // frame
                     [](auto...) noexcept { }, // cancel
                     [](auto...) noexcept { }, // shape
                     [](auto...) noexcept { });// orientation

        std::cout << "-------------------------------------------------------" << std::endl;
        std::cout << zwp_tablet_manager_v2_get_version(tablet.get()) << std::endl;
        auto slate = safe_ptr(zwp_tablet_manager_v2_get_tablet_seat(tablet.get(), seat.get()));
        add_listener(slate.get(),
                     [](auto...) noexcept {
                         std::cout << "A tablet added." << std::endl;
                     },
                     [&](auto stylus) noexcept {
                         std::cout << "A tool added." << std::endl;
                         add_listener(
                             stylus,
                             [](auto tool_type) noexcept {
                                 std::cout << "type: " << tool_type << std::endl;
                             },
                             [](auto hwsn_hi, auto hwsn_lo) noexcept {
                                 std::cout << "sn: " << hwsn_hi << ':' << hwsn_lo << std::endl;
                             },
                             [](auto hwid_hi, auto hwid_lo) noexcept {
                                 std::cout << "id: " << hwid_hi << ':' << hwid_lo << std::endl;
                             },
                             [](auto capability) noexcept {
                                 std::cout << "caps: " << capability << std::endl;
                             },
                             []() noexcept {
                                 std::cout << "done." << std::endl;
                             },
                             [stylus]() noexcept {
                                 std::cout << "removed." << std::endl;
                                 zwp_tablet_tool_v2_destroy(stylus);
                             },
                             [](auto serial, auto, auto) noexcept {
                                 std::cout << "proximity in: " << serial << std::endl;
                             },
                             []() noexcept {
                                 std::cout << "proximity out." << std::endl;
                             },
                             [](auto serial) noexcept {
                                 std::cout << "down: " << serial << std::endl;
                             },
                             []() noexcept {
                                 std::cout << "up." << std::endl;
                             },
                             [&](auto x, auto y) noexcept {
                                 std::cout << "move: "
                                           << std::complex(wl_fixed_to_double(x),
                                                           wl_fixed_to_double(y)) << std::endl;
                                 pointer_vec[15][0] = static_cast<float>(wl_fixed_to_int(x));
                                 pointer_vec[15][1] = resolution_vec[1]; 
                                 pointer_vec[15][1] -= static_cast<float>(wl_fixed_to_int(y));
                             },
                             [](auto pressure) noexcept {
                                 std::cout << "pressure: " << pressure << std::endl;
                             },
                             [](auto distance) noexcept {
                                 std::cout << "distance: " << distance << std::endl;
                             },
                             [](auto phi, auto theta) noexcept {
                                 std::cout << "tilt: "
                                           << std::complex(wl_fixed_to_double(phi),
                                                           wl_fixed_to_double(theta)) << std::endl;
                             },
                             [](auto rotation) noexcept {
                                 std::cout << "rotation: " << rotation << std::endl;
                             },
                             [](auto slider) noexcept {
                                 std::cout << "slider: " << slider << std::endl;;
                             },
                             [](auto degree, auto clicks) noexcept {
                                 std::cout << "wheel: " << degree << '/' << clicks << std::endl;
                             },
                             [](auto serial, auto button, auto state) noexcept {
                                 std::cout << "button: " << serial << ' ' << button << ' '
                                           << state << std::endl;
                             },
                             [](auto time) noexcept {
                                 std::cout << "frame: " << time << std::endl;
                             }
                         );
                     },
                     [](auto...) noexcept {
                         std::cout << "A pad added." << std::endl;
                     });

        auto program = glCreateProgram();
        auto compile = [](auto program, auto shader_type, char const* code) noexcept {
            GLint compiled = 0;
            if (auto id = glCreateShader(shader_type)) {
                glShaderSource(id, 1, &code, nullptr);
                glCompileShader(id);
                glGetShaderiv(id, GL_COMPILE_STATUS, &compiled);
                if (compiled) {
                    glAttachShader(program, id);
                }
                glDeleteShader(id);
            }
            return compiled;
        };
#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)
        compile(program,
                GL_VERTEX_SHADER,
                TO_STRING(attribute vec4 position;
                          varying vec2 vert;
                          void main(void) {
                              vert = position.xy;
                              gl_Position = position;
                          }));
        compile(program,
                GL_FRAGMENT_SHADER,
                TO_STRING(precision mediump float;
                          varying vec2 vert;
                          uniform vec2 resolution;
                          uniform vec2 pointer[16];
                          void main(void) {
                              float brightness = length(gl_FragCoord.xy - resolution / 2.0);
                              brightness /= length(resolution);
                              brightness = 1.0 - brightness;
                              gl_FragColor = vec4(0.0, 0.0, brightness, brightness);
                              for (int i = 0; i < 16; ++i) {
                                  float radius = length(pointer[i] - gl_FragCoord.xy);
                                  float touchMark = smoothstep(16.0, 40.0, radius);
                                  gl_FragColor *= touchMark;
                              }
                          }));
#undef TO_STRING
#undef STRINGIFY
        glBindAttribLocation(program, 0, "position");
        auto link = [](auto program) noexcept {
            glLinkProgram(program);
            GLint linked;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            return linked;
        };
        link(program);
        glUseProgram(program);
        glFrontFace(GL_CW);

        do {
            if (key == 1 && state == 0) {
                return 0;
            }
            glClearColor(0.0, 0.0, 0.8, 0.8);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glUseProgram(program);
            glUniform2fv(glGetUniformLocation(program, "resolution"), 1, resolution_vec);
            glUniform2fv(glGetUniformLocation(program, "pointer"), 16, &pointer_vec[0][0]);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0,
                                  std::array<float, 12>(
                                      {
                                          -1, +1, 0,
                                          +1, +1, 0,
                                          +1, -1, 0,
                                          -1, -1, 0,
                                      }).data());
            glEnableVertexAttribArray(0);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            eglSwapBuffers(egl_display.get(), egl_surface.get());
        } while (wl_display_dispatch(display.get()) != -1);
        eglMakeCurrent(egl_display.get(), nullptr, nullptr, nullptr);
        return 0;
    }
    catch (fatal_error& ex) {
        std::cerr << ex << std::endl;
    }
    return -1;
}
