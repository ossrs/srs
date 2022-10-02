/* SPDX-License-Identifier: MPL-1.1 OR GPL-2.0-or-later */

/*
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 * 
 * The Original Code is the Netscape Portable Runtime library.
 * 
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are 
 * Copyright (C) 1994-2000 Netscape Communications Corporation.  All
 * Rights Reserved.
 * 
 * Contributor(s):  Silicon Graphics, Inc.
 * 
 * Portions created by SGI are Copyright (C) 2000-2001 Silicon
 * Graphics, Inc.  All Rights Reserved.
 * 
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License Version 2 or later (the
 * "GPL"), in which case the provisions of the GPL are applicable 
 * instead of those above.  If you wish to allow use of your 
 * version of this file only under the terms of the GPL and not to
 * allow others to use your version of this file under the MPL,
 * indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by
 * the GPL.  If you do not delete the provisions above, a recipient
 * may use your version of this file under either the MPL or the
 * GPL.
 */

/*
 * This file is derived directly from Netscape Communications Corporation,
 * and consists of extensive modifications made during the year(s) 1999-2000.
 */

#include <stdlib.h>
#include <errno.h>
#include "common.h"


/*
 * Destructor table for per-thread private data
 */
static _st_destructor_t _st_destructors[ST_KEYS_MAX];
static int key_max = 0;


/*
 * Return a key to be used for thread specific data
 */
int st_key_create(int *keyp, _st_destructor_t destructor)
{
    if (key_max >= ST_KEYS_MAX) {
        errno = EAGAIN;
        return -1;
    }
    
    *keyp = key_max++;
    _st_destructors[*keyp] = destructor;
    
    return 0;
}


int st_key_getlimit(void)
{
    return ST_KEYS_MAX;
}


int st_thread_setspecific(int key, void *value)
{
    _st_thread_t *me = _ST_CURRENT_THREAD();
    return st_thread_setspecific2(me, key, value);
}


int st_thread_setspecific2(_st_thread_t *me, int key, void *value)
{
    if (key < 0 || key >= key_max) {
        errno = EINVAL;
        return -1;
    }
    
    if (value != me->private_data[key]) {
        /* free up previously set non-NULL data value */
        if (me->private_data[key] && _st_destructors[key]) {
            (*_st_destructors[key])(me->private_data[key]);
        }
        me->private_data[key] = value;
    }
    
    return 0;
}


void *st_thread_getspecific(int key)
{
    if (key < 0 || key >= key_max)
        return NULL;
    
    return ((_ST_CURRENT_THREAD())->private_data[key]);
}


/*
 * Free up all per-thread private data
 */
void _st_thread_cleanup(_st_thread_t *thread)
{
    int key;
    
    for (key = 0; key < key_max; key++) {
        if (thread->private_data[key] && _st_destructors[key]) {
            (*_st_destructors[key])(thread->private_data[key]);
            thread->private_data[key] = NULL;
        }
    }
}

