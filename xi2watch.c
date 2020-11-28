// c99 -Wall -Wextra -g -O0 -o xi2watch xi2watch.c -lX11 -lXi
/*
X11 and hot-plugged keyboards and multiple layouts
https://lists.debian.org/debian-user/2020/02/msg00755.html

    To: debian-user@lists.debian.org
    Subject: X11 and hot-plugged keyboards and multiple layouts
    From: Nicolas George <george@nsup.org>
    Date: Wed, 19 Feb 2020 17:54:01 +0100
    Message-id: <[?] 20200219165401.nadrfenj5sxnzjd5@phare.normalesup.org>
    Reply-to: debian-user@lists.debian.org

Hi.

A few weeks back, I asked about standard tools to handle hot-plugged
keyboards with different layouts under X11 without root privileges,
hinting that I know how to do it with non-standard tools or with root
privileges.

Here is a concentrate of what I know about the issue, for Debian testing
as of 2020-02-19.

Most of it is already documented on the web, but not all, and not all in
one place. If you find this interesting, feel free to dump it in a wiki
somewhere. And please let me know if there are inaccuracies.

The default keyboard layout on Debian is configured in
/etc/default/keyboard. This file is created and edited by the package
keyboard-configuration, whose bulk is in the debconf configure script,
not the actual contents.

/etc/default/keyboard can be queried and carelessly overwritten by a
DBus service provided by systemd-localed in the systemd package. It can
be queried by the client localectl or with a generic DBus client:

dbus-send --system --print-reply \
  --dest=org.freedesktop.locale1 /org/freedesktop/locale1 \
  org.freedesktop.DBus.Properties.Get \
  string:org.freedesktop.locale1 string:X11Layout

But X.org does not use this.

The udev rule /lib/udev/rules.d/64-xorg-xkb.rules provided by the
xserver-xorg-core package causes /etc/default/keyboard to be imported
into the environment of udev events detected to be associated with
keyboards. The environment can be overridden by a later rule, something
like:

ACTION=="add|change", \
  SUBSYSTEM=="input", KERNEL=="event[0-9]*", \
  ENV{ID_INPUT_KEY}=="?*", \
  ATTRS{idVendor}=="1997", ATTRS{idProduct}=="2433", \
  ENV{XKBLAYOUT}="fr"

udev stores the final environment in /run/udev/data/c${major}:${minor}.

When a new input device is detected, X.org applies the configuration
files. If nothing is specified, /usr/share/X11/xorg.conf.d/10-evdev.conf
from xserver-xorg-input-evdev tells to use the evdev driver but is soon
overridden by /usr/share/X11/xorg.conf.d/40-libinput.conf from
xserver-xorg-input-libinput that tells to use the libinput driver.

The configuration snippets can define several InputClass sections that
can use match criteria to selectively apply options. See INPUTCLASS in
xorg.conf(5) and the examples of the two files above.

If the configuration does not set the layout, the options from the udev
environment are used. I have not found where this fact is documented, I
suspect it is a very generic mechanism in X.org that can apply to any
options, for example touchpad fancy stuff.

All this allows to set different layouts for hot-plugged keyboards using
either snippets of configuration for the X11 server or udev rules.

All this is more or less explained there:
https://wiki.debian.org/XStrikeForce/InputHotplugGuide

It all requires root privileges. Let's see what we can do without.

Handling the layout of hot-plugged keyboards involves two X11
extensions, XKEYBOARD aka XKB and XInputExtension aka XI2.

Configuring a keyboard layout is done with XKB.

The high-level configuration of XKB involves a few settings: layout,
variant and options, as set in /etc/default/keyboard. They are mapped to
snippets of detailed configuration using the contents of
/usr/share/X11/xkb/rules/. It produce a skeleton description that looks
like:

xkb_keymap {
  xkb_keycodes { include "evdev+aliases(azerty)" };
  xkb_types { include "complete" };
  xkb_compat { include "complete" };
  xkb_geometry { include "pc(pc105)" };
  xkb_symbols { include "pc+fr+compose(menu)+terminate(ctrl_alt_bksp)" };
};

The high-level tool to set the layout is setxkbmap.

The description, either generated by rules or written by hand, is
handled by xkbcomp, a tool to convert a textual description into a
compiled description and back. It can operate with files, with standard
extension .xkb for text and .xkm for compiled, and directly with the
tables in the X11 server, only in compiled form obviously.

The current complete description can be obtained with:

xkbcomp $DISPLAY output.xkb

A description ca be loaded to the keyboard with:

xkbcomp input.xkb $DISPLAY

We can display the complete current layout with:

xkbcomp :0 - | xkbcomp - - | xkbprint - - | display -

Hot-plugged input devices are handled by the XI2 extension. The standard
tool xinput from the package with the same name can be used to control
it.

Keyboards are grouped in a shallow hierarchy, with the "Virtual core
keyboard" as the root and master and the actual keyboards under it. I
know it is possible to have several master pointers, which correspond to
several actual pointers on the screen moving independently. I have not
checked what happens if we try to create a second master keyboard.

The xinput command without arguments print the current hierarchy.

xinput can be used to change options on individual devices. That's how
we set the speed on a specific mouse for example:

xinput set-prop "Logitech USB Optical Mouse" "libinput Accel Speed" -0.4

For keyboard layouts, xinput is not smart enough. We need to use xkbcomp
or setxkbmap. xkbcomp has the -i option to specify which device alter;
setxkbmap uses the -device option. They need numeric ids, that can be
obtained from the output of xinput.

If xkbcomp or setxkbmap is called on the master keyboard (which is the
default without -i/-device), it changes all the attached devices at the
same time. Otherwise, it changes only the specified device.

With USB devices, there are frequently several XI2 devices for a single
hardware thingie: one for the mouse (not necessarily present), one for
the keyboard, one for special keys on the keyboard, etc. We can find
which one affects the layout by trial and error.

Remember: newly plugged keyboards do not inherit the layout of the
master keyboard. They get the layout from the X.org configuration or
from udev.

This is how we can set up different layouts as normal users. The next
step is to do it automatically. The X11 server generates XI2 events when
keyboards are hot-plugged. They can be observed using xinput:

xinput test-xi2 | sed -n '/^EVENT.*HierarchyChanged/,/changes:/p'
EVENT type 11 (HierarchyChanged)
    Changes happened:       [device enabled]
    <snip>
    changes:       [device enabled]

We need something that listens to these events and calls xkbcomp or
setxkbmap in reaction. xinput test-xi2 is not suitable for this use
because its output is not script-friendly and because it opens an
obnoxious window.

I have written a small tool for just that use. Its source code is at the
end of this mail. Whenever a change in the XI2 hierarchy is signaled, it
calls the command given in arguments with environment variables
describing the event:

./xi2watch env
<snip>
DEVICE=15
DEVICE_NAME=  mini keyboard Consumer Control
ENABLED=1
FLAG_MASTER_ADDED=0
FLAG_MASTER_REMOVED=0
FLAG_SLAVE_ADDED=0
FLAG_SLAVE_REMOVED=0
FLAG_SLAVE_ATTACHED=0
FLAG_SLAVE_DETACHED=0
FLAG_DEVICE_ENABLED=1
FLAG_DEVICE_DISABLED=0
USE=slave_keyboard

The command can then be a shell script that will choose the layout and
apply it.

	case ${USE}:${FLAG_DEVICE_ENABLED}:${DEVICE_NAME} in
	  slave_keyboard:1:*mini keyboard*)
	    setxkbmap -device $DEVICE fr
	    ;;
	esac

With that, we can configure the layout of keyboards automatically, as
they are plugged, without the need for root privileges. It can also be
used to set the speed of mice. I believe it should be widely available.

The best option would be to integrate that feature into xinput. If
somebody wants to package it, please feel free, and thanks. Integrating
a similar feature into configurable window managers may be another good
option.

I hope all this can help people.

Regards,

--
  Nicolas George


----8<----8<----8<----8<---- xi2watch.c ---->8---->8---->8---->8----
*/

