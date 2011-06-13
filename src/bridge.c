/*
 * Copyright 2010 Canonical, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 3, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *    Cody Russell  <crussell@canonical.com>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>

#include <libdbusmenu-gtk/menuitem.h>
#include <libdbusmenu-gtk/parser.h>
#include <libdbusmenu-glib/menuitem.h>
#include <libdbusmenu-glib/server.h>

#include "bridge.h"
#include "gen-application-menu-registrar.xml.h"

#define APP_MENU_DBUS_NAME   "com.canonical.AppMenu.Registrar"
#define APP_MENU_DBUS_OBJECT "/com/canonical/AppMenu/Registrar"
#define APP_MENU_INTERFACE   "com.canonical.AppMenu.Registrar"
#define APP_MENU_PATH        "/this/is/a/long/object/path"

typedef struct _AppWindowContext AppWindowContext;

static void app_menu_bridge_insert         (UbuntuMenuProxy   *proxy,
                                            GtkWidget         *shell,
                                            GtkWidget         *child,
                                            guint              position);
static gboolean app_menu_bridge_show_local (UbuntuMenuProxy   *proxy);
static void     toplevel_mapped            (GtkWidget         *widget,
                                            gpointer           user_data);
static void     mnemonic_shown_cb          (GtkWidget         *widget,
                                            GParamSpec        *pspec,
                                            AppWindowContext  *context);
static void     toplevel_notify_parent_cb  (GtkWidget         *widget,
                                            GParamSpec        *pspec,
                                            UbuntuMenuProxy   *proxy);
static void     menubar_notify_parent_cb   (GtkWidget         *widget,
                                            GParamSpec        *pspec,
                                            UbuntuMenuProxy   *proxy);
static void     rebuild_window_items       (AppMenuBridge     *bridge,
                                            GtkWidget         *toplevel);
static void     rebuild                    (AppMenuBridge     *bridge,
                                            GtkWidget         *toplevel);
static void app_menu_bridge_proxy_vanished (AppMenuBridge *bridge);
static void app_menu_bridge_proxy_appeared (AppMenuBridge *bridge);
static void appmenuproxy_created_cb        (GObject *          object,
                                            GAsyncResult *     res,
                                            gpointer           user_data);

struct _AppWindowContext
{
  GtkWidget        *window;
  DbusmenuServer   *server;
  gchar            *path;
  gboolean          registered;
  AppMenuBridge    *bridge;
  GCancellable     *cancel;
};

struct _AppMenuBridgePrivate
{
  GList *windows;

  GDBusProxy       *appmenuproxy;
  gboolean          online;
};

typedef struct _RecurseContext
{
  AppMenuBridge    *bridge;
  AppWindowContext *context;

  gint count;
  gboolean previous;
  DbusmenuMenuitem *stack[30];
} RecurseContext;

G_DEFINE_DYNAMIC_TYPE(AppMenuBridge, app_menu_bridge, UBUNTU_TYPE_MENU_PROXY)

static GHashTable *rebuild_ids = NULL;
static GDBusNodeInfo *      registrar_node_info = NULL;
static GDBusInterfaceInfo * registrar_interface_info = NULL;

static void
activate_menu (AppMenuBridge *bridge,
               GtkWidget     *widget,
               gpointer       user_data)
{
  DbusmenuMenuitem *mi = dbusmenu_gtk_parse_get_cached_item (widget);

  if (mi != NULL)
    {
      dbusmenu_menuitem_show_to_user (mi, 0);
    }

  return;
}

static void
app_menu_bridge_init (AppMenuBridge *bridge)
{
  bridge->priv = G_TYPE_INSTANCE_GET_PRIVATE (bridge, APP_MENU_TYPE_BRIDGE, AppMenuBridgePrivate);

  bridge->priv->windows = NULL;

  bridge->priv->appmenuproxy = NULL;
  g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION,
                           G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS | G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                           registrar_interface_info,
                           APP_MENU_DBUS_NAME,
                           APP_MENU_DBUS_OBJECT,
                           APP_MENU_INTERFACE,
                           NULL, /* TODO: cancelable */
                           appmenuproxy_created_cb,
                           bridge);

  bridge->priv->online = FALSE;

  g_signal_connect (bridge,
                    "activate-menu",
                    G_CALLBACK (activate_menu),
                    NULL);

  return;
}

