/*
 * DISTRHO Plugin Toolkit (DPT)
 * Copyright (C) 2012-2013 Filipe Coelho <falktx@falktx.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * For a full copy of the license see the LGPL.txt file
 */

#include "AppPrivate.hpp"

#include "../Widget.hpp"
#include "../Window.hpp"
#include "../../DistrhoUtils.hpp"

#if DISTRHO_OS_WINDOWS
# include "pugl/pugl_win.cpp"
#elif DISTRHO_OS_MAC
# include "pugl/pugl_osx.m"
#elif DISTRHO_OS_LINUX
# include "pugl/pugl_x11.c"
#else
# error Unsupported platform!
#endif

START_NAMESPACE_DISTRHO

// -------------------------------------------------
// Utils

#if DISTRHO_OS_LINUX
static Bool isMapNotify(Display*, XEvent* ev, XPointer win)
{
    return (ev->type == MapNotify && ev->xmap.window == *((::Window*)win));
}
static Bool isUnmapNotify(Display*, XEvent* ev, XPointer win)
{
    return (ev->type == UnmapNotify && ev->xunmap.window == *((::Window*)win));
}
#endif

#define FOR_EACH_WIDGET(it) \
  for (auto it = fWidgets.begin(); it != fWidgets.end(); it++)

// -------------------------------------------------
// Window Private

class Window::Private
{
public:
    Private(Window* self, App::Private* app, Private* parent, intptr_t parentId = 0)
        : kApp(app),
          kSelf(self),
          kView(puglCreate(parentId, "test", 300, 100, false, false)),
          fParent(parent),
          fChildFocus(nullptr),
          fVisible(false),
          fClosed(false),
#if DISTRHO_OS_WINDOWS
          hwnd(0)
#elif DISTRHO_OS_LINUX
          xDisplay(nullptr),
          xWindow(0)
#else
          _dummy(0)
#endif
    {
        if (kView == nullptr)
            return;

        puglSetHandle(kView, this);
        puglSetDisplayFunc(kView, onDisplayCallback);
        puglSetKeyboardFunc(kView, onKeyboardCallback);
        puglSetMotionFunc(kView, onMotionCallback);
        puglSetMouseFunc(kView, onMouseCallback);
        puglSetScrollFunc(kView, onScrollCallback);
        puglSetSpecialFunc(kView, onSpecialCallback);
        puglSetReshapeFunc(kView, onReshapeCallback);
        puglSetCloseFunc(kView, onCloseCallback);

        PuglInternals* impl = kView->impl;

#if DISTRHO_OS_WINDOWS
        //hwnd = impl->hwnd;

        if (parent != nullptr)
        {
            //PuglInternals* parentImpl = parent->kView->impl;
            //SetParent(parentImpl->hwnd, hwnd);
        }
#elif DISTRHO_OS_LINUX
        xDisplay = impl->display;
        xWindow  = impl->win;

        if (parent != nullptr)
        {
            PuglInternals* parentImpl = parent->kView->impl;
            bool     parentWasVisible = parent->isVisible();

            if (parentWasVisible)
            {
                XEvent event;
                XUnmapWindow(xDisplay, xWindow);
                XIfEvent(xDisplay, &event, &isUnmapNotify, (XPointer)&xWindow);
            }

            XSetTransientForHint(xDisplay, xWindow, parentImpl->win);

            if (parentWasVisible)
            {
                XEvent event;
                XMapWindow(xDisplay, xWindow);
                XIfEvent(xDisplay, &event, &isMapNotify, (XPointer)&xWindow);
            }

            XFlush(xDisplay);
        }
#endif

        kApp->addWindow(kSelf);
    }

    ~Private()
    {
        fWidgets.clear();

        if (kView != nullptr)
        {
            kApp->removeWindow(kSelf);
            puglDestroy(kView);
        }
    }

    void exec()
    {
        fClosed = false;
        show();

        if (fParent != nullptr)
        {
#if DISTRHO_OS_WINDOWS
            EnableWindow(fParent->hwnd, FALSE);
#endif
            fParent->fChildFocus = this;
            fParent->show();
        }

        focus();

        while (! fClosed)
        {
            idle();

            if (fParent != nullptr)
                fParent->idle();

            d_msleep(10);
        }

        if (fParent != nullptr)
        {
            fParent->fChildFocus = nullptr;
#if DISTRHO_OS_WINDOWS
            EnableWindow(fParent->hwnd, TRUE);
#endif
        }
    }

    void focus()
    {
#if DISTRHO_OS_WINDOWS
        SetForegroundWindow(hwnd);
        SetActiveWindow(hwnd);
        SetFocus(hwnd);
#elif DISTRHO_OS_LINUX
        XRaiseWindow(xDisplay, xWindow);
        XSetInputFocus(xDisplay, xWindow, RevertToPointerRoot, CurrentTime);
        XFlush(xDisplay);
#endif
    }

    void idle()
    {
        puglProcessEvents(kView);
    }

    void repaint()
    {
        puglPostRedisplay(kView);
    }

    void show()
    {
        setVisible(true);
    }

    void hide()
    {
        setVisible(false);
    }

    bool isVisible()
    {
        return fVisible;
    }

