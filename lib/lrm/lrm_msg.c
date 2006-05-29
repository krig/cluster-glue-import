/* $Id: lrm_msg.c,v 1.26 2006/05/29 11:55:53 andrew Exp $ */
/*
 * Message  Functions  For Local Resource Manager
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

/*
 * By Huang Zhen <zhenh@cn.ibm.com> 2004/2/13
 *
 */
#include <portability.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <clplumbing/cl_log.h>
#include <ha_msg.h>
#include <heartbeat.h>
#include <lrm/lrm_api.h>
#include <lrm/lrm_msg.h>
#define LOG_BASIC_ERROR(apiname)	\
	cl_log(LOG_ERR, "%s(%d): %s failed.", __FUNCTION__, __LINE__, apiname)

const lrm_op_t	lrm_zero_op; /* Default initialized to zeros */

static void
copy_pair(gpointer key, gpointer value, gpointer user_data)
{
	GHashTable* taget_table = (GHashTable*)user_data;
	g_hash_table_insert(taget_table, g_strdup(key), g_strdup(value));
}

GHashTable*
copy_str_table(GHashTable* src_table)
{
	GHashTable* target_table = NULL;

	if ( NULL == src_table) {
		return NULL;
	}
	target_table = g_hash_table_new(g_str_hash, g_str_equal);
	g_hash_table_foreach(src_table, copy_pair, target_table);
	return target_table;
}

static void
merge_pair(gpointer key, gpointer value, gpointer user_data)
{
	GHashTable *merged = (GHashTable*)user_data;

	if (g_hash_table_lookup(merged, key)) {
		return;

	} else if(strncmp(key, "CRM_meta_" /*CRM_META*/, 9) == 0) {
		/* Never repopulate CRM meta attributes */
		return;
	}	
	g_hash_table_insert(merged, g_strdup(key), g_strdup(value));
}

GHashTable*
merge_str_tables(GHashTable* old, GHashTable* new)
{
	GHashTable* merged = NULL;
	if ( NULL == old ) {
		return copy_str_table(new);
	}
	if ( NULL == new ) {
		return copy_str_table(old);
	}
	merged = copy_str_table(new);
	g_hash_table_foreach(old, merge_pair, merged);
	return merged;
}

static gboolean
free_pair(gpointer key, gpointer value, gpointer user_data)
{
	g_free(key);
	g_free(value);
	return TRUE;
}

void
free_str_table(GHashTable* hash_table)
{
	g_hash_table_foreach_remove(hash_table, free_pair, NULL);
	g_hash_table_destroy(hash_table);
}



struct ha_msg*
create_lrm_msg (const char* msg)
{
	struct ha_msg* ret;
	if ((NULL == msg) || (0 == strlen(msg))) {
		return NULL;
	}

	ret = ha_msg_new(1);
	if (HA_OK != ha_msg_add(ret, F_LRM_TYPE, msg)) {
		ha_msg_del(ret);
		LOG_BASIC_ERROR("ha_msg_add");
		return NULL;
	}

	return ret;
}

struct ha_msg*
create_lrm_reg_msg(const char* app_name)
{
	struct ha_msg* ret;
	if ((NULL == app_name) || (0 == strlen(app_name))) {
		return NULL;
	}

	ret = ha_msg_new(5);

	if(HA_OK != ha_msg_add(ret, F_LRM_TYPE, REGISTER)
	|| HA_OK != ha_msg_add(ret, F_LRM_APP, app_name)
	|| HA_OK != ha_msg_add_int(ret, F_LRM_PID, getpid())
	|| HA_OK != ha_msg_add_int(ret, F_LRM_GID, getegid())
	|| HA_OK != ha_msg_add_int(ret, F_LRM_UID, getuid())) {
		ha_msg_del(ret);
		LOG_BASIC_ERROR("ha_msg_add");
		return NULL;
	}
	
	return ret;
}

