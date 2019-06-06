/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Richard Hughes <rhughes@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "gsd-subman-common.h"

const gchar *
gsd_subman_subscription_status_to_string (GsdSubmanSubscriptionStatus status)
{
	if (status == GSD_SUBMAN_SUBSCRIPTION_STATUS_VALID)
		return "valid";
	if (status == GSD_SUBMAN_SUBSCRIPTION_STATUS_INVALID)
		return "invalid";
	if (status == GSD_SUBMAN_SUBSCRIPTION_STATUS_DISABLED)
		return "disabled";
	if (status == GSD_SUBMAN_SUBSCRIPTION_STATUS_PARTIALLY_VALID)
		return "partially-valid";
	return "unknown";
}
