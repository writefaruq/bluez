/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2009  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <string.h>

#include <dbus/dbus.h>

static char *passkey = NULL;

static int do_reject = 0;

static volatile sig_atomic_t __io_canceled = 0;
static volatile sig_atomic_t __io_terminated = 0;

static void sig_term(int sig)
{
	__io_canceled = 1;
}

static DBusHandlerResult agent_filter(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	const char *name, *old, *new;

	if (!dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (!dbus_message_get_args(msg, NULL,
			DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old,
				DBUS_TYPE_STRING, &new, DBUS_TYPE_INVALID)) {
		fprintf(stderr, "Invalid arguments for NameOwnerChanged signal");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (!strcmp(name, "org.bluez") && *new == '\0') {
		fprintf(stderr, "Agent has been terminated\n");
		__io_terminated = 1;
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult request_message(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	DBusMessage *reply;
	const char *path, *address;
	dbus_bool_t numeric;

	if (!passkey)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (!dbus_message_get_args(msg, NULL,
			DBUS_TYPE_STRING, &path, DBUS_TYPE_STRING, &address,
			DBUS_TYPE_BOOLEAN, &numeric, DBUS_TYPE_INVALID)) {
		fprintf(stderr, "Invalid arguments for passkey Request method");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (do_reject) {
		reply = dbus_message_new_error(msg,
					"org.bluez.Error.Rejected", "");
		goto send;
	}

	reply = dbus_message_new_method_return(msg);
	if (!reply) {
		fprintf(stderr, "Can't create reply message\n");
		return DBUS_HANDLER_RESULT_NEED_MEMORY;
	}

	printf("Passkey request for device %s\n", address);

	dbus_message_append_args(reply, DBUS_TYPE_STRING, &passkey,
					DBUS_TYPE_INVALID);

send:
	dbus_connection_send(conn, reply, NULL);

	dbus_connection_flush(conn);

	dbus_message_unref(reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult cancel_message(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	DBusMessage *reply;
	const char *path, *address;

	if (!dbus_message_get_args(msg, NULL,
			DBUS_TYPE_STRING, &path, DBUS_TYPE_STRING, &address,
							DBUS_TYPE_INVALID)) {
		fprintf(stderr, "Invalid arguments for passkey Confirm method");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	printf("Request canceled for device %s\n", address);

	reply = dbus_message_new_method_return(msg);
	if (!reply) {
		fprintf(stderr, "Can't create reply message\n");
		return DBUS_HANDLER_RESULT_NEED_MEMORY;
	}

	dbus_connection_send(conn, reply, NULL);

	dbus_connection_flush(conn);

	dbus_message_unref(reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult release_message(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	DBusMessage *reply;

	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INVALID)) {
		fprintf(stderr, "Invalid arguments for Release method");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (!__io_canceled)
		fprintf(stderr, "Agent has been released\n");

	__io_terminated = 1;

	reply = dbus_message_new_method_return(msg);
	if (!reply) {
		fprintf(stderr, "Can't create reply message\n");
		return DBUS_HANDLER_RESULT_NEED_MEMORY;
	}

	dbus_connection_send(conn, reply, NULL);

	dbus_connection_flush(conn);

	dbus_message_unref(reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult agent_message(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	if (dbus_message_is_method_call(msg, "org.bluez.Agent", "Request"))
		return request_message(conn, msg, data);

	if (dbus_message_is_method_call(msg, "org.bluez.Agent", "Cancel"))
		return cancel_message(conn, msg, data);

	if (dbus_message_is_method_call(msg, "org.bluez.Agent", "Release"))
		return release_message(conn, msg, data);

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable agent_table = {
	.message_function = agent_message,
};

static int register_agent(DBusConnection *conn, const char *device_path,
						const char *agent_path,
						const char *capabilities)
{
	DBusMessage *msg, *reply;
	DBusError err;

	if (!dbus_connection_register_object_path(conn, agent_path,
							&agent_table, NULL)) {
		fprintf(stderr, "Can't register object path for agent\n");
		return -1;
	}

	msg = dbus_message_new_method_call("org.bluez", device_path,
					"org.bluez.Adapter", "RegisterAgent");
	if (!msg) {
		fprintf(stderr, "Can't allocate new method call\n");
		return -1;
	}

	dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH, &agent_path,
					DBUS_TYPE_STRING, &capabilities,
							DBUS_TYPE_INVALID);

	dbus_error_init(&err);

	reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);

	dbus_message_unref(msg);

	if (!reply) {
		fprintf(stderr, "Can't register agent\n");
		if (dbus_error_is_set(&err)) {
			fprintf(stderr, "%s\n", err.message);
			dbus_error_free(&err);
		}
		return -1;
	}

	dbus_message_unref(reply);

	dbus_connection_flush(conn);

	return 0;
}

static int unregister_agent(DBusConnection *conn, const char *device_path,
							const char *agent_path)
{
	DBusMessage *msg, *reply;
	DBusError err;

	msg = dbus_message_new_method_call("org.bluez", device_path,
					"org.bluez.Adapter", "UnregisterAgent");
	if (!msg) {
		fprintf(stderr, "Can't allocate new method call\n");
		return -1;
	}

	dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH, &agent_path,
							DBUS_TYPE_INVALID);

	dbus_error_init(&err);

	reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);

	dbus_message_unref(msg);

	if (!reply) {
		fprintf(stderr, "Can't unregister agent\n");
		if (dbus_error_is_set(&err)) {
			fprintf(stderr, "%s\n", err.message);
			dbus_error_free(&err);
		}
		return -1;
	}

	dbus_message_unref(reply);

	dbus_connection_flush(conn);

	dbus_connection_unregister_object_path(conn, agent_path);

	return 0;
}

static char *get_device(const char *device)
{
	char *path;

	path = strdup("/org/bluez/hci0");

	return path;
}

static void usage(void)
{
	printf("Bluetooth agent ver %s\n\n", VERSION);

	printf("Usage:\n"
		"\tagent [--device interface] [--path agent-path] <passkey>\n"
		"\n");
}

static struct option main_options[] = {
	{ "device",	1, 0, 'i' },
	{ "path",	1, 0, 'p' },
	{ "capabilites",1, 0, 'c' },
	{ "reject",	0, 0, 'r' },
	{ "help",	0, 0, 'h' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char *argv[])
{
	const char *capabilities = "DisplayYesNo";
	struct sigaction sa;
	DBusConnection *conn;
	char match_string[128], default_path[128], *device_id = NULL;
	char *device_path = NULL, *agent_path = NULL;
	int opt;

	snprintf(default_path, sizeof(default_path),
					"/org/bluez/agent_%d", getpid());

	while ((opt = getopt_long(argc, argv, "+i:p:c:rh", main_options, NULL)) != EOF) {
		switch(opt) {
		case 'i':
			device_id = optarg;
			break;
		case 'p':
			if (optarg[0] != '/') {
				fprintf(stderr, "Invalid path\n");
				exit(1);
			}
			agent_path = strdup(optarg);
			break;
		case 'c':
			capabilities = optarg;
			break;
		case 'r':
			do_reject = 1;
			break;
		case 'h':
			usage();
			exit(0);
		default:
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;
	optind = 0;

	if (argc < 1) {
		usage();
		exit(1);
	}

	passkey = strdup(argv[0]);

	if (!agent_path)
		agent_path = strdup(default_path);

	conn = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
	if (!conn) {
		fprintf(stderr, "Can't get on system bus");
		exit(1);
	}

	if (!device_path)
		device_path = get_device(device_id);

	if (register_agent(conn, device_path, agent_path, capabilities) < 0) {
		dbus_connection_unref(conn);
		exit(1);
	}

	if (!dbus_connection_add_filter(conn, agent_filter, NULL, NULL))
		fprintf(stderr, "Can't add signal filter");

	snprintf(match_string, sizeof(match_string),
			"interface=%s,member=NameOwnerChanged,arg0=%s",
					DBUS_INTERFACE_DBUS, "org.bluez");

	dbus_bus_add_match(conn, match_string, NULL);

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags   = SA_NOCLDSTOP;
	sa.sa_handler = sig_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);

	while (!__io_canceled && !__io_terminated) {
		if (dbus_connection_read_write_dispatch(conn, 500) != TRUE)
			break;
	}

	if (!__io_terminated)
		unregister_agent(conn, device_path, agent_path);

	free(device_path);
	free(agent_path);

	free(passkey);

	dbus_connection_unref(conn);

	return 0;
}
