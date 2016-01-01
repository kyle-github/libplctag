/***************************************************************************
 *   Copyright (C) 2015 by OmanTek                                         *
 *   Author Kyle Hayes  kylehayes@omantek.com                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

/**************************************************************************
 * CHANGE LOG                                                             *
 *                                                                        *
 * 2013-11-19  KRH - Created file.                                        *
 **************************************************************************/

/*#ifdef __cplusplus
extern "C"
{
#endif
*/

/*#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
*/
#include <errno.h>
#include <platform.h>
#include <libplctag.h>
#include <libplctag_tag.h>
#include <ab/connection.h>
#include <ab/session.h>
#include <ab/tag.h>
#include <ab/eip.h>
#include <ab/ab.h>
#include <util/attr.h>

/*
 * Shared global data
 */



int find_or_create_connection(ab_tag_p tag, ab_session_p session, attr attribs)
{
    int debug = attr_get_int(attribs,"debug",0);
    const char* path = attr_get_str(attribs, "path", "");
    ab_connection_p connection = AB_CONNECTION_NULL;
    int rc = PLCTAG_STATUS_OK;
    int is_new = 0;

    pdebug(debug, "Starting.");

    /* lock the session while this is happening because we do not
     * want a race condition where two tags try to create the same
     * connection at the same time.
     */

    pdebug(debug,"entering critical block %p",global_session_mut);
    critical_block(global_session_mut) {
        connection = session_find_connection_by_path_unsafe(session, path);

        if (connection == AB_CONNECTION_NULL) {
            connection = connection_create_unsafe(debug, path, session);
            is_new = 1;
        } else {
            /* found a connection, nothing more to do. */
            pdebug(debug, "find_or_create_connection() reusing existing connection.");
            rc = PLCTAG_STATUS_OK;
        }
    }
    pdebug(debug,"leaving critical block %p",global_session_mut);

    if (connection == AB_CONNECTION_NULL) {
        pdebug(debug, "unable to create or find a connection!");
        rc = PLCTAG_ERR_BAD_GATEWAY;
        return rc;
    } else if(is_new) {
        /* only do this if this is a new connection. */

        /* copy path data from the tag */
        mem_copy(connection->conn_path, tag->conn_path, tag->conn_path_size);
        connection->conn_path_size = tag->conn_path_size;

        /* do the ForwardOpen call to set up the session */
        if((rc = connection_perform_forward_open(connection)) != PLCTAG_STATUS_OK) {
            pdebug(debug, "Unable to perform ForwardOpen to set up connection with PLC!");

            /*critical_block(global_session_mut) {
                connection_destroy_unsafe(connection);
            }

            tag->connection = NULL;

            return rc;*/
        }
    }

    tag->connection = connection;

    pdebug(debug, "Done.");

    return rc;
}


/* not thread safe! */
ab_connection_p connection_create_unsafe(int debug, const char* path, ab_session_p session)
{
    ab_connection_p connection = (ab_connection_p)mem_alloc(sizeof(struct ab_connection_t));

    pdebug(debug, "Starting.");

    if (!connection) {
        pdebug(debug, "Unable to allocate new connection!");
        return NULL;
    }

    connection->debug = debug;
    connection->session = session;
    connection->conn_seq_num = 1 /*(uint16_t)(intptr_t)(connection)*/;
    connection->orig_connection_id = ++session->conn_serial_number;
    connection->status = PLCTAG_STATUS_PENDING;

    /* copy the path for later */
    str_copy(&connection->path[0], path, MAX_CONN_PATH);

    /* add the connection to the session */
    session_add_connection_unsafe(session, connection);

    pdebug(debug, "Done.");

    return connection;
}



int connection_perform_forward_open(ab_connection_p connection)
{
    int debug = connection->debug;
    ab_request_p req;
    uint64_t timeout_time;
    int rc = PLCTAG_STATUS_OK;

    pdebug(debug, "Starting.");

    /* get a request buffer */
    rc = request_create(&req);

    do {
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(debug,"Unable to get new request.  rc=%d",rc);
            rc = 0;
            break;
        }

        /* send the ForwardOpen command to the PLC */
        if((rc = send_forward_open_req(connection, req)) != PLCTAG_STATUS_OK) {
            pdebug(connection->debug,"Unable to send ForwardOpen packet!");
            break;
        }

        /* wait for a response */
        timeout_time = time_ms() + 5000; /* MAGIC five seconds */

        while (timeout_time > time_ms() && !req->resp_received) {
            sleep_ms(1);
        }

        /* timeout? */
        if(!req->resp_received) {
            pdebug(debug,"Timed out waiting for ForwardOpen response!");
            rc = PLCTAG_ERR_TIMEOUT_ACK;
            break;
        }

        /* wait for the ForwardOpen response. */
        if((rc = recv_forward_open_resp(connection, req)) != PLCTAG_STATUS_OK) {
            pdebug(debug,"Unable to use ForwardOpen response!");
            rc = PLCTAG_ERR_REMOTE_ERR;
            break;
        }
    } while(0);

    connection->status = rc;

    if(req) {
        request_destroy(&req);
    }

    pdebug(debug, "Done.");

    return rc;
}


