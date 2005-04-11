/*
 * Stonith module for IBM Hardware Management Console (HMC)
 *
 * Author: Huang Zhen <zhenh@cn.ibm.com>
 * Support for HMC V4+ added by Dave Blaschke <debltc@us.ibm.com>
 *
 * Copyright (c) 2004 International Business Machines
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
 *
 * This code has been tested in following environment:
 *
 *	Hardware Management Console (HMC): Release 3, Version 2.4
 *	- Both FullSystemPartition and LPAR Partition:
 *		- p630 7028-6C4 two LPAR partitions
 *		- p650 7038-6M2 one LPAR partition and FullSystemPartition
 *
 *	Hardware Management Console (HMC): Version 4, Release 2.1
 *	- OP720 1000-6CA three LPAR partitions
 *
 *	Note:  Only SSH access to the HMC devices are supported.
 *
 * This command would make a nice status command:
 *
 *	lshmc -r -F ssh
 *
 * The following V3 command will get the list of systems we control and their 
 * mode:
 *
 *	lssyscfg -r sys -F name:mode --all
 *
 *		0 indicates full system partition
 *	      255 indicates the system is partitioned
 *
 * The following V4 command will get the list of systems we control:
 *
 *	lssyscfg -r sys -F name
 *
 * The following V3 command will get the list of partitions for a given managed
 * system running partitioned:
 *
 *	lssyscfg -m managed-system -r lpar -F name --all
 *
 *	Note that we should probably only consider partitions whose boot mode
 *	is normal (1).  (that's my guess, anyway...)
 *
 * The following V4 command will get the list of partitions for a given managed
 * system running partitioned:
 *
 *	lssyscfg -m managed-system -r lpar -F name
 *
 * The following V3 commands provide the reset/on/off actions:
 *
 *	FULL SYSTEM:
 *	  on:	chsysstate -m %1 -r sys -o on -n %1 -c full
 *	  off:	chsysstate -m %1 -r sys -o off -n %1 -c full -b norm
 *	  reset:chsysstate -m %1 -r sys -o reset -n %1 -c full -b norm
 *
 *	Partitioned SYSTEM:
 *	  on:	chsysstate -m %1 -r lpar -o on -n %2
 *	  off:	reset_partition -m %1 -p %2 -t hard
 *	  reset:do off action above, followed by on action...
 *
 *	where %1 is managed-system, %2 is-lpar name
 *
 * The following V4 commands provide the reset/on/off actions:
 *
 *	  on:	chsysstate -m %1 -r lpar -o on -n %2 -f %3
 *	  off:	chsysstate -m %1 -r lpar -o shutdown -n %2 --immed
 *	  reset:chsysstate -m %1 -r lpar -o shutdown -n %2 --immed --restart
 *
 *	where %1 is managed-system, %2 is lpar-name, %3 is profile-name
 *
 * Of course, to do all this, we need to track which partition name goes with
 * which managed system's name, and which systems on the HMC are partitioned
 * and which ones aren't...
 */

#define DEVICE "IBM HMC Device"
#include "stonith_plugin_common.h"

#ifndef	SSH_CMD
#	define SSH_CMD	"ssh"
#endif
#ifndef	HMCROOT
#	define HMCROOT	"hscroot"
#endif

#define PIL_PLUGIN              ibmhmc
#define PIL_PLUGIN_S            "ibmhmc"
#define PIL_PLUGINLICENSE 	LICENSE_LGPL
#define PIL_PLUGINLICENSEURL 	URL_LGPL
#include <pils/plugin.h>

#define MAX_HOST_NAME_LEN	(256*4)
#define MAX_CMD_LEN		1024
#define FULLSYSTEMPARTITION	"FullSystemPartition"
#define MAX_POWERON_RETRY	10
#define MAX_SYS_NUM		64
#define MAX_LPAR_NUM		256
#define MAX_HMC_NAME_LEN	256

#define STATE_UNKNOWN		-1
#define STATE_OFF		0
#define STATE_ON		1
#define STATE_INVALID		2

#define HMCURL	"http://publib-b.boulder.ibm.com/Redbooks.nsf/RedbookAbstracts"\
		"/SG247038.html"