/*
 * xi2watch - watch for XInput2 events
 * (c) 2016-2020 Nicolas George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * gcc -Wall -std=c99 -o xi2watch xi2watch.c -lX11 -lXi
 */

#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

static Display *display;

static void
connect_events(void)
{
    XIEventMask m = { 0 };

    m.deviceid = XIAllDevices;
    m.mask_len = XIMaskLen(XI_LASTEVENT);
    m.mask = calloc(1, m.mask_len);
    if (m.mask == NULL)
        abort();
    XISetMask(m.mask, XI_HierarchyChanged);
    XISelectEvents(display, DefaultRootWindow(display), &m, 1);
    free(m.mask);
}

static void
run_command(char **cmd, XIHierarchyInfo *info)
{
    XIDeviceInfo *devices;
    char idbuf[32], typebuf[32], *type;
    pid_t child;
    int nb_devices = 0, status, i;

    devices = XIQueryDevice(display, 0, &nb_devices);
    snprintf(idbuf, sizeof(idbuf), "%d", info->deviceid);
    child = fork();
    if (child < 0) {
        perror("fork");
        return;
    }
    if (child == 0) {
        setenv("DEVICE", idbuf, 1);
        for (i = 0; i < nb_devices; i++) {
            if (devices[i].deviceid == info->deviceid) {
                setenv("DEVICE_NAME", devices[i].name, 1);
                break;
            }
        }
        setenv("ENABLED", info->enabled ? "1" : "0", 1);
        #define ENV_FLAG(e, f) \
            setenv("FLAG_" e, (info->flags & f) ? "1" : "0", 1)
        ENV_FLAG("MASTER_ADDED", XIMasterAdded);
        ENV_FLAG("MASTER_REMOVED", XIMasterRemoved);
        ENV_FLAG("SLAVE_ADDED", XISlaveAdded);
        ENV_FLAG("SLAVE_REMOVED", XISlaveRemoved);
        ENV_FLAG("SLAVE_ATTACHED", XISlaveAttached);
        ENV_FLAG("SLAVE_DETACHED", XISlaveDetached);
        ENV_FLAG("DEVICE_ENABLED", XIDeviceEnabled);
        ENV_FLAG("DEVICE_DISABLED", XIDeviceDisabled);
        switch (info->use) {
            case 0: type = "none"; break;
            case XIMasterPointer: type = "master_pointer"; break;
            case XIMasterKeyboard: type = "master_keyboard"; break;
            case XISlavePointer: type = "slave_pointer"; break;
            case XISlaveKeyboard: type = "slave_keyboard"; break;
            case XIFloatingSlave: type = "floating_slave"; break;
            default:
                snprintf(typebuf, sizeof(typebuf), "unknown_%d", info->use);
                type = typebuf;
                break;
        }
        setenv("USE", type, 1);
        execvp(cmd[0], cmd);
        perror("exec");
        _exit(1);
    }
    waitpid(child, &status, 0);
    if (status != 0)
        fprintf(stderr, "Child failed\n");
    XIFreeDeviceInfo(devices);
}

static void
hierarchy_changed(XIHierarchyEvent *ev, char **cmd)
{
    int i;
    XIHierarchyInfo *info;

    for (i = 0; i < ev->num_info; i++) {
        info = &ev->info[i];
        if (info->flags != 0)
            run_command(cmd, info);
    }
}

int
main(int argc, char **argv)
{
    XEvent ev;
    int xi_major, xi_event, xi_error;

    if (argc < 2) {
        fprintf(stderr, "Usage: xi2watch command [args]\n");
        exit(1);
    }
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Unable to open display\n");
        exit(1);
    }
    if (!XQueryExtension(display, "XInputExtension", &xi_major, &xi_event, &xi_error)) {
        fprintf(stderr, "XI2 not available\n");
        exit(1);
    }
    connect_events();
    while (1) {
        XNextEvent(display, &ev);
        if (ev.type == GenericEvent &&
            ev.xcookie.extension == xi_major &&
            XGetEventData(display, &ev.xcookie)) {
            if (ev.xcookie.evtype == XI_HierarchyChanged)
                hierarchy_changed(ev.xcookie.data, argv + 1);
        }
    }
    return 0;
}
