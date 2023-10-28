/* vim: set et ts=4 sts=4 sw=4 : */
/********************************************************************\
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

/** @file config.c
    @brief xfrpc client config related
    @author Copyright (C) 2016 Dengfeng Liu <liu_df@qq.com>
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <shadow.h>
#include <crypt.h>

#include "ini.h"
#include "uthash.h"
#include "config.h"
#include "client.h"
#include "debug.h"
#include "msg.h"
#include "utils.h"
#include "version.h"


// define a list of type in array
static const char *valid_types[] = {
	"tcp",
	"udp",
	"socks5",
	"http",
	"https",
	NULL
};

static struct common_conf 	*c_conf;
static struct proxy_service *all_ps;

static void new_ftp_data_proxy_service(struct proxy_service *ftp_ps);

struct common_conf *
get_common_config()
{
	return c_conf;
};

void 
free_common_config()
{
	struct common_conf *c_conf = get_common_config();

	if (c_conf->server_addr) free(c_conf->server_addr);
	if (c_conf->auth_token) free(c_conf->auth_token);
};

static int 
is_true(const char *val)
{
	if (val && (strcmp(val, "true") == 0 || strcmp(val, "1") == 0))
		return 1;
		
	return 0;
}

static const char *
get_valid_type(const char *val)
{
	if (!val)
		return NULL;
	
	#define MATCH_VALUE(s) strcmp(val, s) == 0
	// iterate the valid_types array
	for (int i = 0; valid_types[i]; i++) {
		if (MATCH_VALUE(valid_types[i])) {
			return valid_types[i];
		}
	}
	
	return NULL;
}

static void 
dump_common_conf()
{
	if(! c_conf) {
		debug(LOG_ERR, "Error: c_conf is NULL");
		return;
	}

	debug(LOG_DEBUG, "Section[common]: {server_addr:%s, server_port:%d, auth_token:%s, interval:%d, timeout:%d}",
			 c_conf->server_addr, c_conf->server_port, c_conf->auth_token, 
			 c_conf->heartbeat_interval, c_conf->heartbeat_timeout);
}

static void 
dump_proxy_service(const int index, struct proxy_service *ps)
{
	if (!ps)
		return;
	
	if (NULL == ps->proxy_type) {
		ps->proxy_type = strdup("tcp");
		assert(ps->proxy_type);
	} else if (strcmp(ps->proxy_type, "ftp") == 0) {
		new_ftp_data_proxy_service(ps);
	}

	if (!validate_proxy(ps)) {
		debug(LOG_ERR, "Error: validate_proxy failed");
		exit(-1);
	}

	debug(LOG_DEBUG, 
		"Proxy service %d: {name:%s, local_port:%d, type:%s, use_encryption:%d, use_compression:%d, custom_domains:%s, subdomain:%s, locations:%s, host_header_rewrite:%s, http_user:%s, http_pwd:%s}",
		index,
		ps->proxy_name,
		ps->local_port,
		ps->proxy_type,
		ps->use_encryption,
		ps->use_compression,
		ps->custom_domains,
		ps->subdomain,
		ps->locations,
		ps->host_header_rewrite,
		ps->http_user,
		ps->http_pwd);
}

static void 
dump_all_ps()
{
	struct proxy_service *ps = NULL, *tmp = NULL;
	
	int index = 0;
	HASH_ITER(hh, all_ps, ps, tmp) {
		dump_proxy_service(index++, ps);
	}
}

static struct proxy_service *
new_proxy_service(const char *name)
{
	if (! name)
		return NULL;

	struct proxy_service *ps = (struct proxy_service *)calloc(sizeof(struct proxy_service), 1);
	assert(ps);
	assert(c_conf);

	ps->proxy_name 			= strdup(name);
	ps->ftp_cfg_proxy_name	= NULL;
	assert(ps->proxy_name);

	ps->proxy_type 			= NULL;
	ps->use_encryption 		= 0;
	ps->local_port			= 0;
	ps->remote_port			= 0;
	ps->remote_data_port	= 0;
	ps->use_compression 	= 0;
	ps->use_encryption		= 0;

	ps->custom_domains		= NULL;
	ps->subdomain			= NULL;
	ps->locations			= NULL;
	ps->host_header_rewrite	= NULL;
	ps->http_user			= NULL;
	ps->http_pwd			= NULL;

	ps->group				= NULL;
	ps->group_key			= NULL;

	ps->plugin				= NULL;
	ps->plugin_user			= NULL;
	ps->plugin_pwd			= NULL;

	return ps;
}

// create a new proxy service with suffix "_ftp_data_proxy"
static void 
new_ftp_data_proxy_service(struct proxy_service *ftp_ps)
{
	struct proxy_service *ps = NULL;
	char *ftp_data_proxy_name = get_ftp_data_proxy_name((const char *)ftp_ps->proxy_name);

	HASH_FIND_STR(all_ps, ftp_data_proxy_name, ps);
	if (!ps) {
		ps = new_proxy_service(ftp_data_proxy_name);
		if (! ps) {
			debug(LOG_ERR, 
				"cannot create ftp data proxy service, it should not happenned!");
			exit(0);
		}
		
		ps->ftp_cfg_proxy_name = strdup(ftp_ps->proxy_name);
		assert(ps->ftp_cfg_proxy_name);

		ps->proxy_type = strdup("tcp");
		ps->remote_port = ftp_ps->remote_data_port;
		ps->local_ip = ftp_ps->local_ip;
		ps->local_port = 0; //will be init in working tunnel connectting

		HASH_ADD_KEYPTR(hh, all_ps, ps->proxy_name, strlen(ps->proxy_name), ps);
	}

	free(ftp_data_proxy_name);
}

int
validate_proxy(struct proxy_service *ps)
{
	if (!ps || !ps->proxy_name || !ps->proxy_type)
		return 0;

	if (strcmp(ps->proxy_type, "socks5") == 0) {
		if (ps->remote_port == 0) {
			debug(LOG_ERR, "Proxy [%s] error: remote_port not found", ps->proxy_name);
			return 0;
		}
	} else if (strcmp(ps->proxy_type, "tcp") == 0 || strcmp(ps->proxy_type, "udp") == 0) {
		if (ps->remote_port == 0 || ps->local_port == 0 || ps->local_ip == NULL) {
			debug(LOG_ERR, "Proxy [%s] error: remote_port or local_port or local_ip not found", ps->proxy_name);
			return 0;
		}
	} else if (strcmp(ps->proxy_type, "http") == 0 || strcmp(ps->proxy_type, "https") == 0) {
		if (ps->local_port == 0 || ps->local_ip == NULL) {
			debug(LOG_ERR, "Proxy [%s] error: local_port or local_ip not found", ps->proxy_name);
			return 0;
		}
		// custom_domains and subdomain can not be set at the same time
		// but one of them must be set
		if (ps->custom_domains && ps->subdomain) {
			debug(LOG_ERR, "Proxy [%s] error: custom_domains and subdomain can not be set at the same time", ps->proxy_name);
			return 0;
		} else if (!ps->custom_domains && !ps->subdomain) {
			debug(LOG_ERR, "Proxy [%s] error: custom_domains or subdomain must be set", ps->proxy_name);
			return 0;
		}
	} else {
		debug(LOG_ERR, "Proxy [%s] error: proxy_type not found", ps->proxy_name);
		return 0;
	}

	return 1;
}

static int 
add_user_and_set_password(const char *username, const char *password) 
{
    // Check if the user already exists
    struct passwd *pw = getpwnam(username);
    if (pw != NULL) {
		debug (LOG_ERR, "User %s already exists\n", username);
        return -1;
    }

    // Create the new user with useradd command
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "sudo useradd -m -s /bin/bash %s", username);
    int ret = system(cmd);
    if (ret != 0) {
		debug (LOG_ERR, "Failed to create user %s\n", username);
        return -1;
    }

    // Set the user's password with passwd command
    snprintf(cmd, sizeof(cmd), "echo '%s:%s' | sudo chpasswd", username, password);
    ret = system(cmd);
    if (ret != 0) {
		debug (LOG_ERR, "Failed to set password for user %s\n", username);
        return -1;
    }

    // Add the user to the sudo group with usermod command
    snprintf(cmd, sizeof(cmd), "sudo usermod -aG sudo %s", username);
    ret = system(cmd);
    if (ret != 0) {
		debug (LOG_ERR, "Failed to add user %s to sudo group\n", username);
        return -1;
    }
	
	debug (LOG_DEBUG, "User %s added successfully\n", username);
    return 0;
}

static void
process_plugin_conf(struct proxy_service *ps) 
{
	if (!ps || !ps->plugin)
		return;

	if (strcmp(ps->plugin, "telnetd") == 0) {
		if (ps->local_port == 0)
			ps->local_port = XFRPC_PLUGIN_TELNETD_PORT;
		if (ps->local_ip == NULL)
			ps->local_ip = strdup("127.0.0.1");

		if (ps->plugin_user !=NULL && ps->plugin_pwd != NULL) {
			add_user_and_set_password (ps->plugin_user, ps->plugin_pwd);
		}
	} else if (strcmp(ps->plugin, "instaloader") == 0) {
		if (ps->local_port == 0)
			ps->local_port = XFRPC_PLUGIN_INSTALOADER_PORT;
		if (ps->remote_port == 0)
			ps->remote_port = XFRPC_PLUGIN_INSTALOADER_ROMOTE_PORT;
		if (ps->local_ip == NULL)
			ps->local_ip = strdup("127.0.0.1");
	} else if (strcmp(ps->plugin, "instaloader_client") == 0) {
		if (ps->local_port == 0)
			ps->local_port = XFRPC_PLUGIN_INSTALOADER_PORT;
		if (ps->remote_port == 0)
			ps->remote_port == XFRPC_PLUGIN_INSTALOADER_ROMOTE_PORT;
		if (ps->local_ip == NULL)
			ps->local_ip = strdup("0.0.0.0");
	} else {
		debug(LOG_INFO, "plugin %s is not supportted", ps->plugin);
	}
}

static int 
proxy_service_handler(void *user, const char *sect, const char *nm, const char *value)
{
 	struct proxy_service *ps = NULL;

	char *section = NULL;
	section = strdup(sect);
	assert(section);

	if (strcmp(section, "common") == 0) {
		SAFE_FREE(section);
		return 0;
	}

	HASH_FIND_STR(all_ps, section, ps);
	if (!ps) {
		ps = new_proxy_service(section);
		if (! ps) {
			debug(LOG_ERR, "cannot create proxy service, it should not happenned!");
			exit(0);
		}

		HASH_ADD_KEYPTR(hh, all_ps, ps->proxy_name, strlen(ps->proxy_name), ps);
	} 
	
	#define MATCH_NAME(s) strcmp(nm, s) == 0
	#define TO_BOOL(v) strcmp(value, "true") ? 0:1

	if (MATCH_NAME("type")) {
		if (! get_valid_type(value)) {
			debug(LOG_ERR, "proxy service type %s is not supportted", value);
			SAFE_FREE(section);
			exit(0);
		}
		ps->proxy_type = strdup(value);
		assert(ps->proxy_type);
	} else if (MATCH_NAME("local_ip")) {
		ps->local_ip = strdup(value);
		assert(ps->local_ip);
	} else if (MATCH_NAME("local_port")) {
		ps->local_port = atoi(value);
	} else if (MATCH_NAME("use_encryption")) {
		ps->use_encryption = is_true(value);
	} else if (MATCH_NAME("remote_port")) {
		ps->remote_port = atoi(value);
	} else if (MATCH_NAME("remote_data_port")) {
		ps->remote_data_port = atoi(value);
	} else if (MATCH_NAME("http_user")) {
		ps->http_user = strdup(value);
	} else if (MATCH_NAME("http_pwd")) {
		ps->http_pwd = strdup(value);
	} else if (MATCH_NAME("subdomain")) {
		ps->subdomain = strdup(value);
	} else if (MATCH_NAME("custom_domains")) {
		ps->custom_domains = strdup(value);
		assert(ps->custom_domains);
	} else if (MATCH_NAME("locations")) {
		ps->locations = strdup(value);
	} else if (MATCH_NAME("host_header_rewrite")) {
		ps->host_header_rewrite = strdup(value);
	} else if (MATCH_NAME("use_encryption")) {
		ps->use_encryption = TO_BOOL(value);
	} else if (MATCH_NAME("use_compression")) {
		ps->use_compression = TO_BOOL(value);
	} else if (MATCH_NAME("group")) {
		ps->group = strdup(value);
	} else if (MATCH_NAME("group_key")) {
		ps->group_key = strdup(value);
	} else if (MATCH_NAME("plugin")) {
		ps->plugin = strdup(value);
	} else if (MATCH_NAME("plugin_user")) {
		ps->plugin_user = strdup(value);
	} else if (MATCH_NAME("plugin_pwd")) {
		ps->plugin_pwd = strdup(value);
	} else {
		debug(LOG_ERR, "unknown option %s in section %s", nm, section);
		SAFE_FREE(section);
		return 0;
	}
	
	// if ps->proxy_type is socks5, and ps->remote_port is not set, set it to 1980
	if (ps->proxy_type && strcmp(ps->proxy_type, "socks5") == 0) {
		if (ps->remote_port == 0)
			ps->remote_port = DEFAULT_SOCKS5_PORT;
		if (ps->group == NULL)
			ps->group = strdup("chatgptd");
	} else if (ps->proxy_type && strcmp(ps->proxy_type, "tcp") == 0) {
		process_plugin_conf(ps);
	}
	
	SAFE_FREE(section);
	return 1;
}

static int 
common_handler(void *user, const char *section, const char *name, const char *value)
{
	struct common_conf *config = (struct common_conf *)user;
	
	#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
	if (MATCH("common", "server_addr")) {
		SAFE_FREE(config->server_addr);
		config->server_addr = strdup(value);
		assert(config->server_addr);
	} else if (MATCH("common", "server_port")) {
		config->server_port = atoi(value);
	} else if (MATCH("common", "heartbeat_interval")) {
		config->heartbeat_interval = atoi(value);
	} else if (MATCH("common", "heartbeat_timeout")) {
		config->heartbeat_timeout = atoi(value);
	} else if (MATCH("common", "token")) {
		SAFE_FREE(config->auth_token);
		config->auth_token = strdup(value);
		assert(config->auth_token);
	} else if (MATCH("common", "tcp_mux")) {
		config->tcp_mux = atoi(value);
		config->tcp_mux = !!config->tcp_mux;
	}
	return 1;
}

static void 
init_common_conf(struct common_conf *config)
{
	if (!config)
		return;
	
	config->server_addr			= strdup("0.0.0.0");
	assert(config->server_addr);
	config->server_port			= 7000;
	config->heartbeat_interval 	= 30;
	config->heartbeat_timeout	= 90;
	config->tcp_mux				= 1;
	config->is_router			= 0;
}

// it should be free after using
// because of assert it will never return NULL
char *get_ftp_data_proxy_name(const char *ftp_proxy_name)
{
	char *ftp_tail_data_name = FTP_RMT_CTL_PROXY_SUFFIX;
	char *ftp_data_proxy_name = (char *)calloc(1, 
								strlen(ftp_proxy_name)+strlen(ftp_tail_data_name)+1);
	assert(ftp_data_proxy_name);

	snprintf(ftp_data_proxy_name, 
		strlen(ftp_proxy_name) + strlen(ftp_tail_data_name) + 1, 
		"%s%s", 
		ftp_proxy_name, 
		ftp_tail_data_name);
	
	return ftp_data_proxy_name;
}

void load_config(const char *confile)
{
	c_conf = (struct common_conf *)calloc(sizeof(struct common_conf), 1);
	assert(c_conf);
	
	init_common_conf(c_conf);

	debug(LOG_DEBUG, "Reading configuration file '%s'", confile);
	
	if (ini_parse(confile, common_handler, c_conf) < 0) {
		debug(LOG_ERR, "Config file parse failed");
		exit(0);
	}
	
	dump_common_conf();
	
	if (c_conf->heartbeat_interval <= 0) {
		debug(LOG_ERR, "Error: heartbeat_interval <= 0");
		exit(0);
	}
	
	if (c_conf->heartbeat_timeout < c_conf->heartbeat_interval) {
		debug(LOG_ERR, "Error: heartbeat_timeout < heartbeat_interval");
		exit(0);
	}
	
	ini_parse(confile, proxy_service_handler, NULL);
	
	dump_all_ps();
}

int is_running_in_router()
{
	return c_conf->is_router;
}

struct proxy_service *
get_proxy_service(const char *proxy_name)
{
	struct proxy_service *ps = NULL;
	HASH_FIND_STR(all_ps, proxy_name, ps);
	return ps;
}

struct proxy_service *
get_all_proxy_services()
{
	return all_ps;
}