static StonithPlugin *	ibmhmc_new(const char *);
static void		ibmhmc_destroy(StonithPlugin *);
static const char *	ibmhmc_getinfo(StonithPlugin * s, int InfoType);
static const char**	ibmhmc_get_confignames(StonithPlugin* p);
static int		ibmhmc_status(StonithPlugin * );
static int		ibmhmc_reset_req(StonithPlugin * s,int request,const char* host);
static char **		ibmhmc_hostlist(StonithPlugin  *);
static int		ibmhmc_set_config(StonithPlugin *, StonithNVpair*);

static char* do_shell_cmd(const char* cmd, int* status);
static int check_hmc_status(const char* hmc);
/* static char* do_shell_cmd_fake(const char* cmd, int* status); */

static struct stonith_ops ibmhmcOps = {
	ibmhmc_new,		/* Create new STONITH object	*/
	ibmhmc_destroy,		/* Destroy STONITH object	*/
	ibmhmc_getinfo,		/* Return STONITH info string	*/
	ibmhmc_get_confignames,	/* Return configuration parameters */
	ibmhmc_set_config,      /* Set configuration            */
	ibmhmc_status,		/* Return STONITH device status	*/
	ibmhmc_reset_req,	/* Request a reset */
	ibmhmc_hostlist,	/* Return list of supported hosts */
};

PIL_PLUGIN_BOILERPLATE2("1.0", Debug)

static const PILPluginImports*  PluginImports;
static PILPlugin*               OurPlugin;
static PILInterface*		OurInterface;
static StonithImports*		OurImports;
static void*			interfprivate;


PIL_rc
PIL_PLUGIN_INIT(PILPlugin*us, const PILPluginImports* imports);

PIL_rc
PIL_PLUGIN_INIT(PILPlugin*us, const PILPluginImports* imports)
{
	/* Force the compiler to do a little type checking */
	(void)(PILPluginInitFun)PIL_PLUGIN_INIT;

	PluginImports = imports;
	OurPlugin = us;

	/* Register ourself as a plugin */
	imports->register_plugin(us, &OurPIExports);  

	/*  Register our interface implementation */
 	return imports->register_interface(us, PIL_PLUGINTYPE_S
	,	PIL_PLUGIN_S
	,	&ibmhmcOps
	,	NULL		/*close */
	,	&OurInterface
	,	(void*)&OurImports
	,	&interfprivate); 
}

struct pluginDevice {	
	StonithPlugin		sp;
	const char *		pluginid;
	char *			hmc;
	GList*		 	hostlist;
	int			hmcver;
};

static const char * pluginid = "HMCDevice-Stonith";
static const char * NOTpluginID = "HMC device has been destroyed";

static int
ibmhmc_status(StonithPlugin  *s)
{
	struct pluginDevice* dev = NULL;
	
	if(Debug){
		LOG(PIL_DEBUG, "%s: called\n", __FUNCTION__);
	}

	ERRIFWRONGDEV(s,S_OOPS);

	dev = (struct pluginDevice*) s;
	
	return check_hmc_status(dev->hmc);
}


/*
 *	Return the list of hosts configured for this HMC device
 */

static char **
ibmhmc_hostlist(StonithPlugin  *s)
{
	int j;
	struct pluginDevice* dev;
	int numnames = 0;
	char** ret = NULL;
	GList* node = NULL;

	if(Debug){
		LOG(PIL_DEBUG, "%s: called\n", __FUNCTION__);
	}

	ERRIFWRONGDEV(s,NULL);

	dev = (struct pluginDevice*) s;
	numnames = g_list_length(dev->hostlist);
	if (numnames < 0) {
		LOG(PIL_CRIT, "unconfigured stonith object in %s"
		,	__FUNCTION__);
		return(NULL);
	}

	ret = (char **)MALLOC((numnames+1)*sizeof(char*));
	if (ret == NULL) {
		LOG(PIL_CRIT, "out of memory");
		return ret;
	}

	memset(ret, 0, (numnames+1)*sizeof(char*));
	for (node = g_list_first(dev->hostlist), j = 0
	;	NULL != node
	;	j++, node = g_list_next(node))	{
		char* host = (char*)node->data;
		ret[j] = STRDUP(host);
	}
	return ret;
}

/*
 *	Parse the config information, and stash it away...
 */