static void
context_dispose (AppWindowContext *context)
{
  if (context->cancel != NULL)
    {
      g_cancellable_cancel (context->cancel);
      g_object_unref (context->cancel);
      context->cancel = NULL;
    }

  if (context->server != NULL)
    {
      g_object_unref (context->server);
      context->server = NULL;
    }

  if (context->window != NULL)
    {
      g_signal_handlers_disconnect_by_func(context->window, G_CALLBACK(mnemonic_shown_cb), context);
      g_object_remove_weak_pointer(G_OBJECT(context->window), (gpointer *)&(context->window));
      context->window = NULL;
    }
}

static void
context_free (AppWindowContext *context)
{
  context_dispose (context);

  if (context->path != NULL)
    {
      g_free (context->path);
      context->path = NULL;
    }

  g_free (context);
}

static void
app_menu_bridge_dispose (GObject *object)
{
  AppMenuBridge *bridge = APP_MENU_BRIDGE (object);

  g_list_foreach (bridge->priv->windows, (GFunc)context_dispose, NULL);

  if (bridge->priv->appmenuproxy)
    {
      g_object_unref (bridge->priv->appmenuproxy);
      bridge->priv->appmenuproxy = NULL;
    }
}

static void
app_menu_bridge_finalize (GObject *object)
{
  AppMenuBridge *bridge = APP_MENU_BRIDGE (object);

  g_list_foreach (bridge->priv->windows, (GFunc)context_free, NULL);
  g_list_free (bridge->priv->windows);
  bridge->priv->windows = NULL;

  G_OBJECT_CLASS (app_menu_bridge_parent_class)->finalize (object);
}

static void
toplevel_unmapped (GtkWidget *widget,
                   gpointer   user_data)
{
  AppWindowContext *context = (AppWindowContext *)user_data;

  g_signal_handlers_disconnect_by_func(widget,
                                       G_CALLBACK(toplevel_unmapped),
                                       user_data);

  if (context)
    {
      context->bridge->priv->windows = g_list_remove (context->bridge->priv->windows, context);
      context_free (context);
    }
}

static void
app_menu_bridge_class_init (AppMenuBridgeClass *class)
{
  UbuntuMenuProxyClass *proxy_class;
  GObjectClass *object_class;

  app_menu_bridge_parent_class = g_type_class_peek_parent (class);

  proxy_class = UBUNTU_MENU_PROXY_CLASS (class);
  object_class = G_OBJECT_CLASS (class);

  proxy_class->insert = app_menu_bridge_insert;
  proxy_class->show_local = app_menu_bridge_show_local;

  object_class->dispose  = app_menu_bridge_dispose;
  object_class->finalize = app_menu_bridge_finalize;

  g_type_class_add_private (class, sizeof (AppMenuBridgePrivate));

	if (registrar_node_info == NULL) {
		GError * error = NULL;

		registrar_node_info = g_dbus_node_info_new_for_xml(_application_menu_registrar, &error);
		if (error != NULL) {
			g_error("Unable to parse app menu registrar Interface description: %s", error->message);
			g_error_free(error);
		}
	}

	if (registrar_interface_info == NULL) {
		registrar_interface_info = g_dbus_node_info_lookup_interface(registrar_node_info, APP_MENU_INTERFACE);

		if (registrar_interface_info == NULL) {
			g_error("Unable to find interface '" APP_MENU_INTERFACE "'");
		}
	}

	return;
}

static void
app_menu_bridge_class_finalize (AppMenuBridgeClass *class)
{
}

struct AlterMenuInfo
{
  AppMenuBridge *bridge;
  gboolean       visible;
};

static gboolean
alter_menu_visibility(struct AlterMenuInfo *info)
{
  g_object_set (info->bridge, "show-local", info->visible, NULL);
  g_free(info);
  return FALSE;
}

