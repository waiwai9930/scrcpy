#include "screen_otg.h"

#include "icon.h"
#include "options.h"
#include "util/log.h"
#include <windows.h>

/////////////////////////////////////////////
#ifdef _WIN32
static Uint32 sdlCustomEvent;

sdlCustomEvent = SDL_RegisterEvents(1);
if (sdlCustomEvent == ((Uint32)-1)) {
    // 处理错误：无法注册事件
    LOGE("Could not register custom SDL event.");
}
SetKeyboardHook(); // 确保自定义事件已经注册

// 键盘钩子回调函数
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        if (wParam == WM_KEYDOWN) {
            KBDLLHOOKSTRUCT *pStruct = (KBDLLHOOKSTRUCT *)lParam;
            if (pStruct->vkCode == VK_F4) {
                // 检测到F4键被按下，最小化当前窗口
                // 构造自定义事件并推送到SDL事件队列
                SDL_Event event;
                SDL_memset(&event, 0, sizeof(event));
                event.type = sdlCustomEvent;
                event.user.code = 0; // 可以用来传递自定义数据
                SDL_PushEvent(&event);

                // 按需最小化窗口等操作...
                HWND hwnd = GetForegroundWindow();
                if (hwnd) {
                    ShowWindow(hwnd, SW_MINIMIZE);
                }
                return 1; // 不传递按键事件
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// 设置键盘钩子
void SetKeyboardHook() {
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    if (hHook == NULL) {
        LOGE("Failed to install keyboard hook.");
    }
}

// 卸载键盘钩子
void UnsetKeyboardHook() {
    if (hHook != NULL) {
        UnhookWindowsHookEx(hHook);
        hHook = NULL;
    }
}
#endif
/////////////////////////////////////////////


static void
sc_screen_otg_set_mouse_capture(struct sc_screen_otg *screen, bool capture) {
#ifdef __APPLE__
    // Workaround for SDL bug on macOS:
    // <https://github.com/libsdl-org/SDL/issues/5340>
    if (capture) {
        int mouse_x, mouse_y;
        SDL_GetGlobalMouseState(&mouse_x, &mouse_y);

        int x, y, w, h;
        SDL_GetWindowPosition(screen->window, &x, &y);
        SDL_GetWindowSize(screen->window, &w, &h);

        bool outside_window = mouse_x < x || mouse_x >= x + w
                           || mouse_y < y || mouse_y >= y + h;
        if (outside_window) {
            SDL_WarpMouseInWindow(screen->window, w / 2, h / 2);
        }
    }
#else
    (void) screen;
#endif
    if (SDL_SetRelativeMouseMode(capture)) {
        LOGE("Could not set relative mouse mode to %s: %s",
             capture ? "true" : "false", SDL_GetError());
    }
}

static inline bool
sc_screen_otg_get_mouse_capture(struct sc_screen_otg *screen) {
    (void) screen;
    return SDL_GetRelativeMouseMode();
}

static inline void
sc_screen_otg_toggle_mouse_capture(struct sc_screen_otg *screen) {
    (void) screen;
    bool new_value = !sc_screen_otg_get_mouse_capture(screen);
    sc_screen_otg_set_mouse_capture(screen, new_value);
}

static void
sc_screen_otg_render(struct sc_screen_otg *screen) {
    SDL_RenderClear(screen->renderer);
    if (screen->texture) {
        SDL_RenderCopy(screen->renderer, screen->texture, NULL, NULL);
    }
    SDL_RenderPresent(screen->renderer);
}

bool
sc_screen_otg_init(struct sc_screen_otg *screen,
                   const struct sc_screen_otg_params *params) {
    screen->keyboard = params->keyboard;
    screen->mouse = params->mouse;

    screen->mouse_capture_key_pressed = 0;

    const char *title = params->window_title;
    assert(title);

    /////////////////////////////////////////////
    #ifdef _WIN32
    SetKeyboardHook(); // 安装键盘钩子
    #endif
    /////////////////////////////////////////////

    int x = params->window_x != SC_WINDOW_POSITION_UNDEFINED
          ? params->window_x : (int) SDL_WINDOWPOS_UNDEFINED;
    int y = params->window_y != SC_WINDOW_POSITION_UNDEFINED
          ? params->window_y : (int) SDL_WINDOWPOS_UNDEFINED;
    int width = params->window_width ? params->window_width : 256;
    int height = params->window_height ? params->window_height : 256;

    uint32_t window_flags = SDL_WINDOW_ALLOW_HIGHDPI;
    if (params->always_on_top) {
        window_flags |= SDL_WINDOW_ALWAYS_ON_TOP;
    }
    if (params->window_borderless) {
        window_flags |= SDL_WINDOW_BORDERLESS;
    }

    screen->window = SDL_CreateWindow(title, x, y, width, height, window_flags);
    if (!screen->window) {
        LOGE("Could not create window: %s", SDL_GetError());
        return false;
    }

    screen->renderer = SDL_CreateRenderer(screen->window, -1, 0);
    if (!screen->renderer) {
        LOGE("Could not create renderer: %s", SDL_GetError());
        goto error_destroy_window;
    }

    SDL_Surface *icon = scrcpy_icon_load();

    if (icon) {
        SDL_SetWindowIcon(screen->window, icon);

        if (SDL_RenderSetLogicalSize(screen->renderer, icon->w, icon->h)) {
            LOGW("Could not set renderer logical size: %s", SDL_GetError());
            // don't fail
        }

        screen->texture = SDL_CreateTextureFromSurface(screen->renderer, icon);
        scrcpy_icon_destroy(icon);
        if (!screen->texture) {
            goto error_destroy_renderer;
        }
    } else {
        screen->texture = NULL;
        LOGW("Could not load icon");
    }

    if (screen->mouse) {
        // Capture mouse on start
        sc_screen_otg_set_mouse_capture(screen, true);
    }

    return true;

error_destroy_window:
    SDL_DestroyWindow(screen->window);
error_destroy_renderer:
    SDL_DestroyRenderer(screen->renderer);

    return false;
}

void
sc_screen_otg_destroy(struct sc_screen_otg *screen) {
    if (screen->texture) {
        SDL_DestroyTexture(screen->texture);
    }
    SDL_DestroyRenderer(screen->renderer);
    SDL_DestroyWindow(screen->window);
    /////////////////////////////////////////////
    #ifdef _WIN32
    UnsetKeyboardHook(); // 卸载键盘钩子
    #endif
    /////////////////////////////////////////////
}

static inline bool
sc_screen_otg_is_mouse_capture_key(SDL_Keycode key) {
    return key == SDLK_LALT || key == SDLK_LGUI || key == SDLK_RGUI;
}
static void
sc_screen_otg_process_key(struct sc_screen_otg *screen,
                             const SDL_KeyboardEvent *event) {
    assert(screen->keyboard);
    struct sc_key_processor *kp = &screen->keyboard->key_processor;

    struct sc_key_event evt = {
        .action = sc_action_from_sdl_keyboard_type(event->type),
        .keycode = sc_keycode_from_sdl(event->keysym.sym),
        .scancode = sc_scancode_from_sdl(event->keysym.scancode),
        .repeat = event->repeat,
        .mods_state = sc_mods_state_from_sdl(event->keysym.mod),
    };

    assert(kp->ops->process_key);
    kp->ops->process_key(kp, &evt, SC_SEQUENCE_INVALID);
}

static void
sc_screen_otg_process_mouse_motion(struct sc_screen_otg *screen,
                                   const SDL_MouseMotionEvent *event) {
    assert(screen->mouse);
    struct sc_mouse_processor *mp = &screen->mouse->mouse_processor;

    struct sc_mouse_motion_event evt = {
        // .position not used for HID events
        .xrel = event->xrel,
        .yrel = event->yrel,
        .buttons_state = sc_mouse_buttons_state_from_sdl(event->state, true),
    };

    assert(mp->ops->process_mouse_motion);
    mp->ops->process_mouse_motion(mp, &evt);
}

static void
sc_screen_otg_process_mouse_button(struct sc_screen_otg *screen,
                                   const SDL_MouseButtonEvent *event) {
    assert(screen->mouse);
    struct sc_mouse_processor *mp = &screen->mouse->mouse_processor;

    uint32_t sdl_buttons_state = SDL_GetMouseState(NULL, NULL);

    struct sc_mouse_click_event evt = {
        // .position not used for HID events
        .action = sc_action_from_sdl_mousebutton_type(event->type),
        .button = sc_mouse_button_from_sdl(event->button),
        .buttons_state =
            sc_mouse_buttons_state_from_sdl(sdl_buttons_state, true),
    };

    assert(mp->ops->process_mouse_click);
    mp->ops->process_mouse_click(mp, &evt);
}

static void
sc_screen_otg_process_mouse_wheel(struct sc_screen_otg *screen,
                                  const SDL_MouseWheelEvent *event) {
    assert(screen->mouse);
    struct sc_mouse_processor *mp = &screen->mouse->mouse_processor;

    uint32_t sdl_buttons_state = SDL_GetMouseState(NULL, NULL);

    struct sc_mouse_scroll_event evt = {
        // .position not used for HID events
        .hscroll = event->x,
        .vscroll = event->y,
        .buttons_state =
            sc_mouse_buttons_state_from_sdl(sdl_buttons_state, true),
    };

    assert(mp->ops->process_mouse_scroll);
    mp->ops->process_mouse_scroll(mp, &evt);
}

void
sc_screen_otg_handle_event(struct sc_screen_otg *screen, SDL_Event *event) {
    switch (event->type) {
        case SDL_WINDOWEVENT:
            switch (event->window.event) {
                case SDL_WINDOWEVENT_EXPOSED:
                    sc_screen_otg_render(screen);
                    break;
                case SDL_WINDOWEVENT_FOCUS_LOST:
                    if (screen->mouse) {
                        sc_screen_otg_set_mouse_capture(screen, false);
                    }
                    break;
            }
            return;
        case sdlCustomEvent:
            // 这里处理F4按下的逻辑
            break;
        case SDL_KEYDOWN:
            if (screen->mouse) {
                SDL_Keycode key = event->key.keysym.sym;
                if (sc_screen_otg_is_mouse_capture_key(key)) {
                    if (!screen->mouse_capture_key_pressed) {
                        screen->mouse_capture_key_pressed = key;
                    } else {
                        // Another mouse capture key has been pressed, cancel
                        // mouse (un)capture
                        screen->mouse_capture_key_pressed = 0;
                    }
                    // Mouse capture keys are never forwarded to the device
                    return;
                }
            }

            if (screen->keyboard) {
                sc_screen_otg_process_key(screen, &event->key);
            }
            break;
        case SDL_KEYUP:
            if (screen->mouse) {
                SDL_Keycode key = event->key.keysym.sym;
                SDL_Keycode cap = screen->mouse_capture_key_pressed;
                screen->mouse_capture_key_pressed = 0;
                if (sc_screen_otg_is_mouse_capture_key(key)) {
                    if (key == cap) {
                        // A mouse capture key has been pressed then released:
                        // toggle the capture mouse mode
                        sc_screen_otg_toggle_mouse_capture(screen);
                    }
                    // Mouse capture keys are never forwarded to the device
                    return;
                }
            }

            if (screen->keyboard) {
                sc_screen_otg_process_key(screen, &event->key);
            }
            break;
        case SDL_MOUSEMOTION:
            if (screen->mouse && sc_screen_otg_get_mouse_capture(screen)) {
                sc_screen_otg_process_mouse_motion(screen, &event->motion);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (screen->mouse && sc_screen_otg_get_mouse_capture(screen)) {
                sc_screen_otg_process_mouse_button(screen, &event->button);
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (screen->mouse) {
                if (sc_screen_otg_get_mouse_capture(screen)) {
                    sc_screen_otg_process_mouse_button(screen, &event->button);
                } else {
                    sc_screen_otg_set_mouse_capture(screen, true);
                }
            }
            break;
        case SDL_MOUSEWHEEL:
            if (screen->mouse && sc_screen_otg_get_mouse_capture(screen)) {
                sc_screen_otg_process_mouse_wheel(screen, &event->wheel);
            }
            break;
    }
}