static int
ibmhmc_parse_config_info(struct pluginDevice* dev, const char* info)
{
	int i, j, status;
	char* output = NULL;
	char get_syslist[MAX_CMD_LEN];
	char host[MAX_HOST_NAME_LEN];
	gchar** syslist = NULL;
	gchar** name_mode = NULL;
	char get_lpar[MAX_CMD_LEN];
	gchar** lparlist = NULL;
	char get_hmcver[MAX_CMD_LEN];
	char firstchar;
	int firstnum;

	if(Debug){
		LOG(PIL_DEBUG, "%s: called, info=%s\n", __FUNCTION__, info);
	}

	if (info == NULL || *info == 0){
		return S_BADCONFIG;
	}
	
	/* check whether the HMC has ssh command enabled */
	if (check_hmc_status(info) != S_OK) {
		return S_BADCONFIG;
	}		

	/* get the HMC's version info */
	snprintf(get_hmcver, MAX_CMD_LEN
	,	SSH_CMD " -l " HMCROOT " %s lshmc -v | grep RM", info);
	if (Debug) {
		LOG(PIL_DEBUG, "%s: get_hmcver=%s", __FUNCTION__, get_hmcver);
	}

	output = do_shell_cmd(get_hmcver, &status);
	if (Debug) {
		LOG(PIL_DEBUG, "%s: output=%s\n", __FUNCTION__, output);
	}
	if (output == NULL) {
		return S_BADCONFIG;
	}		

	/* parse the HMC's version info (i.e. "*RM V4R2.1" or "*RM R3V2.6") */
	if ((sscanf(output, "*RM %c%1d", &firstchar, &firstnum) == 2)
	&& ((firstchar == 'V') || (firstchar == 'R'))) {
		dev->hmcver = firstnum;
		if(Debug){
			LOG(PIL_DEBUG, "%s: HMC %s version is %d"
			,	__FUNCTION__, info, dev->hmcver);
		}
	}else{
		LOG(PIL_CRIT, "%s: unable to determine HMC %s version"
		,	__FUNCTION__, info);
		FREE(output);
		return S_BADCONFIG;
	}
	FREE(output);

	/* get the managed system's names of the hmc */
	if (dev->hmcver < 4) {
		snprintf(get_syslist, MAX_CMD_LEN, SSH_CMD " -l " HMCROOT
			" %s lssyscfg -r sys -F name:mode --all", info);
	}else{
		snprintf(get_syslist, MAX_CMD_LEN, SSH_CMD 
			" -l " HMCROOT " %s lssyscfg -r sys -F name", info);
	}
	if(Debug){
		LOG(PIL_DEBUG, "%s: get_syslist=%s", __FUNCTION__, get_syslist);
	}

	output = do_shell_cmd(get_syslist, &status);
	if (output == NULL) {
		return S_BADCONFIG;
	}		
	syslist = g_strsplit(output, "\n", MAX_SYS_NUM);
	FREE(output);

	/* for each managed system */
	for (i = 0; i < MAX_SYS_NUM; i++) {
		if (syslist[i] == NULL || syslist[i][0] == 0) {
			break;
		}

		if (dev->hmcver < 4) {
			name_mode = g_strsplit(syslist[i], ":", 2);
			if(Debug){
			LOG(PIL_DEBUG, "%s: name_mode0=%s, name_mode1=%s\n"
			,	__FUNCTION__, name_mode[0], name_mode[1]);
			}

			/* if it is in fullsystempartition */
			if (NULL != name_mode[1]
			&& 0 == strncmp(name_mode[1], "0", 1)) {
				/* add the FullSystemPartition */
				snprintf(host, MAX_HOST_NAME_LEN
				,	"%s/FullSystemPartition", name_mode[0]);
				dev->hostlist = g_list_append(dev->hostlist 
				,	STRDUP(host));
			}else if (NULL != name_mode[1]
			&& 0 == strncmp(name_mode[1], "255", 3)){
				/* get its lpars */
				snprintf(get_lpar, MAX_CMD_LEN
				,	SSH_CMD " -l " HMCROOT
				" %s lssyscfg -m %s -r lpar -F name --all"
				,	info, name_mode[0]);
				if(Debug){
					LOG(PIL_DEBUG, "%s: get_lpar=%s\n"
					,	__FUNCTION__, get_lpar);
				}

				output = do_shell_cmd(get_lpar, &status);
				if (output == NULL) {
					return S_BADCONFIG;
				}		
				lparlist = g_strsplit(output, "\n"
				,	MAX_LPAR_NUM);
				FREE(output);
	
				/* for each lpar */
				for (j = 0; j < MAX_LPAR_NUM; j++) {
					if (NULL == lparlist[j]) {
						break;
					}
					/* skip the full system partition */
					if (0 == strncmp(lparlist[j]
					,	FULLSYSTEMPARTITION
					,	strlen(FULLSYSTEMPARTITION))) {
						continue;
					}
					/* add the lpar */
					snprintf(host, MAX_HOST_NAME_LEN
					,	"%s/%s", name_mode[0]
					,	lparlist[j]);
					dev->hostlist = 
						g_list_append(dev->hostlist
						,	STRDUP(host));
				}
				g_strfreev(lparlist);
			}
		}else{
			/* get its lpars */
			snprintf(get_lpar, MAX_CMD_LEN
			,	SSH_CMD " -l " HMCROOT
				 " %s lssyscfg -m %s -r lpar -F name"
			,	info, syslist[i]);
			if(Debug){
				LOG(PIL_DEBUG, "%s: get_lpar=%s\n"
				,	__FUNCTION__, get_lpar);
			}

			output = do_shell_cmd(get_lpar, &status);
			if (output == NULL) {
				return S_BADCONFIG;
			}		
			lparlist = g_strsplit(output, "\n", MAX_LPAR_NUM);
			FREE(output);

			/* for each lpar */
			for (j = 0; j < MAX_LPAR_NUM; j++) {
				if (NULL == lparlist[j]) {
					break;
				}
				/* add the lpar */
				snprintf(host, MAX_HOST_NAME_LEN
				,	"%s/%s", syslist[i],lparlist[j]);
				dev->hostlist = g_list_append(dev->hostlist
						,	STRDUP(host));
			}
			g_strfreev(lparlist);
		}
		g_strfreev(name_mode);
	}
	g_strfreev(syslist);
	dev->hmc = STRDUP(info);
	
	return S_OK;
}