static void
window_state_event_callback(GtkWidget *widget, GdkEventWindowState *event,
                            AppMenuBridge *bridge)
{
  struct AlterMenuInfo *info = g_new(struct AlterMenuInfo, 1);

  info->bridge = bridge;
  info->visible = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) == 0;

  /* This delay is necessary to prevent a bug where GNOME Terminal won't resize
     correctly on restore. */
  g_timeout_add (50, (GSourceFunc)alter_menu_visibility, info);
}

static void
assign_signals(AppWindowContext *context, AppMenuBridge *bridge)
{
  GtkWidget *window = gtk_widget_get_toplevel (context->window);
  g_signal_connect (window,
                    "window-state-event",
                    G_CALLBACK (window_state_event_callback),
                    bridge);
}

static void
app_menu_bridge_set_show_local (AppMenuBridge *bridge,
                                gboolean       local)
{
  const gchar *env = g_getenv ("APPMENU_DISPLAY_BOTH");

  if (g_strcmp0 (env, "1") == 0)
    g_object_set (bridge, "show-local", TRUE, NULL);
  else {
    /* The state event signal is used for runtime menubar hiding/unhiding,
       but the signal won't always emit on application startup.
       That's why we also need this initial check. It will only hide the menu if
       one of the windows are maximized. */
    GList *tmp;

    for (tmp = bridge->priv->windows; tmp != NULL; tmp = tmp->next) {
      AppWindowContext *context = tmp->data;
      GdkWindow *window = gtk_widget_get_window (context->window);

      if (gdk_window_get_state(window) & GDK_WINDOW_STATE_MAXIMIZED) {
        g_object_set (bridge, "show-local", FALSE, NULL);
        break;
      } else {
        g_object_set (bridge, "show-local", TRUE, NULL);
      }
    }

    g_list_foreach (bridge->priv->windows, (GFunc)assign_signals, bridge);
  }
}

static void
register_application_window_cb (GObject          *object,
                                GAsyncResult     *res,
                                void             *user_data)
{
  GError * error = NULL;
  AppWindowContext *context = (AppWindowContext *)user_data;

  GVariant * variants = g_dbus_proxy_call_finish(G_DBUS_PROXY(object), res, &error);

  if (variants != NULL) {
    /* Only unref variants if we get some.  Doing this hear instead of
       with the error so that it's clear we don't use it. */
    g_variant_unref(variants);
  }

  if (error != NULL &&
      error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED)
    {
      /* If we were cancelled, we've been disposed (and possibly finalized) and
         shouldn't trust anything in context.  This is because cancel callbacks
         are done in the idle loop for GIO functions. */
      g_error_free(error);
      return;
    }

  if (context->cancel != NULL)
    {
      g_object_unref (context->cancel);
      context->cancel = NULL;
    }

  if (error != NULL)
    {
      g_warning("Unable to register window with path '%s': %s", context->path, error->message);
      g_error_free(error);

      context->registered = FALSE;

      if (context->bridge != NULL)
          app_menu_bridge_set_show_local (context->bridge, TRUE);

      return;
    }

  context->registered = TRUE;

  if (context->bridge != NULL)
    app_menu_bridge_set_show_local (context->bridge, FALSE);
}

static void
register_application_windows (AppMenuBridge *bridge)
{
  GList *tmp = NULL;

  for (tmp = bridge->priv->windows; tmp != NULL; tmp = tmp->next)
    {
      AppWindowContext *context = tmp->data;
      GtkWidget *widget = context->window;

      if (bridge->priv->appmenuproxy == NULL || bridge->priv->online == FALSE)
        {
          if (context->bridge != NULL)
            {
              app_menu_bridge_set_show_local (context->bridge, TRUE);
            }
          continue;
        }

      if (!context->registered && context->server != NULL &&
          context->cancel == NULL && GTK_IS_WINDOW (widget) &&
          bridge->priv->appmenuproxy != NULL)
        {
          context->cancel = g_cancellable_new ();
          g_dbus_proxy_call(bridge->priv->appmenuproxy,
                            "RegisterWindow",
                            g_variant_new("(uo)",
                                          GDK_WINDOW_XID(gtk_widget_get_window(widget)),
                                          context->path),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1, /* timeout */
                            context->cancel,
                            register_application_window_cb,
                            context);
        }
    }
}

