// SPDX-License-Identifier: AGPL-3.0-only
#define JUCE_GUI_BASICS_INCLUDE_XHEADERS 1
#include "X11Windows.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace x11
{
    namespace
    {
        std::atomic<bool> lost { false };
        ::Display* protectedDisplay = nullptr;
        std::function<void()> lostCallback;

        ::Display* display()
        {
            auto* system = juce::XWindowSystem::getInstanceWithoutCreating();
            return system != nullptr ? system->getDisplay() : nullptr;
        }

        // Sends a request to Hyprland's socket, when running under Hyprland; the reply is
        // read and dropped. The socket is in $XDG_RUNTIME_DIR/hypr, or /tmp/hypr before 0.40.
        void hyprland (const std::string& request)
        {
            const auto* signature = std::getenv ("HYPRLAND_INSTANCE_SIGNATURE");
            if (signature == nullptr || *signature == 0)
                return;
            const auto* runtime = std::getenv ("XDG_RUNTIME_DIR");
            for (const auto& base : { std::string (runtime != nullptr ? runtime : ""), std::string ("/tmp") })
            {
                if (base.empty())
                    continue;
                const auto path = base + "/hypr/" + signature + "/.socket.sock";
                sockaddr_un address {};
                if (path.size() >= sizeof (address.sun_path))
                    continue;
                address.sun_family = AF_UNIX;
                path.copy (address.sun_path, path.size());
                const auto fd = ::socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
                if (fd < 0)
                    return;
                if (::connect (fd, reinterpret_cast<sockaddr*> (&address), sizeof (address)) == 0)
                {
                    if (::write (fd, request.data(), request.size()) == (ssize_t) request.size())
                    {
                        char reply[256];
                        while (::read (fd, reply, sizeof (reply)) > 0) {}
                    }
                    ::close (fd);
                    return;
                }
                ::close (fd);
            }
        }

        // A Hyprland window rule that matches this exact title: every character that is not
        // a letter or digit becomes "." (any character), as spaces and ";" cannot appear.
        std::string titleRule (const std::string& title)
        {
            std::string rule = "title:^(";
            for (const auto c : title)
                rule += (std::isalnum ((unsigned char) c) != 0) ? c : '.';
            return rule + ")$";
        }
    }

    void setWindowClass (void* handle)
    {
        auto* d = display();
        if (d == nullptr || handle == nullptr || lost)
            return;

        char name[] = APP_ID;
        XClassHint hint { name, name };
        auto* x = juce::X11Symbols::getInstance();   // JUCE loads libX11 at run time; PHI does not link it
        x->xSetClassHint (d, (::Window) (juce::pointer_sized_uint) handle, &hint);
        x->xFlush (d);
    }

    void setOpacity (void* handle, double opacity, const std::string& title)
    {
        if (! title.empty())
        {
            // The request changed between Hyprland versions; the ones it does not know fail harmlessly.
            char value[16];
            std::snprintf (value, sizeof (value), "%.2f", juce::jlimit (0.0, 1.0, opacity));
            const auto rule = titleRule (title);
            hyprland ("[[BATCH]]dispatch setprop " + rule + " alpha " + value
                      + " ; dispatch setprop " + rule + " opacity " + value
                      + " ; setprop " + rule + " alpha " + value + " lock");
        }

        auto* d = display();
        if (d == nullptr || handle == nullptr || lost)
            return;

        auto* x = juce::X11Symbols::getInstance();
        const auto atom = x->xInternAtom (d, "_NET_WM_WINDOW_OPACITY", False);
        unsigned long value = (unsigned long) (juce::jlimit (0.0, 1.0, opacity) * 0xffffffffu);
        x->xChangeProperty (d, (::Window) (juce::pointer_sized_uint) handle, atom, XA_CARDINAL, 32, PropModeReplace,
                            reinterpret_cast<unsigned char*> (&value), 1);
        x->xFlush (d);
    }

    void floatWindow (const std::string& title, int width, int height)
    {
        const auto rule = titleRule (title);
        hyprland ("[[BATCH]]dispatch setfloating " + rule
                  + " ; dispatch resizewindowpixel exact " + std::to_string (width) + " " + std::to_string (height) + "," + rule
                  + " ; dispatch focuswindow " + rule
                  + " ; dispatch centerwindow");
    }

    void surviveLostConnection (std::function<void()> whenLost)
    {
        auto* d = display();
        if (d == nullptr || d == protectedDisplay)
            return;
        lostCallback = std::move (whenLost);

        // XSetIOErrorExitHandler is in libX11 1.7 and later; without it Xlib exits as before.
        using ExitHandler = void (*) (::Display*, void*);
        using SetExitHandler = void (*) (::Display*, ExitHandler, void*);
        auto* library = dlopen ("libX11.so.6", RTLD_LAZY | RTLD_NOLOAD);
        auto* set = library != nullptr ? reinterpret_cast<SetExitHandler> (dlsym (library, "XSetIOErrorExitHandler")) : nullptr;
        if (set == nullptr)
            return;

        // The process-wide handler runs first, and Xlib's default one exits
        // straight away; this one returns, so the per-display handler below
        // decides. Other connections (Qt's, a plugin's) still exit as before.
        juce::X11Symbols::getInstance()->xSetIOErrorHandler ([] (::Display*) { return 0; });

        set (d, [] (::Display* broken, void*)
        {
            if (lost.exchange (true))
                return;
            // Stop JUCE's event loop from polling the dead connection.
            const auto fd = juce::X11Symbols::getInstance()->xConnectionNumber (broken);
            juce::MessageManager::callAsync ([fd]
            {
                juce::LinuxEventLoop::unregisterFdCallback (fd);
                if (lostCallback)
                    lostCallback();
            });
        }, nullptr);
        protectedDisplay = d;
    }

    bool connectionLost() { return lost; }

    void useHostErrorHandler()
    {
        // Errors such as BadWindow while a plugin's window is torn down are
        // expected and ignored, as JUCE's own handler does.
        juce::X11Symbols::getInstance()->xSetErrorHandler ([] (::Display*, XErrorEvent*) { return 0; });
    }
}