static const char**     
ibmhmc_get_confignames(StonithPlugin* p)
{
	static const char * names[] = {ST_IPADDR, NULL};
	if (Debug) {
		LOG(PIL_DEBUG, "%s: called.", __FUNCTION__);
	}
	return names;
}



/*
 *	Reset the given host, and obey the request type.
 *	We should reset without power cycle for the non-partitioned case
 */
static int
ibmhmc_reset_req(StonithPlugin * s, int request, const char * host)
{
	GList*			node = NULL;
	struct pluginDevice*	dev = NULL;
	char			off_cmd[MAX_CMD_LEN];
	char			on_cmd[MAX_CMD_LEN];
	char			reset_cmd[MAX_CMD_LEN];
	gchar**			names = NULL;
	int			i;
	int			is_lpar = FALSE;
	int			status;
	char*			pch;
	char*			output = NULL;
	char			state_cmd[MAX_CMD_LEN];
	int			state = STATE_UNKNOWN;
	
	status = 0;
	if(Debug){
		LOG(PIL_DEBUG, "%s: called, host=%s\n", __FUNCTION__, host);
	}
	
	ERRIFWRONGDEV(s,S_OOPS);
	
	if (NULL == host) {
		LOG(PIL_CRIT, "invalid argument to %s", __FUNCTION__);
		return(S_OOPS);
	}

	dev = (struct pluginDevice*) s;

	for (node = g_list_first(dev->hostlist)
	;	NULL != node
	;	node = g_list_next(node)) {
		if(Debug){
			LOG(PIL_DEBUG, "%s: node->data=%s\n"
			,	__FUNCTION__, (char*)node->data);
		}
		
		if (0 == strcasecmp((char*)node->data, host)) {
			break;
		}
	}

	if (!node) {
		LOG(PIL_CRIT
		,	"Host %s is not configured in this STONITH module. "
			"Please check your configuration information.", host);
		return (S_OOPS);
	}

	names = g_strsplit((char*)node->data, "/", 2);
	/* names[0] will be the name of managed system */
	/* names[1] will be the name of the lpar partition */
	if(Debug){
		LOG(PIL_DEBUG, "%s: names[0]=%s, names[1]=%s\n"
		,	__FUNCTION__, names[0], names[1]);
	}

	if (dev->hmcver < 4) {
		if (0 == strcasecmp(names[1], FULLSYSTEMPARTITION)) {
			is_lpar = FALSE;
		
			snprintf(off_cmd, MAX_CMD_LEN
			,	SSH_CMD " -l " HMCROOT " %s chsysstate"
			" -r sys -m %s -o off -n %s -c full"
			,	dev->hmc, dev->hmc, names[0]);

			snprintf(on_cmd, MAX_CMD_LEN
			,	SSH_CMD " -l " HMCROOT " %s chsysstate"
			" -r sys -m %s -o on -n %s -c full -b norm"
			,	dev->hmc, names[0], names[0]);

			snprintf(reset_cmd, MAX_CMD_LEN
			,	SSH_CMD " -l " HMCROOT " %s chsysstate"
			" -r sys -m %s -o reset -n %s -c full -b norm"
			,	dev->hmc, names[0], names[0]);
		
		}else{
			is_lpar = TRUE;
		
			snprintf(off_cmd, MAX_CMD_LEN
			,	SSH_CMD " -l " HMCROOT " %s reset_partition"
			" -m %s -p %s -t hard"
			,	dev->hmc, names[0], names[1]);

			snprintf(on_cmd, MAX_CMD_LEN
			,	SSH_CMD " -l " HMCROOT " %s chsysstate"
			" -r lpar -m %s -o on -n %s"
			,	dev->hmc, names[0], names[1]);

			snprintf(state_cmd, MAX_CMD_LEN
			,	SSH_CMD " -l " HMCROOT " %s lssyscfg"
			" -r lpar -m %s -F state -n %s"
			,	dev->hmc, names[0], names[1]);
		}
	}else{
		is_lpar = TRUE;

		snprintf(off_cmd, MAX_CMD_LEN
		,	SSH_CMD " -l " HMCROOT " %s chsysstate"
		" -m %s -r lpar -o shutdown -n \"%s\" --immed"
		,	dev->hmc, names[0], names[1]);

		snprintf(on_cmd, MAX_CMD_LEN
		,	SSH_CMD " -l " HMCROOT " %s lssyscfg"
		" -m %s -r lpar -F \"default_profile\""
		" --filter \"lpar_names=%s\""
		,	dev->hmc, names[0], names[1]);

		output = do_shell_cmd(on_cmd, &status);
		if (output == NULL) {
			LOG(PIL_CRIT, "command %s failed", on_cmd);
			return (S_OOPS);
		}
		if ((pch = strchr(output, '\n')) != NULL) {
			*pch = 0;
		}
		snprintf(on_cmd, MAX_CMD_LEN
		,	SSH_CMD " -l " HMCROOT " %s chsysstate"
		" -m %s -r lpar -o on -n %s -f %s"
		,	dev->hmc, names[0], names[1], output);
		FREE(output);
		output = NULL;

		snprintf(reset_cmd, MAX_CMD_LEN
		,	SSH_CMD " -l " HMCROOT " %s chsysstate"
		" -m %s -r lpar -o shutdown -n %s --immed --restart"
		,	dev->hmc, names[0], names[1]);

		snprintf(state_cmd, MAX_CMD_LEN
		,	SSH_CMD " -l " HMCROOT " %s lssyscfg"
		" -m %s -r lpar -F state --filter \"lpar_names=%s\""
		,	dev->hmc, names[0], names[1]);
	}
	g_strfreev(names);
	
	if(Debug){
		LOG(PIL_DEBUG, "%s: off_cmd=%s, on_cmd=%s,"
			"reset_cmd=%s, state_cmd=%s\n" 
		,	__FUNCTION__, off_cmd, on_cmd, reset_cmd, state_cmd);
	}

	output = do_shell_cmd(state_cmd, &status);
	if (output == NULL) {
		LOG(PIL_CRIT, "command %s failed", on_cmd);
		return S_OOPS;
	}
	if ((pch = strchr(output, '\n')) != NULL) {
		*pch = 0;
	}
	if (strcmp(output, "Running") == 0
	|| strcmp(output, "Starting") == 0
	|| strcmp(output, "Open Firmware") == 0) {
		state = STATE_ON;
	}else if (strcmp(output, "Shutting Down") == 0
	|| strcmp(output, "Not Activated") == 0
	|| strcmp(output, "Ready") == 0) {
		state = STATE_OFF;
	}else if (strcmp(output, "Not Available") == 0
	|| strcmp(output, "Error") == 0) {
		state = STATE_INVALID;
	}
	FREE(output);
	output = NULL;

	if (state == STATE_INVALID) {
		LOG(PIL_CRIT, "host %s in invalid state", host);
		return S_OOPS;
	}

	switch (request) {
	case ST_POWERON:
		if (state == STATE_ON) {
			LOG(PIL_INFO, "host %s already on", host);
			return S_OK;
		}

		output = do_shell_cmd(on_cmd, &status);
		if (0 != status) {
			LOG(PIL_CRIT, "command %s failed", on_cmd);
			return S_OOPS;
		}
		break;
	case ST_POWEROFF:
		if (state == STATE_OFF) {
			LOG(PIL_INFO, "host %s already off", host);
			return S_OK;
		}

		output = do_shell_cmd(off_cmd, &status);
		if (0 != status) {
			LOG(PIL_CRIT, "command %s failed", off_cmd);
			return S_OOPS;
		}
		break;
	case ST_GENERIC_RESET:
		if (dev->hmcver < 4) {
			if (is_lpar) {
				if (state == STATE_ON) {
					output = do_shell_cmd(off_cmd, &status);
					if (0 != status) {
						LOG(PIL_CRIT, "command %s "
							"failed", off_cmd);
						return S_OOPS;
					}
				}
				for (i = 0; i < MAX_POWERON_RETRY; i++) {
					char *output2;
					output2 = do_shell_cmd(on_cmd, &status);
					if (output2 != NULL) {
						FREE(output2);
					}
					if (0 != status) {
						sleep(1);
					}else{
						break;
					}
				}
				if (MAX_POWERON_RETRY == i) {
					LOG(PIL_CRIT, "command %s failed"
					,	on_cmd);
					return S_OOPS;
				}
			}else{
				output = do_shell_cmd(reset_cmd, &status);
				if (0 != status) {
					LOG(PIL_CRIT, "command %s failed"						,	reset_cmd);
					return S_OOPS;
				}
				break;
			}
		}else{
			if (state == STATE_ON) {
				output = do_shell_cmd(reset_cmd, &status);
			}else{
				output = do_shell_cmd(on_cmd, &status);
			}
			if (0 != status) {
				LOG(PIL_CRIT, "command %s failed", reset_cmd);
				return S_OOPS;
			}
		}
		break;
	default:
		return S_INVAL;
	}

	if (output != NULL) {
		FREE(output);
	}
		
	LOG(PIL_INFO, "Host %s %s.", host, __FUNCTION__);

	return S_OK;
}