static void
unregister_application_windows (AppMenuBridge *bridge)
{
  GList *tmp = NULL;

  for (tmp = bridge->priv->windows; tmp != NULL; tmp = tmp->next)
    {
      AppWindowContext *context = tmp->data;

      context->registered = FALSE;
    }

  app_menu_bridge_set_show_local (bridge, TRUE);
}

static void
app_menu_bridge_proxy_vanished (AppMenuBridge *bridge)
{
  bridge->priv->online = FALSE;

  unregister_application_windows (bridge);
}

static void
app_menu_bridge_proxy_appeared (AppMenuBridge *bridge)
{
  bridge->priv->online = TRUE;

  register_application_windows (bridge);
}

/* Gets called anytime the name owner changes, this is typically
   when the indicator crashes or gets removed. */
static void
appmenuproxy_owner_changed (GObject * object, GParamSpec * pspec, gpointer user_data)
{
	AppMenuBridge * bridge = APP_MENU_BRIDGE(user_data);
	g_return_if_fail(bridge != NULL);

	gchar * name = g_dbus_proxy_get_name_owner(bridge->priv->appmenuproxy);
	if (name != NULL) {
		g_free(name);
		app_menu_bridge_proxy_appeared(bridge);
	} else {
		app_menu_bridge_proxy_vanished(bridge);
	}

	return;
}

/* Callback for the asyncronous creation of the proxy object.
   If it is created successfully we act like it just appeared, otherwise
   we error as if it vanished */
static void
appmenuproxy_created_cb (GObject * object, GAsyncResult * res, gpointer user_data)
{
	GError * error = NULL;
	GDBusProxy * proxy = NULL;

	proxy = g_dbus_proxy_new_for_bus_finish(res, &error);
	if (error != NULL) {
		g_warning("Unable to create Ubuntu Menu Proxy: %s", error->message);
		g_error_free(error);
		/* No return, we want to still call vanished */
	}

	AppMenuBridge * bridge = APP_MENU_BRIDGE(user_data);
	g_return_if_fail(bridge != NULL);

	bridge->priv->appmenuproxy = proxy;

	gchar * name = NULL;
	if (proxy != NULL) {
		name = g_dbus_proxy_get_name_owner(proxy);

		g_signal_connect(G_OBJECT(proxy),
		                 "notify::g-name-owner",
		                 G_CALLBACK(appmenuproxy_owner_changed),
		                 bridge);
	}

	/* Note: name will be NULL if proxy was NULL */
	if (name != NULL) {
		g_free(name);
		app_menu_bridge_proxy_appeared(bridge);
	} else {
		app_menu_bridge_proxy_vanished(bridge);
	}

	return;
}

typedef struct _RebuildData {
  AppMenuBridge *bridge;
  GtkWidget     *widget;
} RebuildData;

static gboolean
do_rebuild (RebuildData *data)
{
  if (data->widget != NULL && gtk_widget_is_toplevel (data->widget))
    {
      rebuild_window_items (data->bridge,
                            data->widget);
    }

  if (data->widget != NULL)
    {
      g_object_remove_weak_pointer (G_OBJECT (data->widget), (gpointer*)&data->widget);
      g_hash_table_remove (rebuild_ids, data->widget);
    }

  g_free (data);

  return FALSE;
}

static void
rebuild (AppMenuBridge *bridge,
         GtkWidget     *toplevel)
{
  guint id = 0;

  if (rebuild_ids != NULL)
    {
      id = GPOINTER_TO_UINT (g_hash_table_lookup (rebuild_ids, toplevel));

      if (id > 0)
        {
          g_source_remove (id);
          g_hash_table_remove (rebuild_ids, toplevel);
          id = 0;
        }
    }

  RebuildData *data = g_new0 (RebuildData, 1);
  data->bridge = bridge;
  data->widget = toplevel;

  id = g_timeout_add (100,
                      (GSourceFunc)do_rebuild,
                      data);

  g_object_add_weak_pointer (G_OBJECT (data->widget), (gpointer*)&data->widget);

  if (rebuild_ids == NULL)
    {
      rebuild_ids = g_hash_table_new (g_direct_hash, g_direct_equal);
    }

  g_hash_table_insert (rebuild_ids, toplevel, GUINT_TO_POINTER (id));
}