    void setVisible(bool yesNo)
    {
        if (fVisible == yesNo)
            return;

        fVisible = yesNo;

#if DISTRHO_OS_WINDOWS
        if (yesNo)
        {
            ShowWindow(hwnd, WS_VISIBLE);
            ShowWindow(hwnd, SW_RESTORE);
            //SetForegroundWindow(hwnd);
        }
        else
        {
            ShowWindow(hwnd, SW_HIDE);
        }

        UpdateWindow(hwnd);
#elif DISTRHO_OS_LINUX
        XEvent event;

        if (yesNo)
        {
            XMapRaised(xDisplay, xWindow);
            XIfEvent(xDisplay, &event, &isMapNotify, (XPointer)&xWindow);
        }
        else
        {
            XUnmapWindow(xDisplay, xWindow);
            XIfEvent(xDisplay, &event, &isUnmapNotify, (XPointer)&xWindow);
        }

        XFlush(xDisplay);
#endif

        if (yesNo)
            kApp->oneShown();
        else
            kApp->oneHidden();
    }

    void setWindowTitle(const char* title)
    {
#if DISTRHO_OS_WINDOWS
        SetWindowTextA(hwnd, title);
#elif DISTRHO_OS_LINUX
        XStoreName(xDisplay, xWindow, title);
        XFlush(xDisplay);
#endif
    }

    intptr_t getWindowId()
    {
        return puglGetNativeWindow(kView);
    }

protected:
    void onDisplay()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        FOR_EACH_WIDGET(it)
        {
            Widget* widget = *it;
            widget->onDisplay();
        }
    }

    void onKeyboard(bool press, uint32_t key)
    {
        if (fChildFocus != nullptr)
            return fChildFocus->focus();

        FOR_EACH_WIDGET(it)
        {
            Widget* widget = *it;
            widget->onKeyboard(press, key);
        }
    }

    void onMouse(int button, bool press, int x, int y)
    {
        if (fChildFocus != nullptr)
            return fChildFocus->focus();

        FOR_EACH_WIDGET(it)
        {
            Widget* widget = *it;
            widget->onMouse(button, press, x, y);
        }
    }

    void onMotion(int x, int y)
    {
        if (fChildFocus != nullptr)
            return;

        FOR_EACH_WIDGET(it)
        {
            Widget* widget = *it;
            widget->onMotion(x, y);
        }
    }

    void onScroll(float dx, float dy)
    {
        if (fChildFocus != nullptr)
            return;

        FOR_EACH_WIDGET(it)
        {
            Widget* widget = *it;
            widget->onScroll(dx, dy);
        }
    }

    void onSpecial(bool press, Key key)
    {
        if (fChildFocus != nullptr)
            return;

        FOR_EACH_WIDGET(it)
        {
            Widget* widget = *it;
            widget->onSpecial(press, key);
        }
    }

    void onReshape(int width, int height)
    {

        FOR_EACH_WIDGET(it)
        {
            Widget* widget = *it;
            widget->onReshape(width, height);
        }
    }

    void onClose()
    {
        fClosed = true;

        if (fChildFocus != nullptr)
            fChildFocus->onClose();

        FOR_EACH_WIDGET(it)
        {
            Widget* widget = *it;
            widget->onClose();
        }

        hide();
    }

private:
    App::Private* const kApp;
    Window*       const kSelf;
    PuglView*     const kView;

    Private* fParent;
    Private* fChildFocus;
    bool     fVisible;
    bool     fClosed;

    std::list<Widget*> fWidgets;

#if DISTRHO_OS_WINDOWS
    HWND     hwnd;
#elif DISTRHO_OS_LINUX
    Display* xDisplay;
    ::Window xWindow;
#else
    int      _dummy;
#endif

    // Callbacks
    #define handlePtr ((Private*)puglGetHandle(view))

    static void onDisplayCallback(PuglView* view)
    {
        handlePtr->onDisplay();
    }

    static void onKeyboardCallback(PuglView* view, bool press, uint32_t key)
    {
        handlePtr->onKeyboard(press, key);
    }

    static void onMouseCallback(PuglView* view, int button, bool press, int x, int y)
    {
        handlePtr->onMouse(button, press, x, y);
    }

    static void onMotionCallback(PuglView* view, int x, int y)
    {
        handlePtr->onMotion(x, y);
    }

    static void onScrollCallback(PuglView* view, float dx, float dy)
    {
        handlePtr->onScroll(dx, dy);
    }

    static void onSpecialCallback(PuglView* view, bool press, PuglKey key)
    {
        handlePtr->onSpecial(press, static_cast<Key>(key));
    }

    static void onReshapeCallback(PuglView* view, int width, int height)
    {
        handlePtr->onReshape(width, height);
    }

    static void onCloseCallback(PuglView* view)
    {
        handlePtr->onClose();
    }

    #undef handlePtr
};

// -------------------------------------------------
// Window

Window::Window(App* app, Window* parent)
    : kPrivate(new Private(this, app->kPrivate, (parent != nullptr) ? parent->kPrivate : nullptr))
{
}

Window::Window(App* app, intptr_t parentId)
    : kPrivate(new Private(this, app->kPrivate, nullptr, parentId))
{
}

Window::~Window()
{
    delete kPrivate;
}

void Window::exec()
{
    kPrivate->exec();
}

void Window::focus()
{
    kPrivate->focus();
}

void Window::idle()
{
    kPrivate->idle();
}

void Window::repaint()
{
    kPrivate->repaint();
}

bool Window::isVisible()
{
    return kPrivate->isVisible();
}

void Window::setVisible(bool yesNo)
{
    kPrivate->setVisible(yesNo);
}

void Window::setWindowTitle(const char* title)
{
    kPrivate->setWindowTitle(title);
}

intptr_t Window::getWindowId()
{
    return kPrivate->getWindowId();
}

// -------------------------------------------------

END_NAMESPACE_DISTRHO
