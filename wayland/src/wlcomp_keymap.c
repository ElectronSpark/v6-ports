#include "wlcomp_keymap.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <wayland/wayland-server-protocol.h>

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

void wlcomp_keymap_send(struct wl_resource *res)
{
    /* Send a full US-QWERTY XKB keymap via anonymous memfd.
     * GTK3/libxkbcommon requires a real fd it can mmap.
     * Keycodes = PS/2 set1 scancode + 8 (evdev convention). */
    static const char us_keymap[] =
        "xkb_keymap {\n"
        "  xkb_keycodes \"ps2\" {\n"
        "    minimum = 8; maximum = 255;\n"
        "    <ESC>  = 9;\n"
        "    <AE01> = 10; <AE02> = 11; <AE03> = 12; <AE04> = 13;\n"
        "    <AE05> = 14; <AE06> = 15; <AE07> = 16; <AE08> = 17;\n"
        "    <AE09> = 18; <AE10> = 19; <AE11> = 20; <AE12> = 21;\n"
        "    <BKSP> = 22;\n"
        "    <TAB>  = 23;\n"
        "    <AD01> = 24; <AD02> = 25; <AD03> = 26; <AD04> = 27;\n"
        "    <AD05> = 28; <AD06> = 29; <AD07> = 30; <AD08> = 31;\n"
        "    <AD09> = 32; <AD10> = 33; <AD11> = 34; <AD12> = 35;\n"
        "    <RTRN> = 36;\n"
        "    <LCTL> = 37;\n"
        "    <AC01> = 38; <AC02> = 39; <AC03> = 40; <AC04> = 41;\n"
        "    <AC05> = 42; <AC06> = 43; <AC07> = 44; <AC08> = 45;\n"
        "    <AC09> = 46; <AC10> = 47; <AC11> = 48;\n"
        "    <TLDE> = 49;\n"
        "    <LFSH> = 50;\n"
        "    <BKSL> = 51;\n"
        "    <AB01> = 52; <AB02> = 53; <AB03> = 54; <AB04> = 55;\n"
        "    <AB05> = 56; <AB06> = 57; <AB07> = 58; <AB08> = 59;\n"
        "    <AB09> = 60; <AB10> = 61;\n"
        "    <RTSH> = 62;\n"
        "    <KPMU> = 63;\n"
        "    <LALT> = 64;\n"
        "    <SPCE> = 65;\n"
        "    <CAPS> = 66;\n"
        "    <FK01> = 67; <FK02> = 68; <FK03> = 69; <FK04> = 70;\n"
        "    <FK05> = 71; <FK06> = 72; <FK07> = 73; <FK08> = 74;\n"
        "    <FK09> = 75; <FK10> = 76;\n"
        "    <UP>   = 111; <DOWN> = 116; <LEFT> = 113; <RGHT> = 114;\n"
        "    <HOME> = 110; <END>  = 115; <PGUP> = 112; <PGDN> = 117;\n"
        "    <INS>  = 118; <DELE> = 119;\n"
        "  };\n"
        "  xkb_types \"basic\" {\n"
        "    type \"ONE_LEVEL\" {\n"
        "      modifiers = none;\n"
        "      map[none] = Level1;\n"
        "      level_name[Level1] = \"Any\";\n"
        "    };\n"
        "    type \"TWO_LEVEL\" {\n"
        "      modifiers = Shift;\n"
        "      map[none]  = Level1;\n"
        "      map[Shift] = Level2;\n"
        "      level_name[Level1] = \"Base\";\n"
        "      level_name[Level2] = \"Shift\";\n"
        "    };\n"
        "    type \"ALPHABETIC\" {\n"
        "      modifiers = Shift+Lock;\n"
        "      map[none]       = Level1;\n"
        "      map[Shift]      = Level2;\n"
        "      map[Lock]       = Level2;\n"
        "      map[Shift+Lock] = Level1;\n"
        "      level_name[Level1] = \"Base\";\n"
        "      level_name[Level2] = \"Caps\";\n"
        "    };\n"
        "  };\n"
        "  xkb_compatibility \"basic\" {\n"
        "    interpret Shift_L   { action = SetMods(modifiers=Shift); };\n"
        "    interpret Shift_R   { action = SetMods(modifiers=Shift); };\n"
        "    interpret Control_L { action = SetMods(modifiers=Control); };\n"
        "    interpret Alt_L     { action = SetMods(modifiers=Mod1); };\n"
        "    interpret Caps_Lock { action = LockMods(modifiers=Lock); };\n"
        "  };\n"
        "  xkb_symbols \"us\" {\n"
        "    key <ESC>  { [ Escape ] };\n"
        "    key <AE01> { [ 1, exclam      ] };\n"
        "    key <AE02> { [ 2, at           ] };\n"
        "    key <AE03> { [ 3, numbersign   ] };\n"
        "    key <AE04> { [ 4, dollar       ] };\n"
        "    key <AE05> { [ 5, percent      ] };\n"
        "    key <AE06> { [ 6, asciicircum  ] };\n"
        "    key <AE07> { [ 7, ampersand    ] };\n"
        "    key <AE08> { [ 8, asterisk     ] };\n"
        "    key <AE09> { [ 9, parenleft    ] };\n"
        "    key <AE10> { [ 0, parenright   ] };\n"
        "    key <AE11> { [ minus, underscore ] };\n"
        "    key <AE12> { [ equal, plus     ] };\n"
        "    key <BKSP> { [ BackSpace       ] };\n"
        "    key <TAB>  { [ Tab             ] };\n"
        "    key <AD01> { type[Group1] = \"ALPHABETIC\", [ q, Q ] };\n"
        "    key <AD02> { type[Group1] = \"ALPHABETIC\", [ w, W ] };\n"
        "    key <AD03> { type[Group1] = \"ALPHABETIC\", [ e, E ] };\n"
        "    key <AD04> { type[Group1] = \"ALPHABETIC\", [ r, R ] };\n"
        "    key <AD05> { type[Group1] = \"ALPHABETIC\", [ t, T ] };\n"
        "    key <AD06> { type[Group1] = \"ALPHABETIC\", [ y, Y ] };\n"
        "    key <AD07> { type[Group1] = \"ALPHABETIC\", [ u, U ] };\n"
        "    key <AD08> { type[Group1] = \"ALPHABETIC\", [ i, I ] };\n"
        "    key <AD09> { type[Group1] = \"ALPHABETIC\", [ o, O ] };\n"
        "    key <AD10> { type[Group1] = \"ALPHABETIC\", [ p, P ] };\n"
        "    key <AD11> { [ bracketleft, braceleft   ] };\n"
        "    key <AD12> { [ bracketright, braceright ] };\n"
        "    key <RTRN> { [ Return ] };\n"
        "    key <LCTL> { [ Control_L ] };\n"
        "    key <AC01> { type[Group1] = \"ALPHABETIC\", [ a, A ] };\n"
        "    key <AC02> { type[Group1] = \"ALPHABETIC\", [ s, S ] };\n"
        "    key <AC03> { type[Group1] = \"ALPHABETIC\", [ d, D ] };\n"
        "    key <AC04> { type[Group1] = \"ALPHABETIC\", [ f, F ] };\n"
        "    key <AC05> { type[Group1] = \"ALPHABETIC\", [ g, G ] };\n"
        "    key <AC06> { type[Group1] = \"ALPHABETIC\", [ h, H ] };\n"
        "    key <AC07> { type[Group1] = \"ALPHABETIC\", [ j, J ] };\n"
        "    key <AC08> { type[Group1] = \"ALPHABETIC\", [ k, K ] };\n"
        "    key <AC09> { type[Group1] = \"ALPHABETIC\", [ l, L ] };\n"
        "    key <AC10> { [ semicolon, colon    ] };\n"
        "    key <AC11> { [ apostrophe, quotedbl ] };\n"
        "    key <TLDE> { [ grave, asciitilde    ] };\n"
        "    key <LFSH> { [ Shift_L   ] };\n"
        "    key <BKSL> { [ backslash, bar ] };\n"
        "    key <AB01> { type[Group1] = \"ALPHABETIC\", [ z, Z ] };\n"
        "    key <AB02> { type[Group1] = \"ALPHABETIC\", [ x, X ] };\n"
        "    key <AB03> { type[Group1] = \"ALPHABETIC\", [ c, C ] };\n"
        "    key <AB04> { type[Group1] = \"ALPHABETIC\", [ v, V ] };\n"
        "    key <AB05> { type[Group1] = \"ALPHABETIC\", [ b, B ] };\n"
        "    key <AB06> { type[Group1] = \"ALPHABETIC\", [ n, N ] };\n"
        "    key <AB07> { type[Group1] = \"ALPHABETIC\", [ m, M ] };\n"
        "    key <AB08> { [ comma, less     ] };\n"
        "    key <AB09> { [ period, greater ] };\n"
        "    key <AB10> { [ slash, question ] };\n"
        "    key <RTSH> { [ Shift_R   ] };\n"
        "    key <LALT> { [ Alt_L     ] };\n"
        "    key <SPCE> { [ space     ] };\n"
        "    key <CAPS> { [ Caps_Lock ] };\n"
        "    key <FK01> { [ F1  ] }; key <FK02> { [ F2  ] };\n"
        "    key <FK03> { [ F3  ] }; key <FK04> { [ F4  ] };\n"
        "    key <FK05> { [ F5  ] }; key <FK06> { [ F6  ] };\n"
        "    key <FK07> { [ F7  ] }; key <FK08> { [ F8  ] };\n"
        "    key <FK09> { [ F9  ] }; key <FK10> { [ F10 ] };\n"
        "    key <UP>   { [ Up    ] }; key <DOWN> { [ Down  ] };\n"
        "    key <LEFT> { [ Left  ] }; key <RGHT> { [ Right ] };\n"
        "    key <HOME> { [ Home  ] }; key <END>  { [ End   ] };\n"
        "    key <PGUP> { [ Prior ] }; key <PGDN> { [ Next  ] };\n"
        "    key <INS>  { [ Insert ] }; key <DELE> { [ Delete ] };\n"
        "    modifier_map Shift   { <LFSH>, <RTSH> };\n"
        "    modifier_map Control { <LCTL> };\n"
        "    modifier_map Mod1    { <LALT> };\n"
        "    modifier_map Lock    { <CAPS> };\n"
        "  };\n"
        "};\n";
    size_t keymap_size = sizeof(us_keymap);  /* includes NUL */

    int km_fd = (int)syscall(SYS_memfd_create, "wlcomp-keymap", MFD_CLOEXEC);
    if (km_fd >= 0) {
        write(km_fd, us_keymap, keymap_size);
        lseek(km_fd, 0, SEEK_SET);
        wl_keyboard_send_keymap(res, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                km_fd, keymap_size);
        close(km_fd);
    } else {
        fprintf(stderr, "wlcomp: memfd keymap failed: %s\n", strerror(errno));
    }

    if (wl_resource_get_version(res) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
        wl_keyboard_send_repeat_info(res, 25, 400);
}