/*
 *	Parse the information in the given configuration file,
 *	and stash it away...
 */
static int
ibmhmc_set_config(StonithPlugin * s, StonithNVpair* list)
{
	struct pluginDevice* dev = NULL;
	const char * ipaddr;	
	
	ERRIFWRONGDEV(s,S_OOPS);

	if(Debug){
		LOG(PIL_DEBUG, "%s: called\n", __FUNCTION__);
	}
	
	dev = (struct pluginDevice*) s;

	if((ipaddr = OurImports->GetValue(list, ST_IPADDR)) == NULL){
		return S_OOPS;
	}

	if(Debug){
		LOG(PIL_DEBUG, "%s: ipaddr=%s\n", __FUNCTION__, ipaddr);	
	}
	
	if (S_OK != ibmhmc_parse_config_info(dev, ipaddr)){
		return S_BADCONFIG;
	}
	
	return S_OK;
}

static const char*
ibmhmc_getinfo(StonithPlugin* s, int reqtype)
{
	struct pluginDevice* dev;
	const char* ret;

	ERRIFWRONGDEV(s,NULL);

	dev = (struct pluginDevice *)s;

	switch (reqtype) {
		case ST_DEVICEID:
			ret = DEVICE;
			break;

		case ST_DEVICENAME:
			ret = dev->hmc;
			break;

		case ST_DEVICEDESCR:
			ret = "IBM Hardware Management Console (HMC)\n"
			"Use for IBM i5, p5, pSeries and OpenPower systems "
			"managed by HMC\n";
			break;

		case ST_DEVICEURL:
			ret = HMCURL;
			break;

		default:
			ret = NULL;
			break;
	}
	return ret;
}

