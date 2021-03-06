/***************************************************************************
 * Copyright (C) 2017 - 2020, Lanka Hsu, <lankahsu@gmail.com>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/
#include <signal.h>
#include <getopt.h>

#include "utilx9.h"
#include "dbus_def.h"

#define TAG "dbus_123"

// ** app **
static int is_quit = 0;
static int is_service = 0;
char msg[LEN_OF_BUF256]="";

static DBusHandlerResult dbus_filter_cb(DBusConnection *connection, DBusMessage *message, void *usr_data)
{
	dbus_bool_t handled = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	//const char *method = dbus_message_get_member(message);
	//const char *iface = dbus_message_get_interface(message);
	//const char *path = dbus_message_get_path(message);

	if (dbus_message_is_signal(message, DBUS_S_IFAC_DEMO, DBUS_METHOD_COMMAND))
	{
		handled = demo_signal_cb(connection, message, NULL);
	}
	else if ( dbus_message_is_method_call(message, DBUS_M_IFAC_DEMO_CMD, DBUS_METHOD_COMMAND))
	{
		handled = echo_method_cb(connection, message, NULL);
	}

	return handled;
}

static int dbus_match_cb(DBusConnection *dbus_listen, DBusError *err)
{
	int ret = -1;

	// add a rule for which messages we want to see
	dbus_bus_add_match(dbus_listen, DBUS_S_MATCH_DEMO, err); // see signals from the given interface
	if (dbus_error_is_set(err))
	{
		DBG_ER_LN("dbus_bus_add_match error !!! (%s, %s)", DBUS_S_MATCH_DEMO, err->message);
		goto exit_match;
	}

	dbus_bus_request_name(dbus_listen, DBUS_DEMO_DEST, DBUS_NAME_FLAG_REPLACE_EXISTING, err);
	if (dbus_error_is_set(err))
	{
		DBG_ER_LN("dbus_bus_request_name error !!! (%s, %s)", DBUS_DEMO_DEST, err->message );
		goto exit_match;
	}

	ret = 0;

exit_match:

	return ret;
}

static int app_quit(void)
{
	return is_quit;
}

static void app_loop(void)
{
	dbus_path_set("/com/xbox/dbus_123");
	if (is_service)
	{
		dbus_thread_init(dbus_match_cb, dbus_filter_cb);

		while (app_quit()==0)
		{
			sleep(1);
		}
	}
	else
	{
		dbus_client_init();

		dbus_signal_str(DBUS_S_IFAC_DEMO, DBUS_METHOD_COMMAND, msg);

		char *retStr = dbus_method_str2str(DBUS_DEMO_DEST, DBUS_M_IFAC_DEMO_CMD, DBUS_METHOD_COMMAND, msg, TIMEOUT_OF_DBUS_REPLY);
		DBG_IF_LN("(retStr: %s)", retStr);
		SAFE_FREE(retStr);
		
		dbus_conn_free();
	}
}

static int app_init(void)
{
	int ret = 0;

	return ret;
}

static void app_set_quit(int mode)
{
	is_quit = mode;
}

static void app_stop(void)
{
	if (app_quit()==0)
	{
		app_set_quit(1);
	}
}
static void app_exit(void)
{
	app_stop();
}

static void app_signal_handler(int signum)
{
	DBG_ER_LN("(signum: %d)", signum);
	switch (signum)
	{
		case SIGINT:
		case SIGTERM:
		case SIGHUP:
			app_stop();
			break;
		case SIGPIPE:
			break;

		case SIGUSR1:
			break;

		case SIGUSR2:
			dbg_lvl_round();
			DBG_ER_LN("dbg_lvl_get(): %d", dbg_lvl_get());
			break;
	}
}

static void app_signal_register(void)
{
	signal(SIGINT, app_signal_handler );
	signal(SIGTERM, app_signal_handler );
	signal(SIGHUP, app_signal_handler );
	signal(SIGUSR1, app_signal_handler );
	signal(SIGUSR2, app_signal_handler );

	signal(SIGPIPE, SIG_IGN );
}

int option_index = 0;
const char* short_options = "d:e:sh";
static struct option long_options[] =
{
	{ "debug",       required_argument,   NULL,    'd'  },
	{ "service",     no_argument,         NULL,    's'  },
	{ "echo",        no_argument,         NULL,    'e'  },
	{ "help",        no_argument,         NULL,    'h'  },
	{ 0,             0,                      0,    0    }
};

static void app_showusage(int exit_code)
{
	printf( "Usage: %s\n"
					"  -d, --debug       debug level\n"
					"  -e, --echo        message\n"
					"  -s, --service\n"
					"  -h, --help\n", TAG);
	printf( "Version: %s\n", version_show());
	printf( "Example:\n"
					"  %s -d 4 -s\n", TAG);
	exit(exit_code);
}

static void app_ParseArguments(int argc, char **argv)
{
	int opt;

	while((opt = getopt_long (argc, argv, short_options, long_options, &option_index)) != -1)  
	{
		switch (opt)
		{
			case 'd':
				if (optarg)
				{
					dbg_lvl_set(atoi(optarg));
				}
				break;
			case 'e':
				if (optarg)
				{
					SAFE_SPRINTF(msg, "%s", optarg);
				}
				break;
			case 's':
				is_service = 1;
				break;
			default:
				app_showusage(-1); break;
		}
	}
}

int main(int argc, char *argv[])
{
	app_ParseArguments(argc, argv);
	app_signal_register();
	atexit(app_exit);

	if ( app_init() == -1 )
	{
		return -1;
	}

	app_loop();

	return 0;
}
