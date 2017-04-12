/*
 * Copyright(c) 2013 Tim Ruehsen
 * Copyright(c) 2015-2016 Free Software Foundation, Inc.
 *
 * This file is part of libwget.
 *
 * Libwget is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Libwget is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libwget.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 * Memory allocation routines
 *
 * Changelog
 * 18.01.2013  Tim Ruehsen  created
 *
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>

#include <wget.h>
#include "private.h"

static struct _CONFIG {
	char *
		cookie_file;
	wget_cookie_db_t *
		cookie_db;
	char
		cookies_enabled,
		keep_session_cookies;
} _config = {
	.cookies_enabled = 0
};

static int _init;
static wget_thread_mutex_t _mutex = WGET_THREAD_MUTEX_INITIALIZER;

void wget_global_init(int first_key, ...)
{
	va_list args;
	int key, rc;
	const char *psl_file = NULL;
	wget_logger_func_t func; // intermediate var to satisfy MSVC

	wget_thread_mutex_lock(&_mutex);

	if (_init++) {
		wget_thread_mutex_unlock(&_mutex);
		return;
	}

	wget_console_init();

	va_start (args, first_key);
	for (key = first_key; key; key = va_arg(args, int)) {
		switch (key) {
		case WGET_DEBUG_STREAM:
			wget_logger_set_stream(wget_get_logger(WGET_LOGGER_DEBUG), va_arg(args, FILE *));
			break;
		case WGET_DEBUG_FUNC:
			func = va_arg(args, wget_logger_func_t);
			wget_logger_set_func(wget_get_logger(WGET_LOGGER_DEBUG), func);
			break;
		case WGET_DEBUG_FILE:
			wget_logger_set_file(wget_get_logger(WGET_LOGGER_DEBUG), va_arg(args, const char *));
			break;
		case WGET_ERROR_STREAM:
			wget_logger_set_stream(wget_get_logger(WGET_LOGGER_ERROR), va_arg(args, FILE *));
			break;
		case WGET_ERROR_FUNC:
			func = va_arg(args, wget_logger_func_t);
			wget_logger_set_func(wget_get_logger(WGET_LOGGER_ERROR), func);
			break;
		case WGET_ERROR_FILE:
			wget_logger_set_file(wget_get_logger(WGET_LOGGER_ERROR), va_arg(args, const char *));
			break;
		case WGET_INFO_STREAM:
			wget_logger_set_stream(wget_get_logger(WGET_LOGGER_INFO), va_arg(args, FILE *));
			break;
		case WGET_INFO_FUNC:
			func = va_arg(args, wget_logger_func_t);
			wget_logger_set_func(wget_get_logger(WGET_LOGGER_INFO), func);
			break;
		case WGET_INFO_FILE:
			wget_logger_set_file(wget_get_logger(WGET_LOGGER_INFO), va_arg(args, const char *));
			break;
		case WGET_DNS_CACHING:
			wget_tcp_set_dns_caching(NULL, va_arg(args, int));
			break;
		case WGET_TCP_FASTFORWARD:
			wget_tcp_set_tcp_fastopen(NULL, va_arg(args, int));
			break;
		case WGET_COOKIE_SUFFIXES:
			psl_file = va_arg(args, const char *);
			_config.cookies_enabled = 1;
			break;
		case WGET_COOKIES_ENABLED:
			_config.cookies_enabled = !!va_arg(args, int);
			break;
		case WGET_COOKIE_FILE:
			// load cookie-store
			_config.cookies_enabled = 1;
			_config.cookie_file = va_arg(args, char *);
			break;
		case WGET_COOKIE_KEEPSESSIONCOOKIES:
			_config.keep_session_cookies = !!va_arg(args, int);
			break;
		case WGET_BIND_ADDRESS:
			wget_tcp_set_bind_address(NULL, va_arg(args, const char *));
			break;
		case WGET_NET_FAMILY_EXCLUSIVE:
			wget_tcp_set_family(NULL, va_arg(args, int));
			break;
		case WGET_NET_FAMILY_PREFERRED:
			wget_tcp_set_preferred_family(NULL, va_arg(args, int));
			break;
		default:
			wget_thread_mutex_unlock(&_mutex);
			wget_error_printf(_("%s: Unknown option %d"), __func__, key);
			va_end(args);
			return;
		}
	}
	va_end(args);

	if (_config.cookies_enabled && _config.cookie_file) {
		_config.cookie_db = wget_cookie_db_init(NULL);
		wget_cookie_set_keep_session_cookies(_config.cookie_db, _config.keep_session_cookies);
		wget_cookie_db_load(_config.cookie_db, _config.cookie_file);
		wget_cookie_db_load_psl(_config.cookie_db, psl_file);
	}

	rc = wget_net_init();

	wget_thread_mutex_unlock(&_mutex);

	if (rc)
		wget_error_printf_exit(_("%s: Failed to init networking (%d)"), __func__, rc);
}

void wget_global_deinit(void)
{
	int rc = 0;

	wget_thread_mutex_lock(&_mutex);

	if (_init == 1) {
		// free resources here
		if (_config.cookie_db && _config.cookies_enabled && _config.cookie_file) {
			wget_cookie_db_save(_config.cookie_db, _config.cookie_file);
			wget_cookie_db_free(&_config.cookie_db);
		}
		wget_tcp_set_bind_address(NULL, NULL);
		wget_tcp_set_dns_caching(NULL, 0);
		wget_dns_cache_free();

		rc = wget_net_deinit();
		wget_ssl_deinit();
		wget_http_set_http_proxy(NULL, NULL);
		wget_http_set_https_proxy(NULL, NULL);
		wget_http_set_no_proxy(NULL, NULL);
	}

	if (_init > 0) _init--;

	wget_thread_mutex_unlock(&_mutex);

	if (rc)
		wget_error_printf(_("%s: Failed to deinit networking (%d)"), __func__, rc);
}

int wget_global_get_int(int key)
{
	switch (key) {
	case WGET_DNS_CACHING:
		return wget_tcp_get_dns_caching(NULL);
	case WGET_COOKIES_ENABLED:
		return _config.cookies_enabled;
	case WGET_COOKIE_KEEPSESSIONCOOKIES:
		return _config.keep_session_cookies;
	case WGET_NET_FAMILY_EXCLUSIVE:
		return wget_tcp_get_family(NULL);
	case WGET_NET_FAMILY_PREFERRED:
		return wget_tcp_get_preferred_family(NULL);
	default:
		wget_error_printf(_("%s: Unknown option %d"), __func__, key);
		return 0;
	}
}

const void *wget_global_get_ptr(int key)
{
	switch (key) {
	case WGET_DEBUG_STREAM:
		return wget_logger_get_stream(wget_get_logger(WGET_LOGGER_DEBUG));
	case WGET_DEBUG_FILE:
		return wget_logger_get_file(wget_get_logger(WGET_LOGGER_DEBUG));
	case WGET_ERROR_STREAM:
		return wget_logger_get_stream(wget_get_logger(WGET_LOGGER_ERROR));
	case WGET_ERROR_FILE:
		return wget_logger_get_file(wget_get_logger(WGET_LOGGER_ERROR));
	case WGET_INFO_STREAM:
		return wget_logger_get_stream(wget_get_logger(WGET_LOGGER_INFO));
	case WGET_INFO_FILE:
		return wget_logger_get_file(wget_get_logger(WGET_LOGGER_INFO));
	case WGET_COOKIE_FILE:
		return _config.cookie_file;
	case WGET_COOKIE_DB:
		return &_config.cookie_db;
	default:
		wget_error_printf(_("%s: Unknown option %d"), __func__, key);
		return NULL;
	}
}

wget_global_get_func_t wget_global_get_func(int key)
{
	switch (key) {
	case WGET_DEBUG_FUNC:
		return wget_logger_get_func(wget_get_logger(WGET_LOGGER_DEBUG));
	case WGET_ERROR_FUNC:
		return wget_logger_get_func(wget_get_logger(WGET_LOGGER_ERROR));
	case WGET_INFO_FUNC:
		return wget_logger_get_func(wget_get_logger(WGET_LOGGER_INFO));
	default:
		wget_error_printf(_("%s: Unknown option %d"), __func__, key);
		return NULL;
	}
}
