/*
 * Copyright (C) 2020 Nikita Melekhin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include "EventLoop.h"
#include <memory.h>
#include <string.h>
#include <sys/time.h>
#include <syscalls.h>

namespace LFoundation {

static EventLoop* s_the = 0;

EventLoop& EventLoop::the()
{
    return *s_the;
}

EventLoop::EventLoop()
{
    s_the = this;
}

void EventLoop::check_fds()
{
    if (m_waiting_fds.size() == 0) {
        return;
    }
    fd_set_t readfds;
    fd_set_t writefds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    int nfds = -1;
    for (int i = 0; i < m_waiting_fds.size(); i++) {
        if (m_waiting_fds[i].m_on_read) {
            FD_SET(m_waiting_fds[i].m_fd, &readfds);
        }
        if (m_waiting_fds[i].m_on_write) {
            FD_SET(m_waiting_fds[i].m_fd, &writefds);
        }
        if (nfds < m_waiting_fds[i].m_fd) {
            nfds = m_waiting_fds[i].m_fd;
        }
    }

    // For now, that means, that we don't wait for fds.
    timeval_t timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    int res = select(nfds + 1, &readfds, &writefds, nullptr, &timeout);

    for (int i = 0; i < m_waiting_fds.size(); i++) {
        if (m_waiting_fds[i].m_on_read) {
            if (FD_ISSET(m_waiting_fds[i].m_fd, &readfds)) {
                m_event_queue.push_back(QueuedEvent(m_waiting_fds[i], new FDWaiterReadEvent()));
            }
        }
        if (m_waiting_fds[i].m_on_write) {
            if (FD_ISSET(m_waiting_fds[i].m_fd, &writefds)) {
                m_event_queue.push_back(QueuedEvent(m_waiting_fds[i], new FDWaiterWriteEvent()));
            }
        }
    }
}

void EventLoop::check_timers()
{
    for (int i = 0; i < m_timers.size(); i++) {
        if (m_timers[i].expired()) {
            m_event_queue.push_back(QueuedEvent(m_timers[i], new TimerEvent()));
        }
    }
}

void EventLoop::pump()
{
    check_fds();
    check_timers();
    Vector<QueuedEvent> events_to_dispatch(move(m_event_queue));
    for (int i = 0; i < events_to_dispatch.size(); i++) {
        events_to_dispatch[i].receiver.receive_event(move(events_to_dispatch[i].event));
    }
    if (!events_to_dispatch.size()) {
        sched_yield();
    }
}

int EventLoop::run()
{
    while (!m_stop_flag) {
        pump();
    }
    return m_exit_code;
}

}