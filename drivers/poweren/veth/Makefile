# Copyright (c) 2010-2011 IBM Corp.
# All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

ccflags-y += -Idrivers/poweren/pcie_endpoint

obj-$(CONFIG_POWEREN_VIRTIO_VETH) := poweren_veth.o
poweren_veth-objs := poweren_veth_vf.o poweren_veth_slotmgr.o poweren_veth_sma.o poweren_veth_vnet.o poweren_veth_core.o
