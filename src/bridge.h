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

#ifndef __APP_MENU_BRIDGE_H__
#define __APP_MENU_BRIDGE_H__

#include <gtk/ubuntumenuproxy.h>

#define APP_MENU_TYPE_BRIDGE         (app_menu_bridge_get_type ())
#define APP_MENU_BRIDGE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), APP_MENU_TYPE_BRIDGE, AppMenuBridge))
#define APP_MENU_BRIDGE_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), APP_MENU_TYPE_BRIDGE, AppMenuBridgeClass))
#define APP_MENU_IS_BRIDGE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), APP_MENU_TYPE_BRIDGE))
#define APP_MENU_IS_BRIDGE_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c), APP_MENU_TYPE_BRIDGE))
#define APP_MENU_BRIDGE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), APP_MENU_TYPE_BRIDGE, AppMenuBridgeClass))

typedef struct _AppMenuBridge        AppMenuBridge;
typedef struct _AppMenuBridgeClass   AppMenuBridgeClass;
typedef struct _AppMenuBridgePrivate AppMenuBridgePrivate;

struct _AppMenuBridge
{
  UbuntuMenuProxy parent_object;
  AppMenuBridgePrivate *priv;
};

struct _AppMenuBridgeClass
{
  UbuntuMenuProxyClass parent_class;
};

GType app_menu_bridge_get_type () G_GNUC_CONST;

#endif /* __APP_MENU_BRIDGE_H__ */