static DbusmenuMenuitem * find_menu_bar (GtkWidget * widget);

static void
find_menu_bar_helper (GtkWidget * widget, gpointer data)
{
	DbusmenuMenuitem ** mi = (DbusmenuMenuitem **)data;

	/* We've already found a menu, let's get through the
	   foreach as quickly as possible */
	if (*mi != NULL) {
		return;
	}

	*mi = find_menu_bar(widget);
	return;
}

static DbusmenuMenuitem *
find_menu_bar (GtkWidget * widget)
{
	if (GTK_IS_MENU_BAR(widget) || GTK_IS_MENU_ITEM(widget)) {
		return dbusmenu_gtk_parse_menu_structure(widget);
	}

	if (GTK_IS_CONTAINER(widget)) {
		DbusmenuMenuitem * mi = NULL;

		gtk_container_foreach(GTK_CONTAINER(widget), find_menu_bar_helper, &mi);

		return mi;
	}

	return NULL;
}

/* Respond to changing of the mnemonics shown property to say
   to the appmenu wether we need to have the menus shown on the
   panel.  If there is no auto-mnemonics in this theme we're just
   not doing this as we can't tell what the user wanted. */
static void
mnemonic_shown_cb (GtkWidget       *widget,
                    GParamSpec      *pspec,
                    AppWindowContext *context)
{
	DbusmenuStatus dstatus = DBUSMENU_STATUS_NORMAL;
	if (context->window != NULL) {
		gboolean mshown = gtk_window_get_mnemonics_visible(GTK_WINDOW(context->window));
		gboolean autom;

		g_object_get(gtk_widget_get_settings(context->window), "gtk-auto-mnemonics", &autom, NULL);

		if (autom && mshown) {
			dstatus = DBUSMENU_STATUS_NOTICE;
		}
	}

	if (context->server != NULL) {
		/* g_debug("Setting dbusmenu server status to: %d", dstatus); */
		dbusmenu_server_set_status(context->server, dstatus);
	}

	return;
}