struct ha_msg*
create_lrm_addrsc_msg(const char* rid, const char* class, const char* type,
			const char* provider, GHashTable* params)
{
	struct ha_msg* msg;
	if (NULL==rid||NULL==class||NULL==type) {
		return NULL;
	}
	
	msg = ha_msg_new(5);
	if(HA_OK != ha_msg_add(msg, F_LRM_TYPE, ADDRSC)
	|| HA_OK != ha_msg_add(msg, F_LRM_RID, rid)
	|| HA_OK != ha_msg_add(msg, F_LRM_RCLASS, class)
	|| HA_OK != ha_msg_add(msg, F_LRM_RTYPE, type)) {
		ha_msg_del(msg);
		LOG_BASIC_ERROR("ha_msg_add");
		return NULL;
	}
		
	if( provider ) {
		if (HA_OK != ha_msg_add(msg, F_LRM_RPROVIDER, provider)) {
			ha_msg_del(msg);
			LOG_BASIC_ERROR("ha_msg_add");
			return NULL;
		}
	}
	
	if ( params ) {
		if (HA_OK != ha_msg_add_str_table(msg,F_LRM_PARAM,params)) {
			ha_msg_del(msg);
			LOG_BASIC_ERROR("ha_msg_add");
			return NULL;
		}
	}
	return msg;
}


struct ha_msg*
create_lrm_rsc_msg(const char* rid, const char* msg)
{
	struct ha_msg* ret;
	if ((NULL == rid) ||(NULL == msg) || (0 == strlen(msg))) {
		return NULL;
	}

	ret = ha_msg_new(2);
	if(HA_OK != ha_msg_add(ret, F_LRM_TYPE, msg)
	|| HA_OK != ha_msg_add(ret, F_LRM_RID, rid)) {
		ha_msg_del(ret);
		LOG_BASIC_ERROR("ha_msg_add");
		return NULL;
	}
	return ret;
}



struct ha_msg*
create_lrm_ret(int ret, int fields)
{
	struct ha_msg* msg = ha_msg_new(fields);
	if(HA_OK != ha_msg_add(msg, F_LRM_TYPE, RETURN)
	|| HA_OK != ha_msg_add_int(msg, F_LRM_RET, ret)) {
		ha_msg_del(msg);
		LOG_BASIC_ERROR("ha_msg_add");
		return NULL;
	}
	return msg;
}

/* 
 * $Log: lrm_msg.c,v $
 * Revision 1.26  2006/05/29 11:55:53  andrew
 * Fix for OSDL #1273
 *   This patch reverses the copy order for better performance and never
 *   (re)populates the hashtable with attributes starting with "CRM_meta_"
 *
 * Revision 1.25  2005/06/02 01:07:33  zhenh
 * 1. improve some names of internal functions.
 * 2. remove the useless "unregister" message.
 *
 * Revision 1.24  2005/06/01 09:37:16  sunjd
 * Bug 495: log message tweak
 *
 * Revision 1.23  2005/05/30 08:16:58  sunjd
 * fix the mis-use of STRLEN_CONST
 *
 * Revision 1.22  2005/04/22 06:08:50  alan
 * Put in a fix for an uninitialized variable -- added a new
 * const lrm_op_t object lrm_zero_op - which can be used as an initializer for
 * lrm_op_t objects so this doesn't happen.
 *
 * Revision 1.21  2005/04/07 07:34:50  sunjd
 * use STRLEN_CONST & STRNCMP_CONST instead
 *
 * Revision 1.20  2004/12/05 04:32:50  gshi
 * Moved some message-related functions from lrm_msg.c to cl_msg.c
 * These functions are general and shall be available to other subsystems
 *
 * Revision 1.19  2004/12/03 02:24:08  zhenh
 * make the ha_msg_value_int() endian-independence
 *
 * Revision 1.18  2004/09/27 08:33:55  zhenh
 * apply the new cl_msg_list_xxx() funcions in lrm
 *
 * Revision 1.17  2004/09/17 03:33:24  zhenh
 * in some platform(maybe 64bits), using int as size_t causes warning.
 *
 * Revision 1.16  2004/09/13 07:10:30  zhenh
 * fix a bug: the msg does not contain the request field so returning NULL is correct. not an error. remove the wrong log
 *
 * Revision 1.15  2004/09/10 10:09:43  sunjd
 * Fix a bug: duplicate keys in GHashtable, is not expected
 *
 * Revision 1.14  2004/09/10 02:07:16  zhenh
 * make names of functions more clear,fix some bug and  make it more robust
 *
 * Revision 1.13  2004/09/03 01:07:08  zhenh
 * add provider for resource
 *
 * Revision 1.12  2004/08/29 04:38:08  msoffen
 * Added log for history to end of file.
 *
 */