int send_forward_open_req(ab_connection_p connection, ab_request_p req)
{
    eip_forward_open_request_t *fo;
    uint8_t *data;
    int rc = PLCTAG_STATUS_OK;
    int debug = connection->debug;

    pdebug(debug,"Starting");

    req->debug = debug;

    fo = (eip_forward_open_request_t*)(req->data);

    /* point to the end of the struct */
    data = (req->data) + sizeof(eip_forward_open_request_t);

    /* set up the path information. */
    mem_copy(data, connection->conn_path, connection->conn_path_size);
    data += connection->conn_path_size;

    /* fill in the static parts */

    /* encap header parts */
    fo->encap_command = h2le16(AB_EIP_READ_RR_DATA); /* 0x006F EIP Send RR Data command */
    fo->encap_length =
        h2le16(data - (uint8_t*)(&fo->interface_handle)); /* total length of packet except for encap header */
    fo->router_timeout = h2le16(1);                       /* one second is enough ? */

    /* CPF parts */
    fo->cpf_item_count = h2le16(2);                  /* ALWAYS 2 */
    fo->cpf_nai_item_type = h2le16(AB_EIP_ITEM_NAI); /* null address item type */
    fo->cpf_nai_item_length = h2le16(0);             /* no data, zero length */
    fo->cpf_udi_item_type = h2le16(AB_EIP_ITEM_UDI); /* unconnected data item, 0x00B2 */
    fo->cpf_udi_item_length =
        h2le16(data - (uint8_t*)(&fo->cm_service_code)); /* length of remaining data in UC data item */

    /* Connection Manager parts */
    fo->cm_service_code = AB_EIP_CMD_FORWARD_OPEN; /* 0x54 Forward Open Request */
    fo->cm_req_path_size = 2;                      /* size of path in 16-bit words */
    fo->cm_req_path[0] = 0x20;                     /* class */
    fo->cm_req_path[1] = 0x06;                     /* CM class */
    fo->cm_req_path[2] = 0x24;                     /* instance */
    fo->cm_req_path[3] = 0x01;                     /* instance 1 */

    /* Forward Open Params */
    fo->secs_per_tick = AB_EIP_SECS_PER_TICK;         /* seconds per tick, no used? */
    fo->timeout_ticks = AB_EIP_TIMEOUT_TICKS;         /* timeout = srd_secs_per_tick * src_timeout_ticks, not used? */
    fo->orig_to_targ_conn_id = h2le32(0);             /* is this right?  Our connection id or the other machines? */
    fo->targ_to_orig_conn_id = h2le32(connection->orig_connection_id); /* connection id in the other direction. */
    fo->conn_serial_number = h2le16((uint16_t)(intptr_t)(connection)); /* our connection SEQUENCE number. */
    fo->orig_vendor_id = h2le16(AB_EIP_VENDOR_ID);               /* our unique :-) vendor ID */
    fo->orig_serial_number = h2le32(AB_EIP_VENDOR_SN);           /* our serial number. */
    fo->conn_timeout_multiplier = AB_EIP_TIMEOUT_MULTIPLIER;     /* timeout = mult * RPI */
    fo->orig_to_targ_rpi = h2le32(AB_EIP_RPI); /* us to target RPI - Request Packet Interval in microseconds */
    fo->orig_to_targ_conn_params = h2le16(AB_EIP_PLC5_PARAM); /* FIXME - must be based on PLC type. what kind of PLC we are targetting, PLC5 */
    fo->targ_to_orig_rpi = h2le32(AB_EIP_RPI); /* target to us RPI - not really used for explicit messages? */
    fo->targ_to_orig_conn_params = h2le16(AB_EIP_PLC5_PARAM); /* FIXME - must be based on PLC type. We are pretending to be a LGX. */
    fo->transport_class = AB_EIP_TRANSPORT_CLASS_T3; /* 0xA3, server transport, class 3, application trigger */
    fo->path_size = connection->conn_path_size/2; /* size in 16-bit words */

    /* set the size of the request */
    req->request_size = data - (req->data);

    /* mark it as ready to send */
    req->send_request = 1;

    /* add the request to the session's list. */
    rc = request_add(connection->session, req);

    pdebug(debug, "Done");

    return rc;
}


