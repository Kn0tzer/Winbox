/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"
#include "../../io/fsfilenode.h"
#include "kstat.h"

// The kernel registers one evdev gamepad (/dev/input/event5, char 13:69,
// see DevInputJoystick in devinput.cpp) but guests had no /sys at all, so
// libudev-based discovery (Wine's winebus udev backend) enumerated an
// empty input class and the device was invisible. Mirror the real sysfs
// shape for it, following the sysfb.cpp pattern: a class symlink into a
// virtual device node carrying the dev/uevent attributes libudev reads.
// Identity and capability masks match the kernel device exactly
// (BUS_USB 0x045e:0x028e, buttons BTN_SOUTH..BTN_THUMBR, axes
// X/Y/Z/RX/RY/RZ/HAT0X/HAT0Y).
void sysAddInput(const std::shared_ptr<FsNode>& sysNode, const std::shared_ptr<FsNode>& devicesNode, const std::shared_ptr<FsNode>& rootNode) {
	std::shared_ptr<FsNode> classNode = sysNode->getChildByName(B("class"));
	if (!classNode) {
		classNode = Fs::addFileNode(B("/sys/class"), B(""), B(""), true, sysNode);
	}
	std::shared_ptr<FsNode> inputClassNode = classNode->getChildByName(B("input"));
	if (!inputClassNode) {
		inputClassNode = Fs::addFileNode(B("/sys/class/input"), B(""), B(""), true, classNode);
	}
	Fs::addDynamicLinkFile(B("/sys/class/input/event5"), k_mdev(0, 0), inputClassNode, false, B("../../devices/virtual/input/input5/event5"));

	std::shared_ptr<FsNode> virtualNode = devicesNode->getChildByName(B("virtual"));
	if (!virtualNode) {
		virtualNode = Fs::addFileNode(B("/sys/devices/virtual"), B(""), B(""), true, devicesNode);
	}
	std::shared_ptr<FsNode> inputNode = virtualNode->getChildByName(B("input"));
	if (!inputNode) {
		inputNode = Fs::addFileNode(B("/sys/devices/virtual/input"), B(""), B(""), true, virtualNode);
	}
	std::shared_ptr<FsNode> padNode = inputNode->getChildByName(B("input5"));
	if (!padNode) {
		padNode = Fs::addFileNode(B("/sys/devices/virtual/input/input5"), B(""), B(""), true, inputNode);
	}

	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/name"), K__S_IREAD, k_mdev(0, 0), padNode, B("BoxedWine gamepad"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/phys"), K__S_IREAD, k_mdev(0, 0), padNode, B("boxedwine/input0"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/uniq"), K__S_IREAD, k_mdev(0, 0), padNode, B(""));
	Fs::addDynamicLinkFile(B("/sys/devices/virtual/input/input5/subsystem"), k_mdev(0, 0), padNode, false, B("../../../../class/input"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/uevent"), K__S_IREAD, k_mdev(0, 0), padNode, B("PRODUCT=3/45e/28e/110\nNAME=\"BoxedWine gamepad\"\nPHYS=\"boxedwine/input0\"\nEV=b\nKEY=7fff0000 0 0 0 0 0 0 0 0\nABS=3003f\nMSC=0\nLED=0"));

	std::shared_ptr<FsNode> idNode = padNode->getChildByName(B("id"));
	if (!idNode) {
		idNode = Fs::addFileNode(B("/sys/devices/virtual/input/input5/id"), B(""), B(""), true, padNode);
	}
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/id/bustype"), K__S_IREAD, k_mdev(0, 0), idNode, B("0003"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/id/vendor"), K__S_IREAD, k_mdev(0, 0), idNode, B("045e"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/id/product"), K__S_IREAD, k_mdev(0, 0), idNode, B("028e"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/id/version"), K__S_IREAD, k_mdev(0, 0), idNode, B("0110"));

	std::shared_ptr<FsNode> capNode = padNode->getChildByName(B("capabilities"));
	if (!capNode) {
		capNode = Fs::addFileNode(B("/sys/devices/virtual/input/input5/capabilities"), B(""), B(""), true, padNode);
	}
	// EV_SYN|EV_KEY|EV_ABS = 0xb; buttons 0x130..0x13e land in word 9 as
	// 0x7fff0000 (words print highest-first); axes X,Y,Z,RX,RY,RZ,HAT0X,
	// HAT0Y = bits 0..5,16,17 = 0x3003f.
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/capabilities/ev"), K__S_IREAD, k_mdev(0, 0), capNode, B("b"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/capabilities/key"), K__S_IREAD, k_mdev(0, 0), capNode, B("0 0 0 0 0 0 0 0 0 7fff0000"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/capabilities/abs"), K__S_IREAD, k_mdev(0, 0), capNode, B("3003f"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/capabilities/rel"), K__S_IREAD, k_mdev(0, 0), capNode, B("0"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/capabilities/msc"), K__S_IREAD, k_mdev(0, 0), capNode, B("0"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/capabilities/led"), K__S_IREAD, k_mdev(0, 0), capNode, B("0"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/capabilities/snd"), K__S_IREAD, k_mdev(0, 0), capNode, B("0"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/capabilities/ff"), K__S_IREAD, k_mdev(0, 0), capNode, B("0"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/capabilities/sw"), K__S_IREAD, k_mdev(0, 0), capNode, B("0"));

	std::shared_ptr<FsNode> eventNode = padNode->getChildByName(B("event5"));
	if (!eventNode) {
		eventNode = Fs::addFileNode(B("/sys/devices/virtual/input/input5/event5"), B(""), B(""), true, padNode);
	}
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/event5/dev"), K__S_IREAD, k_mdev(0, 0), eventNode, B("13:69"));
	Fs::addVirtualFile(B("/sys/devices/virtual/input/input5/event5/uevent"), K__S_IREAD, k_mdev(0, 0), eventNode, B("MAJOR=13\nMINOR=69\nDEVNAME=input/event5"));
	Fs::addDynamicLinkFile(B("/sys/devices/virtual/input/input5/event5/device"), k_mdev(0, 0), eventNode, false, B(".."));
	Fs::addDynamicLinkFile(B("/sys/devices/virtual/input/input5/event5/subsystem"), k_mdev(0, 0), eventNode, false, B("../../../../../class/input"));

	// libudev reports a device's type classification (and its
	// devnode) only from the udev database entry the daemon wrote
	// after running its builtins; Wine's bus classifies input
	// devices purely by those properties, and the guest has no
	// daemon. Synthesise the entry a real system holds for this
	// device (char 13:69): the node line, plus the input_id
	// builtin's verdict for the kernel device's declared buttons
	// and axes -- an input device, and specifically a joystick.
	std::shared_ptr<FsNode> runNode = Fs::getNodeFromLocalPath(B(""), B("/run"), true);
	if (!runNode) {
		runNode = Fs::addFileNode(B("/run"), B(""), B(""), true, rootNode);
	}
	std::shared_ptr<FsNode> udevNode = runNode->getChildByName(B("udev"));
	if (!udevNode) {
		udevNode = Fs::addFileNode(B("/run/udev"), B(""), B(""), true, runNode);
	}
	std::shared_ptr<FsNode> udevDataNode = udevNode->getChildByName(B("data"));
	if (!udevDataNode) {
		udevDataNode = Fs::addFileNode(B("/run/udev/data"), B(""), B(""), true, udevNode);
	}
	Fs::addVirtualFile(B("/run/udev/data/c13:69"), K__S_IREAD, k_mdev(0, 0), udevDataNode, B("N:input/event5\nE:ID_INPUT=1\nE:ID_INPUT_JOYSTICK=1\n"));
}
