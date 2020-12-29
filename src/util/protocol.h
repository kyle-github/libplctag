/***************************************************************************
 *   Copyright (C) 2020 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 * This software is available under either the Mozilla Public License      *
 * version 2.0 or the GNU LGPL version 2 (or later) license, whichever     *
 * you choose.                                                             *
 *                                                                         *
 * MPL 2.0:                                                                *
 *                                                                         *
 *   This Source Code Form is subject to the terms of the Mozilla Public   *
 *   License, v. 2.0. If a copy of the MPL was not distributed with this   *
 *   file, You can obtain one at http://mozilla.org/MPL/2.0/.              *
 *                                                                         *
 *                                                                         *
 * LGPL 2:                                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#pragma once

#include <util/attr.h>
#include <util/mutex.h>
#include <util/slice.h>

typedef struct protocol_s *protocol_p;
struct protocol_s {
    struct protocol_s *next;

    mutex_p mutex;
    protocol_request_p request_list;

    const char *stack_type;
    const char *host;
    const char *path;
};

typedef struct protocol_request_s *protocol_request_p;
struct protocol_request_s {
    struct protocol_request *next;
    void *client;
    slice_t (*build_request_callback)(slice_t output_buffer, void *client);
    int (*handle_response_callback)(slice_t input_buffer, void *client);
    int (*abort_callback)(void *client);
};

extern int protocol_get(const char *stack_type, attr attribs, protocol_p *protocol, int (*constructor)(attr attribs, protocol_p *protocol));
extern int protocol_cleanup(protocol_p protocol);

extern int protocol_request_init(protocol_p protocol, protocol_request_p req);
extern int protocol_request_start(protocol_p protocol, protocol_request_p req, void *client,
                                  slice_t (*build_request_callback)(slice_t output_buffer, void *client),
                                  int (*handle_response_callback)(slice_t input_buffer, void *client));
extern int protocol_request_abort(protocol_p plc, protocol_request_p req);

extern int protocol_module_init(void);
extern void protocol_module_teardown(void);