int recv_forward_open_resp(ab_connection_p connection, ab_request_p req)
{
    eip_forward_open_response_t *fo_resp;
    int rc = PLCTAG_STATUS_OK;
    int debug = connection->debug;

    pdebug(debug,"Starting");

    fo_resp = (eip_forward_open_response_t*)(req->data);

    do {
        if(le2h16(fo_resp->encap_command) != AB_EIP_READ_RR_DATA) {
            pdebug(debug,"Unexpected EIP packet type received: %d!",fo_resp->encap_command);
            rc = PLCTAG_ERR_BAD_DATA;
            break;
        }

        if(le2h16(fo_resp->encap_status) != AB_EIP_OK) {
            pdebug(debug,"EIP command failed, response code: %d",fo_resp->encap_status);
            rc = PLCTAG_ERR_REMOTE_ERR;
            break;
        }

        if(fo_resp->general_status != AB_EIP_OK) {
            pdebug(debug,"Forward Open command failed, response code: %d",fo_resp->general_status);
            rc = PLCTAG_ERR_REMOTE_ERR;
            break;
        }

        connection->orig_connection_id = le2h32(fo_resp->orig_to_targ_conn_id);
        connection->targ_connection_id = le2h32(fo_resp->targ_to_orig_conn_id);
        connection->is_connected = 1;

        pdebug(debug,"Connection set up succeeded.");

        connection->status = PLCTAG_STATUS_OK;
        rc = PLCTAG_STATUS_OK;
    } while(0);

    pdebug(debug,"Done.");

    return rc;
}

int connection_add_tag_unsafe(ab_connection_p connection, ab_tag_p tag)
{
    pdebug(connection->debug, "Starting");

    tag->next = connection->tags;
    connection->tags = tag;

    pdebug(connection->debug, "Done");

    return PLCTAG_STATUS_OK;
}


int connection_add_tag(ab_connection_p connection, ab_tag_p tag)
{
    int rc = PLCTAG_STATUS_OK;
    int debug = connection->debug;

    if(connection) {
        pdebug(debug,"entering critical block %p",global_session_mut);
        critical_block(global_session_mut) {
            rc = connection_add_tag_unsafe(connection, tag);
        }
        pdebug(debug,"leaving critical block %p",global_session_mut);

    } else {
        rc = PLCTAG_ERR_NULL_PTR;
    }

    return rc;
}

int connection_remove_tag_unsafe(ab_connection_p connection, ab_tag_p tag)
{
    ab_tag_p cur;
    ab_tag_p prev;
    int debug = tag->debug;
    int rc;

    cur = connection->tags;
    prev = NULL;

    while (cur && cur != tag) {
        prev = cur;
        cur = cur->next;
    }

    if (cur == tag) {
        if (prev) {
            prev->next = cur->next;
        } else {
            connection->tags = cur->next;
        }

        tag->connection = NULL;

        rc = PLCTAG_STATUS_OK;
    } else {
        rc = PLCTAG_ERR_NOT_FOUND;
    }

    if (connection_empty_unsafe(connection)) {
        pdebug(debug, "destroying connection");
        connection_destroy_unsafe(connection);
    }

    return rc;
}

int connection_remove_tag(ab_connection_p connection, ab_tag_p tag)
{
    int rc = PLCTAG_STATUS_OK;
    int debug = connection->debug;

    if(connection && connection->session) {
        pdebug(debug,"entering critical block %p",global_session_mut);
        critical_block(global_session_mut) {
            rc = connection_remove_tag_unsafe(connection, tag);
        }
        pdebug(debug,"leaving critical block %p",global_session_mut);
    } else {
        rc = PLCTAG_ERR_NULL_PTR;
    }

    return rc;
}


int connection_empty_unsafe(ab_connection_p connection)
{
    if(!connection) {
        return 1;
    }

    return (connection->tags == NULL);
}


int connection_is_empty(ab_connection_p connection)
{
    int rc = PLCTAG_STATUS_OK;

    critical_block(global_session_mut) {
        rc = connection_empty_unsafe(connection);
    }

    return rc;
}

