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

#include <errno.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include <gdbus.h>

#include "logging.h"
#include "../src/adapter.h"
#include "../src/device.h"

#include "server.h"
#include "manager.h"

static int idle_timeout = 0;

static DBusConnection *connection = NULL;


static int dmtx_server_probe(struct btd_adapter *adapter)
{
	bdaddr_t src;

	adapter_get_address(adapter, &src);

	return server_start(&src);
}

static void dmtx_server_remove(struct btd_adapter *adapter)
{
	bdaddr_t src;

	adapter_get_address(adapter, &src);

	server_stop(&src);
}



static struct btd_adapter_driver dmtx_server_driver = {
	.name   = "dmtx-server",
	.probe  = dmtx_server_probe,
	.remove = dmtx_server_remove,
};

int dmtx_manager_init(DBusConnection *conn, GKeyFile *config)
{
	GError *err = NULL;

	if (config) {
		idle_timeout = g_key_file_get_integer(config, "General",
						"IdleTimeout", &err);
		if (err) {
			debug("dmtx.conf: %s", err->message);
			g_error_free(err);
		}
	}

	connection = dbus_connection_ref(conn);

	btd_register_adapter_driver(&dmtx_server_driver);

	return 0;
}

void dmtx_manager_exit(void)
{

	btd_unregister_adapter_driver(&dmtx_server_driver);

	dbus_connection_unref(connection);

	connection = NULL;
}