/*
 *	HMC Stonith destructor...
 */
static void
ibmhmc_destroy(StonithPlugin *s)
{
	struct pluginDevice* dev;

	if(Debug){
		LOG(PIL_DEBUG, "%s : called\n", __FUNCTION__);
	}

	VOIDERRIFWRONGDEV(s);

	dev = (struct pluginDevice *)s;

	dev->pluginid = NOTpluginID;
	if (dev->hmc) {
		FREE(dev->hmc);
		dev->hmc = NULL;
	}
	if (dev->hostlist) {
		GList* node;
		while (NULL != (node=g_list_first(dev->hostlist))) {
			dev->hostlist = g_list_remove_link(dev->hostlist, node);
			FREE(node->data);
			g_list_free(node);
		}
		dev->hostlist = NULL;
	}
	
	FREE(dev);
}

static StonithPlugin *
ibmhmc_new(const char *subplugin)
{
	struct pluginDevice* dev = MALLOCT(struct pluginDevice);
	
	if(Debug){
		LOG(PIL_DEBUG, "%s: called\n", __FUNCTION__);
	}
	
	if (dev == NULL) {
		LOG(PIL_CRIT, "%s: out of memory", __FUNCTION__);
		return(NULL);
	}

	memset(dev, 0, sizeof(*dev));

	dev->pluginid = pluginid;
	dev->hmc = NULL;
	dev->hostlist = NULL;
	dev->hmcver = -1;
	dev->sp.s_ops = &ibmhmcOps;

	if(Debug){
		LOG(PIL_DEBUG, "%s: returning successfully\n", __FUNCTION__);
	}

	return((void *)dev);
}