int connection_destroy_unsafe(ab_connection_p connection)
{
    int debug = connection->debug;

    pdebug(debug, "Starting.");

    if (!connection) {
        return 1;
    }

    /* do not destroy the connection if there are
     * connections still */
    if (connection->tags) {
        pdebug(debug, "Attempt to destroy connection while open tags exist!");
        return 0;
    }

    if (connection_close(connection)) {
        return 0;
    }

    /* call the mutex protected version */
    session_remove_connection_unsafe(connection->session, connection);

    mem_free(connection);

    pdebug(debug, "Done.");

    return 1;
}

int connection_close(ab_connection_p connection)
{
    int debug = connection->debug;
    ab_request_p req;
    int rc = PLCTAG_STATUS_OK;

    pdebug(debug, "Starting.");

    /* get a request buffer */
    rc = request_create(&req);

    do {
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(debug,"Unable to get new request.  rc=%d",rc);
            rc = 0;
            break;
        }

        /* send the ForwardClose command to the PLC */
        if((rc = send_forward_close_req(connection, req)) != PLCTAG_STATUS_OK) {
            pdebug(connection->debug,"Unable to send ForwardClose packet!");
            break;
        }
    } while(0);

    connection->status = rc;

    pdebug(debug, "Done.");

    return rc;
}


int send_forward_close_req(ab_connection_p connection, ab_request_p req)
{
    eip_forward_close_req_t *fo;
    uint8_t *data;
    int rc = PLCTAG_STATUS_OK;
    int debug = connection->debug;

    pdebug(debug,"Starting");

    req->debug = debug;

    fo = (eip_forward_close_req_t*)(req->data);

    /* point to the end of the struct */
    data = (req->data) + sizeof(eip_forward_close_req_t);

    /* set up the path information. */
    mem_copy(data, connection->conn_path, connection->conn_path_size);
    data += connection->conn_path_size;

    /* fill in the static parts */

    /* encap header parts */
    fo->encap_command = h2le16(AB_EIP_READ_RR_DATA); /* 0x006F EIP Send RR Data command */
    fo->encap_length =
        h2le16(data - (uint8_t*)(&fo->interface_handle)); /* total length of packet except for encap header */
    fo->router_timeout = h2le16(1);                       /* one second is enough ? */

    /* CPF parts */
    fo->cpf_item_count = h2le16(2);                  /* ALWAYS 2 */
    fo->cpf_nai_item_type = h2le16(AB_EIP_ITEM_NAI); /* null address item type */
    fo->cpf_nai_item_length = h2le16(0);             /* no data, zero length */
    fo->cpf_udi_item_type = h2le16(AB_EIP_ITEM_UDI); /* unconnected data item, 0x00B2 */
    fo->cpf_udi_item_length =
        h2le16(data - (uint8_t*)(&fo->cm_service_code)); /* length of remaining data in UC data item */

    /* Connection Manager parts */
    fo->cm_service_code = AB_EIP_CMD_FORWARD_CLOSE;/* 0x4E Forward Close Request */
    fo->cm_req_path_size = 2;                      /* size of path in 16-bit words */
    fo->cm_req_path[0] = 0x20;                     /* class */
    fo->cm_req_path[1] = 0x06;                     /* CM class */
    fo->cm_req_path[2] = 0x24;                     /* instance */
    fo->cm_req_path[3] = 0x01;                     /* instance 1 */

    /* Forward Open Params */
    fo->secs_per_tick = AB_EIP_SECS_PER_TICK;         /* seconds per tick, no used? */
    fo->timeout_ticks = AB_EIP_TIMEOUT_TICKS;         /* timeout = srd_secs_per_tick * src_timeout_ticks, not used? */
    fo->conn_serial_number = h2le16((uint16_t)(intptr_t)(connection)); /* our connection SEQUENCE number. */
    fo->orig_vendor_id = h2le16(AB_EIP_VENDOR_ID);               /* our unique :-) vendor ID */
    fo->orig_serial_number = h2le32(AB_EIP_VENDOR_SN);           /* our serial number. */
    fo->path_size = connection->conn_path_size/2; /* size in 16-bit words */

    /* set the size of the request */
    req->request_size = data - (req->data);

    /* mark it as ready to send */
    req->send_request = 1;
    req->abort_after_send = 1; /* don't return to us.*/

    /* add the request to the session's list. */
    rc = request_add_unsafe(connection->session, req);

    pdebug(debug, "Done");

    return rc;
}
