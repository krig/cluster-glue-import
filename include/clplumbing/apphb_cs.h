/*
 * Copyright (C) 2002 Alan Robertson <alanr@unix.sh>
 * This software licensed under the GNU LGPL.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _CLPLUMBING_APPHB_CS_H
#define _CLPLUMBING_APPHB_CS_H

/* Internal client-server messages for APP heartbeat service */

#define APPHBSOCKPATH		"/var/lib/heartbeat/apphb.comm"

#define APPHB_TLEN	8
#define APPHB_OLEN	256

#define	REGISTER	"reg"
#define	UNREGISTER	"unreg"
#define	HEARTBEAT	"hb"
#define	SETINTERVAL	"setint"

/*
 * These messages are really primitive.
 * They don't have any form of version control, they're in host byte order,
 * and they're all in binary...
 *
 * Fortunately, this is a very simple local service ;-)
 */

/* Generic (no parameter) App heartbeat message */
struct apphb_msg {
	char msgtype [APPHB_TLEN];
};

/* App heartbeat Registration message */
struct apphb_signupmsg {
	char msgtype [APPHB_TLEN];
	char appname [APPHB_OLEN];
	char appinstance [APPHB_OLEN];
	pid_t	pid;
	uid_t	uid;
	gid_t	gid;
};

/* App heartbeat setinterval message */
struct apphb_msmsg {
	char	msgtype [APPHB_TLEN];
	int	ms;
};

/* App heartbeat server return code (errno) */
struct apphb_rc {
	int	rc;
};
#endif