static char*
do_shell_cmd(const char* cmd, int* status)
{
	const int BUFF_LEN=4096;
	int read_len = 0;
	char buff[BUFF_LEN];
	char* data = NULL;
	GString* g_str_tmp = NULL;

	FILE* file = popen(cmd, "r");
	if (NULL == file) {
		return NULL;
	}

	g_str_tmp = g_string_new("");
	while(!feof(file)) {
		memset(buff, 0, BUFF_LEN);
		read_len = fread(buff, 1, BUFF_LEN, file);
		if (0 < read_len) {
			g_string_append(g_str_tmp, buff);
		}else{
			sleep(1);
		}
	}
	data = (char*)MALLOC(g_str_tmp->len+1);
	data[0] = data[g_str_tmp->len] = 0;
	strncpy(data, g_str_tmp->str, g_str_tmp->len);
	g_string_free(g_str_tmp, TRUE);

	*status = pclose(file);
fprintf(stderr, "$$$$ %s\n", data);
	return data;
}

static int
check_hmc_status(const char* hmc)
{
	int status;
	char check_status[MAX_CMD_LEN];
	char* output = NULL;

	if(Debug){
		LOG(PIL_DEBUG, "%s: called, hmc=%s\n", __FUNCTION__, hmc);
	}

	snprintf(check_status, MAX_CMD_LEN
	,	SSH_CMD " -l " HMCROOT " %s lshmc -r -F ssh", hmc);
	if(Debug){
		LOG(PIL_DEBUG, "%s: check_status %s\n", __FUNCTION__
		,	check_status);
	}

	output = do_shell_cmd(check_status, &status);
	
	if (Debug) {
		LOG(PIL_DEBUG, "%s: status=%d, output=%s\n", __FUNCTION__
		,	status, output ? output : "(nil)");
	}

	if (NULL == output || strncmp(output, "enable", 6) != 0) {
		return S_BADCONFIG;
	}
	FREE(output);
	return S_OK;
}

/*
static char*
do_shell_cmd_fake(const char* cmd, int* status)
{
	printf("%s()\n", __FUNCTION__);
	printf("cmd:%s\n", cmd);
	*status=0;
	return NULL;
}
*/
