/*
 * Copyright (C) 2020-2021 Nikita Melekhin. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Devices.h"
#include <fcntl.h>
#include <libfoundation/Logger.h>

namespace WinServer {

static Devices* s_the;

Devices::Devices()
{
    s_the = this;
    m_mouse_fd = open("/dev/mouse", O_RDONLY);
    if (m_mouse_fd < 0) {
        Logger::debug << "Can't open mouse" << std::endl;
        std::abort();
    }

    m_keyboard_fd = open("/dev/kbd", O_RDONLY);
    if (m_keyboard_fd < 0) {
        Logger::debug << "Can't open keyboard" << std::endl;
        std::abort();
    }

    LFoundation::EventLoop::the().add(
        m_mouse_fd, [] {
            Devices::the().pump_mouse();
        },
        nullptr);

    LFoundation::EventLoop::the().add(
        m_keyboard_fd, [] {
            Devices::the().pump_keyboard();
        },
        nullptr);
}

Devices& Devices::the()
{
    return *s_the;
}

} // namespace WinServer