static void
rebuild_window_items (AppMenuBridge *bridge,
                      GtkWidget     *toplevel)
{
  XID xid;
  AppWindowContext *context = NULL;

  /* Disconnect any "map" signal and reconnect, which guarantees that there is
     at least one and at most one listener */
  g_signal_handlers_disconnect_by_func(toplevel,
                                       G_CALLBACK(toplevel_mapped),
                                       bridge);
  g_signal_connect (toplevel, "map",
                    G_CALLBACK (toplevel_mapped),
                    bridge);

  if (!GTK_IS_WINDOW (toplevel))
    {
      g_signal_connect (G_OBJECT (toplevel),
                        "notify::parent",
                        G_CALLBACK (toplevel_notify_parent_cb),
                        bridge);

      return;
    }
  else if (g_object_class_find_property (G_OBJECT_GET_CLASS (toplevel),
                                         "ubuntu-no-proxy") != NULL)

    {
      gboolean no_proxy;

      g_object_get (G_OBJECT (toplevel),
                    "ubuntu-no-proxy", &no_proxy,
                    NULL);

      if (no_proxy)
        {
          return;
        }
    }

  if (!gtk_widget_get_mapped (toplevel))
    {
      return;
    }

  xid = GDK_WINDOW_XID (gtk_widget_get_window (toplevel));

  GList *tmp;
  gboolean found = FALSE;

  for (tmp = bridge->priv->windows; tmp != NULL; tmp = tmp->next)
    {
      context = tmp->data;

      if (context && GTK_IS_WIDGET (context->window))
        {
          XID xid2 = GDK_WINDOW_XID (gtk_widget_get_window (context->window));

          if (xid == xid2)
            {
              found = TRUE;
              break;
            }
        }
    }

  if (!found)
    {
      context = g_new0 (AppWindowContext, 1);
      context->bridge = bridge;
      context->cancel = NULL;
      bridge->priv->windows = g_list_prepend (bridge->priv->windows, context);
    }

  if (context->window)
    {
      if (context->window != toplevel)
        {
          g_signal_handlers_disconnect_by_func (context->window,
                                                G_CALLBACK (toplevel_unmapped),
                                                context);
        }
    }

  if (context->window != toplevel)
    {
      if (context->window != NULL) {
        g_object_remove_weak_pointer(G_OBJECT(context->window), (gpointer *)&(context->window));
        g_signal_handlers_disconnect_by_func(context->window, G_CALLBACK(mnemonic_shown_cb), context);
      }

      context->window = toplevel;

      g_object_add_weak_pointer(G_OBJECT(context->window), (gpointer *)&(context->window));
      g_signal_connect (toplevel,
                        "unmap",
                        G_CALLBACK (toplevel_unmapped),
                        context);
    }

  if (!context->path)
    context->path = g_strdup_printf ("/com/canonical/menu/%X", (guint)xid);

  if (!context->server) {
    context->server = dbusmenu_server_new (context->path);

    GtkTextDirection dir = gtk_widget_get_default_direction();
    if (dir != GTK_TEXT_DIR_NONE) {
      dbusmenu_server_set_text_direction(context->server, dir == GTK_TEXT_DIR_LTR ? DBUSMENU_TEXT_DIRECTION_LTR : DBUSMENU_TEXT_DIRECTION_RTL);
    }
  }

  if (context->window != NULL) {
    g_signal_connect (G_OBJECT (toplevel),
                      "notify::mnemonics-visible",
                      G_CALLBACK (mnemonic_shown_cb),
                      context);
  }

  DbusmenuMenuitem * mi = find_menu_bar(toplevel);
  dbusmenu_server_set_root(context->server, mi);
  if (mi != NULL) {
    g_object_unref(G_OBJECT(mi));
  }

  register_application_windows (bridge);
}

static void
toplevel_mapped (GtkWidget *widget,
                 gpointer   user_data)
{
  AppMenuBridge *bridge = APP_MENU_BRIDGE (user_data);

  if (GTK_IS_WINDOW (widget))
    {
      rebuild (bridge, widget);
      //register_application_windows (bridge);

      return;
    }
}

static void
toplevel_notify_parent_cb (GtkWidget       *widget,
                           GParamSpec      *pspec,
                           UbuntuMenuProxy *proxy)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
  AppMenuBridge *bridge = APP_MENU_BRIDGE (proxy);

  if (gtk_widget_get_parent (widget) == NULL)
    return;

  if (GTK_IS_WINDOW (toplevel))
    {
      g_signal_handlers_disconnect_by_func (widget,
                                            G_CALLBACK (toplevel_notify_parent_cb),
                                            proxy);
      rebuild (bridge, toplevel);
    }
  else
    {
      g_signal_connect (G_OBJECT (toplevel),
                        "notify::parent",
                        G_CALLBACK (toplevel_notify_parent_cb),
                        proxy);
    }
}

static void
menubar_notify_parent_cb (GtkWidget       *widget,
                          GParamSpec      *pspec,
                          UbuntuMenuProxy *proxy)
{
  if (gtk_widget_get_parent (widget) == NULL)
    return; /* TODO: Should clear all entries (find old context and set root to NULL) */

  /* Rebuild now or when toplevel window appears */
  toplevel_notify_parent_cb (widget, NULL, proxy);
}

static void
attach_notify_cb (GtkWidget     *widget,
                  GParamSpec    *pspec,
                  AppMenuBridge *bridge)
{
  if (pspec->name == g_intern_static_string ("attach-widget"))
    {
      GtkWidget *attach = NULL;

      g_object_get (widget, "attach-widget", &attach, NULL);

      rebuild (bridge, attach);
    }
}

