/*
 * File I/O extension to the State Threads Library.
 */

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
 * The Original Code is the file I/O extension to the State Threads Library.
 * 
 * The Initial Developer of the Original Code is Jeff
 * <jlb-st@houseofdistraction.com>.  Portions created by the Initial
 * Developer are Copyright (C) 2002 the Initial Developer.  All Rights
 * Reserved.
 * 
 * Contributor(s): (none)
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

#ifndef __STX_FILEIO_H__
#define __STX_FILEIO_H__

#include <st.h>

#ifdef __cplusplus
extern "C" {
#endif

extern ssize_t stx_file_read(st_netfd_t fd, off_t offset, void *buf, size_t nbytes, st_utime_t timeout);

#ifdef __cplusplus
}
#endif
#endif /* !__STX_FILEIO_H__ */
