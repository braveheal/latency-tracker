#!/bin/sh
#
# Copyright (C) 2015 Julien Desfossez <jdesfossez@efficios.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; only
# version 2.1 of the License.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

#lttng-sessiond --extra-kmod-probes=latency_tracker -d
lttng create --snapshot
lttng enable-channel -k chan1 --subbuf-size 2M
lttng enable-event -k -a -c chan1
lttng start

while true; do
	cat /proc/wake_latency
	lttng stop
	lttng snapshot record
	lttng start
done

lttng stop
lttng destroy