static void
app_menu_bridge_insert (UbuntuMenuProxy *proxy,
                        GtkWidget       *parent,
                        GtkWidget       *child,
                        guint            position)
{
  AppMenuBridge        *bridge;
  AppMenuBridgePrivate *priv;
  GtkWidget            *toplevel = NULL;

  if (GTK_IS_TEAROFF_MENU_ITEM (child))
    return;

  bridge = APP_MENU_BRIDGE (proxy);
  priv = bridge->priv;

  toplevel = gtk_widget_get_toplevel (parent);

  if (GTK_IS_MENU_BAR (parent))
    {
      /* Watch for toplevel window to appear */
      if (!GTK_IS_WINDOW (toplevel) &&
          g_signal_handler_find (toplevel, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                 toplevel_notify_parent_cb, NULL) == 0)
        {
          g_signal_connect (toplevel,
                            "notify::parent",
                            G_CALLBACK (toplevel_notify_parent_cb),
                            proxy);
        }

      /* Watch for parent changes for the menubar itself */
      if (g_signal_handler_find (parent, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                 menubar_notify_parent_cb, NULL) == 0)
        {
          g_signal_connect (parent,
                            "notify::parent",
                            G_CALLBACK (menubar_notify_parent_cb),
                            proxy);
        }
    }
  else if (GTK_IS_MENU (parent))
    {
      GtkWidget *attach = NULL;

      g_object_get (parent, "attach-widget", &attach, NULL);

      if (attach == NULL)
        {
          g_signal_connect (G_OBJECT (parent),
                            "notify",
                            G_CALLBACK (attach_notify_cb),
                            bridge);
          return;
        }
      else
        {
          DbusmenuMenuitem *mi = dbusmenu_gtk_parse_get_cached_item (attach);

          if (mi != NULL)
            {
              DbusmenuMenuitem *child_dmi = dbusmenu_gtk_parse_menu_structure (child);

              g_object_set_data (G_OBJECT (child_dmi), "dbusmenu-parent", mi);
              dbusmenu_menuitem_child_add_position (mi,
                                                    child_dmi,
                                                    position);
              return;
            }

          rebuild (bridge, toplevel);
        }
    }

  if (GTK_IS_WINDOW (toplevel))
    {
      if (gtk_widget_get_mapped (toplevel))
        {
          rebuild (bridge, toplevel);
        }
      else
        {
          g_signal_connect (toplevel, "map",
                            G_CALLBACK (toplevel_mapped),
                            bridge);
        }
    }
}

static gboolean
app_menu_bridge_show_local (UbuntuMenuProxy *proxy)
{
  gboolean local;

  g_object_get (proxy,
                "show-local", &local,
                NULL);

  return local;
}

/* Crude blacklist to avoid patching innocent apps */
/* Use xprop | grep CLASS to find the name to use */
static gboolean
app_menu_brige_shouldnt_load (void)
{
  const char *prg = g_get_prgname ();

  if ((g_strrstr (prg, "indicator-applet") != NULL)
      || (g_strcmp0 (prg, "indicator-loader") == 0)
      || (g_strcmp0 (prg, "mutter") == 0)
      || (g_strcmp0 (prg, "midori") == 0)
      || (g_strcmp0 (prg, "firefox-bin") == 0)
      || (g_strcmp0 (prg, "thunderbird-bin") == 0)
      || (g_strcmp0 (prg, "Eclipse") == 0)
      || (g_strcmp0 (prg, "emacs") == 0)
      || (g_strcmp0 (prg, "emacs23") == 0)
      || (g_strcmp0 (prg, "emacs23-lucid") == 0)
      || (g_strcmp0 (prg, "gnome-panel") == 0))
    {
      return TRUE;
    }

  return FALSE;
}


G_MODULE_EXPORT void
menu_proxy_module_load (UbuntuMenuProxyModule *module)
{
  static gboolean registered = FALSE;

  /* Prevent well-known applications to re-export
	 their own dbusmenus */
  if (app_menu_brige_shouldnt_load ())
	  return;

  if (!registered)
    {
      app_menu_bridge_register_type (G_TYPE_MODULE (module));

      registered = TRUE;
    }
}

G_MODULE_EXPORT void
menu_proxy_module_unload (UbuntuMenuProxyModule *module)
{
  if (rebuild_ids)
    {
      g_hash_table_destroy (rebuild_ids);
      rebuild_ids = NULL;
    }
}
