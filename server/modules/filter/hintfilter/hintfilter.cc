/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "hintfilter"

#include <stdio.h>
#include <maxscale/filter.hh>
#include <maxscale/alloc.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include "mysqlhint.hh"

/**
 * hintfilter.c - a filter to parse the MaxScale hint syntax and attach those
 * hints to the buffers that carry the requests.
 *
 */

static MXS_FILTER*         createInstance(const char* name, MXS_CONFIG_PARAMETER* params);
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session);
static void                closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void                freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void                setDownstream(MXS_FILTER* instance,
                                         MXS_FILTER_SESSION* fsession,
                                         MXS_DOWNSTREAM* downstream);
static int      routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, GWBUF* queue);
static void     diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb);
static json_t*  diagnostic_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession);
static uint64_t getCapabilities(MXS_FILTER* instance);

extern "C"
{

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
    MXS_MODULE* MXS_CREATE_MODULE()
    {
        static MXS_FILTER_OBJECT MyObject =
        {
            createInstance,
            newSession,
            closeSession,
            freeSession,
            setDownstream,
            NULL,   // No upstream requirement
            routeQuery,
            NULL,   // No clientReply
            diagnostic,
            diagnostic_json,
            getCapabilities,
            NULL,   // No destroyInstance
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_FILTER,
            MXS_MODULE_ALPHA_RELEASE,
            MXS_FILTER_VERSION,
            "A hint parsing filter",
            "V1.0.0",
            RCAP_TYPE_CONTIGUOUS_INPUT,
            &MyObject,
            NULL,   /* Process init. */
            NULL,   /* Process finish. */
            NULL,   /* Thread init. */
            NULL,   /* Thread finish. */
            {
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param name      The name of the instance (as defined in the config file).
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static MXS_FILTER* createInstance(const char* name, MXS_CONFIG_PARAMETER* params)
{
    return static_cast<MXS_FILTER*>(new(std::nothrow) HINT_INSTANCE);
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session)
{
    return static_cast<MXS_FILTER_SESSION*>(new(std::nothrow) HINT_SESSION);
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    HINT_SESSION* my_session = (HINT_SESSION*)session;

    for (auto& a : my_session->named_hints)
    {
        hint_free(a.second);
    }

    for (auto& a : my_session->stack)
    {
        hint_free(a);
    }
}

/**
 * Free the memory associated with this filter session.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    delete session;
}

/**
 * Set the downstream component for this filter.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 * @param downstream    The downstream filter or router
 */
static void setDownstream(MXS_FILTER* instance, MXS_FILTER_SESSION* session, MXS_DOWNSTREAM* downstream)
{
    HINT_SESSION* my_session = (HINT_SESSION*)session;

    my_session->down = *downstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
static int routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* queue)
{
    HINT_SESSION* my_session = (HINT_SESSION*)session;

    if (modutil_is_SQL(queue) && gwbuf_length(queue) > 5)
    {
        my_session->process_hints(queue);
    }

    /* Now process the request */
    return my_session->down.routeQuery(my_session->down.instance,
                                       my_session->down.session,
                                       queue);
}

/**
 * Diagnostics routine
 *
 * If fsession is NULL then print diagnostics on the filter
 * instance as a whole, otherwise print diagnostics for the
 * particular session.
 *
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 * @param   dcb     The DCB for diagnostic output
 */
static void diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb)
{
}

/**
 * Diagnostics routine
 *
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 */
static json_t* diagnostic_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession)
{
    return NULL;
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(MXS_FILTER* instance)
{
    return RCAP_TYPE_NONE;
}
