/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * This file must be UTF-8, Çe fichier doit être encodé en UTF8          *
 * Copyright (C) 2015    Stefan Sundin                                   *
 * This program is free software: you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation, either version 3 or later.              *
 * Modified By Raymond Gillibert in 2020                                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef ALTSNAP_STRINGS_H
#define ALTSNAP_STRINGS_H

#include <stddef.h>
#include <windows.h>

#include <string>
#include <vector>

#include "hooks/hooks.h"
#include "resource.h"

namespace altsnap::app {

struct langinfoitem {
  std::wstring code;  // Short code such as en-US, fr-FR.
  std::wstring lang_english;
  std::wstring lang;  // Name in the language itself.
  std::wstring author;
  std::wstring fn;  // Full path of the .ini; empty for built-in.
};

#define LANGUAGE_MAP                                                             \
  LNGVALUE(Code, L"en-US", NULL)                                                 \
  LNGVALUE(LangEnglish, L"English", NULL)                                        \
  LNGVALUE(Lang, L"English", NULL)                                               \
  LNGVALUE(Author, L"Stefan Sundin, RamonUnch", NULL)                            \
  /* Menu */                                                                     \
  LNGVALUE(MenuEnable, L"&Enable", NULL)                                         \
  LNGVALUE(MenuDisable, L"&Disable", NULL)                                       \
  LNGVALUE(MenuHideTray, L"&Hide tray", NULL)                                    \
  LNGVALUE(MenuConfigure, L"&Configure...", NULL)                                \
  LNGVALUE(MenuAbout, L"&About", NULL)                                           \
  LNGVALUE(MenuOpenIniFile, L"&Open ini file", NULL)                             \
  LNGVALUE(MenuSaveZones, L"&Save test windows as snap layout", NULL)            \
  LNGVALUE(MenuEmptyZone, L"(empty)", NULL)                                      \
  LNGVALUE(MenuSnapLayout, L"Snap layout &", NULL)                               \
  LNGVALUE(MenuEditLayout, L"Edi&t snap layout", NULL)                           \
  LNGVALUE(MenuCloseAllZones, L"C&lose all test windows", NULL)                  \
  LNGVALUE(MenuExit, L"E&xit", NULL)                                             \
  /* Config Title and Tabs */                                                    \
  LNGVALUE(ConfigTitle, APP_NAME L" Configuration", NULL)                        \
  LNGVALUE(ConfigTabGeneral, L"General", NULL)                                   \
  LNGVALUE(ConfigTabMouse, L"Mouse", NULL)                                       \
  LNGVALUE(ConfigTabKeyboard, L"Keyboard", NULL)                                 \
  LNGVALUE(ConfigTabBlacklist, L"Blacklist", NULL)                               \
  LNGVALUE(ConfigTabAdvanced, L"Advanced", NULL)                                 \
  LNGVALUE(ConfigTabAbout, L"About", NULL)                                       \
  /* General Box */                                                              \
  LNGVALUE(GeneralBox, L"General settings", NULL)                                \
  LNGVALUE(GeneralAutoFocus,                                                     \
           L"&Focus windows when dragging.\nYou can also press Ctrl to "         \
           "focus windows.",                                                     \
           NULL)                                                                 \
  LNGVALUE(GeneralAero, L"Mimi&c Aero Snap", NULL)                               \
  LNGVALUE(GeneralSmartAero, L"Smart Aero Sna&p dimensions", NULL)               \
  LNGVALUE(GeneralSmarterAero, L"Smarter Aer&o Snap dimensions", NULL)           \
  LNGVALUE(GeneralStickyResize, L"Resi&ze other snapped windows with Shift",     \
           NULL)                                                                 \
  LNGVALUE(GeneralInactiveScroll, L"&Scroll inactive windows", NULL)             \
  LNGVALUE(GeneralMDI, L"&MDI support", NULL)                                    \
  LNGVALUE(GeneralAutoSnap, L"S&nap window edges to:", NULL)                     \
  LNGVALUE(GeneralAutoSnap0, L"Disabled", NULL)                                  \
  LNGVALUE(GeneralAutoSnap1, L"To screen borders", NULL)                         \
  LNGVALUE(GeneralAutoSnap2, L"+ outside of windows", NULL)                      \
  LNGVALUE(GeneralAutoSnap3, L"+ inside of windows", NULL)                       \
  LNGVALUE(GeneralLanguage, L"&Language:", NULL)                                 \
  LNGVALUE(GeneralFullWin, L"&Drag full windows", NULL)                          \
  LNGVALUE(GeneralUseZones,                                                      \
           L"Snap to Layo&ut with Shift (configure with tray menu)",             \
           L"This option will mimick FancyZones\r\nOpen TestWindows to "         \
           "set up the zones\n Look at the tray icon menu")                      \
  LNGVALUE(GeneralPiercingClick,                                                 \
           L"Avoi&d blocking Alt+Click (disables AltSnap double-clicks)",        \
           NULL)                                                                 \
  LNGVALUE(GeneralResizeAll, L"&Resize all windows", NULL)                       \
  LNGVALUE(GeneralResizeCenter, L"Center resize mode", NULL)                     \
  LNGVALUE(GeneralResizeCenterNorm, L"All d&irections", NULL)                    \
  LNGVALUE(GeneralResizeCenterBr, L"&Bottom right", NULL)                        \
  LNGVALUE(GeneralResizeCenterMove, L"Mo&ve", NULL)                              \
  LNGVALUE(GeneralResizeCenterClose, L"Clos&est side", NULL)                     \
  LNGVALUE(GeneralAutostartBox, L"Autostart", NULL)                              \
  LNGVALUE(GeneralAutostart, L"S&tart " APP_NAME L" when logging on",            \
           L"KHCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run")           \
  LNGVALUE(GeneralAutostartHide, L"&Hide tray", NULL)                            \
  LNGVALUE(GeneralAutostartElevate, L"&Elevate to administrator privileges",     \
           NULL)                                                                 \
  LNGVALUE(GeneralAutostartElevateTip,                                           \
           L"Note that a UAC prompt will appear every time you log in, "         \
           "unless you disable UAC completely or use the Task "                  \
           "Scheduler.\nTo setup a Scheduled task for this purpose, you "        \
           "can use the sch_On.bat batch files in Altsnap's folder.",            \
           NULL)                                                                 \
  LNGVALUE(GeneralElevate, L"E&levate", NULL)                                    \
  LNGVALUE(GeneralElevated, L"Elevated", NULL)                                   \
  LNGVALUE(GeneralElevationAborted, L"Elevation aborted.", NULL)                 \
  /* Keyboard & Mouse tabs */                                                    \
  LNGVALUE(InputMouseBox, L"Mouse actions", NULL)                                \
  LNGVALUE(InputMouseBtAc1, L"&1. Primary", L"Action performed with Hotkey")     \
  LNGVALUE(InputMouseBtAc2, L"&2. Alternate",                                    \
           L"Action performed with Hotkey + Modifier key (see Keyboard tab)")    \
  LNGVALUE(InputMouseINTTB, L"&Title bar",                                       \
           L"Action performed when clicking on the title bar")                   \
  LNGVALUE(InputMouseWhileM, L"Whil&e moving", NULL)                             \
  LNGVALUE(InputMouseWhileR, L"While resi&zing", NULL)                           \
  LNGVALUE(InputMouseLMB, L"Left mouse &button:", NULL)                          \
  LNGVALUE(InputMouseMMB, L"&Middle mouse button:", NULL)                        \
  LNGVALUE(InputMouseRMB, L"Ri&ght mouse button:", NULL)                         \
  LNGVALUE(InputMouseMB4, L"Mouse button &4:", NULL)                             \
  LNGVALUE(InputMouseMB5, L"Mouse button &5:", NULL)                             \
  LNGVALUE(InputMouseScroll, L"&Scroll wheel:", NULL)                            \
  LNGVALUE(InputMouseHScroll, L"Scroll wheel (&horizontal):", NULL)              \
  LNGVALUE(InputMouseMoveUp, L"Long &drag-free Move:", NULL)                     \
  LNGVALUE(InputMouseResizeUp, L"Long drag-&free Resize:", NULL)                 \
  LNGVALUE(InputMouseTTBActionBox,                                               \
           L"Use specific actions when clicking the Title bar", NULL)            \
  LNGVALUE(InputMouseTTBActionNA, L"Without hot&key", NULL)                      \
  LNGVALUE(InputMouseTTBActionWA, L"&With hotkey", NULL)                         \
  LNGVALUE(InputMouseMMBHC, L"M&iddle mouse button", NULL)                       \
  LNGVALUE(InputMouseMB4HC, L"M&ouse button 4", NULL)                            \
  LNGVALUE(InputMouseMB5HC, L"Mo&use button 5", NULL)                            \
  LNGVALUE(InputMouseLongClickMove, L"Mo&ve windows with a long left-click",     \
           NULL)                                                                 \
  LNGVALUE(InputScrollLockState,                                                 \
           L"Suspend/Resume AltSnap based on &Scroll lock state", NULL)          \
  LNGVALUE(InputUniKeyHoldMenu,                                                  \
           L"Pop&up an extended character menu when holding an "                 \
           "alphabetic key down",                                                \
           NULL)                                                                 \
  LNGVALUE(InputKeyCombo, L"Use two keys &combo to activate", NULL)              \
  LNGVALUE(InputGrabWithAlt, L"&Action without click:", NULL)                    \
  LNGVALUE(InputGrabWithAltB, L"Acti&on without click (alt):", NULL)             \
                                                                                 \
  LNGVALUE(InputActionMove, L"Move window", NULL)                                \
  LNGVALUE(InputActionResize, L"Resize window", NULL)                            \
  LNGVALUE(InputActionRestore, L"Restore window", NULL)                          \
  LNGVALUE(InputActionClose, L"&Close window", NULL)                             \
  LNGVALUE(InputActionKill, L"&Kill program", NULL)                              \
  LNGVALUE(InputActionPause, L"Pause program", NULL)                             \
  LNGVALUE(InputActionResume, L"Resume program", NULL)                           \
  LNGVALUE(InputActionASOnOff, L"S&uspend/Resume AltSnap", NULL)                 \
  LNGVALUE(InputActionMoveOnOff, L"Movement dis&abled", NULL)                    \
  LNGVALUE(InputActionMinimize, L"Mi&nimize window", NULL)                       \
  LNGVALUE(InputActionMaximize, L"Ma&ximize window", NULL)                       \
  LNGVALUE(InputActionLower, L"&Lower window", NULL)                             \
  LNGVALUE(InputActionFocus, L"Focus window", NULL)                              \
  LNGVALUE(InputActionNStacked, L"Next stacked window", NULL)                    \
  LNGVALUE(InputActionNStacked2, L"Next laser stacked window", NULL)             \
  LNGVALUE(InputActionPStacked, L"Previous stacked window", NULL)                \
  LNGVALUE(InputActionPStacked2, L"Previous laser stacked window", NULL)         \
  LNGVALUE(InputActionNPStacked, L"Next/Prev stacked window", NULL)              \
  LNGVALUE(InputActionNPStacked2, L"Next/Prev laser stacked window", NULL)       \
  LNGVALUE(InputActionStackList, L"Stacked windows list", NULL)                  \
  LNGVALUE(InputActionStackList2, L"Laser stacked windows list", NULL)           \
  LNGVALUE(InputActionAltTabList, L"Windows list", NULL)                         \
  LNGVALUE(InputActionAltTabFullList, L"All windows list", NULL)                 \
  LNGVALUE(InputActionMZone, L"Move to zone", NULL)                              \
  LNGVALUE(InputActionXZone, L"Extend to the zone", NULL)                        \
  LNGVALUE(InputActionStep, L"Step", NULL)                                       \
  LNGVALUE(InputActionRoll, L"&Roll/Unroll window", NULL)                        \
  LNGVALUE(InputActionAlwaysOnTop, L"Toggle always on &top", NULL)               \
  LNGVALUE(InputActionBorderless, L"Toggle &borderless", NULL)                   \
  LNGVALUE(InputActionCenter, L"C&enter on desktop", NULL)                       \
  LNGVALUE(InputActionCenter2, L"Center on &physical monitor", NULL)             \
  LNGVALUE(InputActionOriClick, L"Send ori&ginal click", NULL)                   \
  LNGVALUE(InputActionNothing, L"Nothing", NULL)                                 \
  LNGVALUE(InputActionAltTab, L"Alt+Tab", NULL)                                  \
  LNGVALUE(InputActionVolume, L"Volume", NULL)                                   \
  LNGVALUE(InputActionMute, L"Mute &sounds", NULL)                               \
  LNGVALUE(InputActionMenu, L"Action menu", NULL)                                \
  LNGVALUE(InputActionMaximizeHV, L"Maximize &Vertically", NULL)                 \
  LNGVALUE(InputActionSideSnap, L"&Snap to monitor side/corner", NULL)           \
  LNGVALUE(InputActionExtendSnap, L"Extend to monitor side/corner", NULL)        \
  LNGVALUE(InputActionExtendTNEdge, L"Extend to next edge", NULL)                \
  LNGVALUE(InputActionMoveTNEdge, L"Move to next edge", NULL)                    \
  LNGVALUE(InputActionMinAllOther, L"Minimize &other windows", NULL)             \
  LNGVALUE(InputActionTransparency, L"Transparency", NULL)                       \
  LNGVALUE(InputActionZoom, L"Zoom window", NULL)                                \
  LNGVALUE(InputActionZoom2, L"Zoom window 2", NULL)                             \
  LNGVALUE(InputActionHScroll, L"Horizontal scroll", NULL)                       \
  LNGVALUE(InputActionNPLayout, L"Next/Prev Snap Layout", NULL)                  \
  LNGVALUE(InputActionNLayout, L"Next Snap Layout", NULL)                        \
  LNGVALUE(InputActionPLayout, L"Prev Snap Layout", NULL)                        \
                                                                                 \
  LNGVALUE(InputHotkeysBox, L"Hotkeys", NULL)                                    \
  LNGVALUE(InputHotkeysModKey, L"Modifier key for al&ternate action:", NULL)     \
  LNGVALUE(InputHotclicksBox, L"Hotclick (activate with a click)", NULL)         \
  LNGVALUE(InputHotclicksMore,                                                   \
           L"A checked button can be combined with an action but it will "       \
           "always be blocked in this case.",                                    \
           NULL)                                                                 \
  LNGVALUE(InputHotkeysAlt, L"Alt", NULL)                                        \
  LNGVALUE(InputHotkeysWinkey, L"Winkey", NULL)                                  \
  LNGVALUE(InputHotkeysCtrl, L"Ctrl", NULL)                                      \
  LNGVALUE(InputHotkeysShift, L"Shift", NULL)                                    \
  LNGVALUE(InputHotkeysShortcuts, L"S&hortcut for action:", NULL)                \
  LNGVALUE(InputHotkeysShortcutsPick, L"Pick &keys", NULL)                       \
  LNGVALUE(InputHotkeysShortcutsClear, L"Clear ke&ys", NULL)                     \
  LNGVALUE(InputHotkeysShortcutsSet, L"Sa&ve", NULL)                             \
  LNGVALUE(InputHotkeysUsePtWindow, L"Apply to the &pointed window", NULL)       \
  LNGVALUE(InputHotkeysLeftAlt, L"L&eft Alt", NULL)                              \
  LNGVALUE(InputHotkeysRightAlt, L"&Right Alt", NULL)                            \
  LNGVALUE(InputHotkeysLeftWinkey, L"Left &Winkey", NULL)                        \
  LNGVALUE(InputHotkeysRightWinkey, L"Right W&inkey", NULL)                      \
  LNGVALUE(InputHotkeysLeftCtrl, L"&Left Ctrl", NULL)                            \
  LNGVALUE(InputHotkeysRightCtrl, L"Ri&ght Ctrl", NULL)                          \
  LNGVALUE(InputHotkeysLeftShift, L"Left Shift", NULL)                           \
  LNGVALUE(InputHotkeysRightShift, L"Right Shift", NULL)                         \
  LNGVALUE(InputHotkeysMore,                                                     \
           L"You can add any key by editing the ini file.\nUse the shift "       \
           "key to snap windows to each other.",                                 \
           NULL)                                                                 \
  /* Blacklits Tab*/                                                             \
  LNGVALUE(BlacklistBox, L"Blacklist settings", NULL)                            \
  LNGVALUE(                                                                      \
      BlacklistFormat,                                                           \
      L"exename.exe\r\ntitle|class\r\n*|class\r\nexename.exe:title|class",       \
      NULL)                                                                      \
  LNGVALUE(BlacklistProcessBlacklist, L"&Process blacklist:",                    \
           L"Globally applies ie:\r\n*|Notepad, Photoshop.exe, "                 \
           "explorer.exe*|#32770")                                               \
  LNGVALUE(BlacklistBlacklist, L"&Windows blacklist:", NULL)                     \
  LNGVALUE(BlacklistScrolllist, L"Windows that should ignore &scroll action:",   \
           NULL)                                                                 \
  LNGVALUE(BlacklistMDIs, L"&MDIs not to be treated as such:", NULL)             \
  LNGVALUE(BlacklistPause, L"Processes not to be pa&used or killed:", NULL)      \
  LNGVALUE(BlacklistFindWindowBox, L"Identify window", NULL)                     \
  /* Advanced Tab */                                                             \
  LNGVALUE(AdvancedMetricsBox, L"Metrics", NULL)                                 \
  LNGVALUE(AdvancedCenterFraction, L"&Center/Sides fraction (%):", NULL)         \
  LNGVALUE(AdvancedAeroHoffset, L"Aero offset(%) &Horizontal:", NULL)            \
  LNGVALUE(AdvancedAeroVoffset, L"&Vertical:", NULL)                             \
  LNGVALUE(AdvancedSnapThreshold, L"&Snap Threshold (pixels):", NULL)            \
  LNGVALUE(AdvancedAeroThreshold, L"A&ero Threshold (pixels):", NULL)            \
  LNGVALUE(AdvancedSnapGap, L"Snapping &gap (pixels):", NULL)                    \
  LNGVALUE(AdvancedAeroSpeed, L"Ma&x snapping speed (pixels):", NULL)            \
  LNGVALUE(AdvancedTestWindow, L"Test &Window", NULL)                            \
  LNGVALUE(AdvancedMoveTrans, L"Opacit&y when moving:", NULL)                    \
  LNGVALUE(AdvancedBehaviorBox, L"Behavior", NULL)                               \
  LNGVALUE(AdvancedMultipleInstances, L"Allow multiple &instances of AltSnap",   \
           NULL)                                                                 \
  LNGVALUE(AdvancedAutoRemaximize,                                               \
           L"Automatically &remaximize windows when changing monitor", NULL)     \
  LNGVALUE(AdvancedAeroTopMaximizes, L"&Maximize windows snapped at top",        \
           NULL)                                                                 \
  LNGVALUE(AdvancedAeroDBClickShift,                                             \
           L"Invert shift &behavior for double-click aero snapping", NULL)       \
  LNGVALUE(AdvancedMaxWithLClick,                                                \
           L"&Toggle maximize state with the resize button while moving",        \
           NULL)                                                                 \
  LNGVALUE(AdvancedRestoreOnClick,                                               \
           L"Rest&ore window with single click like original AltDrag", NULL)     \
  LNGVALUE(AdvancedFullScreen, L"Enable on &full screen windows", NULL)          \
  LNGVALUE(AdvancedBLMaximized, L"&Disable AltSnap on Maximized windows",        \
           NULL)                                                                 \
  LNGVALUE(AdvancedFancyZone, L"Restore Fancy&Zones snapped windows", NULL)      \
  LNGVALUE(AdvancedNoRestore, L"Never restore AltSna&pped windows", NULL)        \
  LNGVALUE(AdvancedTopmostIndicator,                                             \
           L"Show an i&ndicator on always on top windows", NULL)                 \
  /* About Tab */                                                                \
  LNGVALUE(AboutBox, L"About " APP_NAME, NULL)                                   \
  LNGVALUE(AboutVersion, L"Version", NULL)                                       \
  LNGVALUE(AboutAuthor, L"Created by Stefan Sundin", NULL)                       \
  LNGVALUE(AboutAuthor2, L"Maintained by Raymond Gillibert", NULL)               \
  LNGVALUE(                                                                    \
      AboutLicense,                                                            \
      APP_NAME L" is free and open source software!\nFeel free to "            \
               L"redistribute!",                                               \
      NULL) \
  LNGVALUE(AboutTranslationCredit, L"Translation credit", NULL)                  \
  /* Misc */                                                                     \
  LNGVALUE(MiscUnhookError,                                                      \
           L"There was an error disabling AltSnap. This was most likely "        \
           "caused by Windows having already disabled AltSnap's "                \
           "hooks.\n\nIf this is the first time this has happened, you "         \
           "can safely ignore it and continue using AltSnap.\n\nIf this "        \
           "is happening repeatedly, you can read on the website how to "        \
           "prevent this from happening again (look for 'AltDrag "               \
           "mysteriously stops working' in the documentation).",                 \
           NULL)                                                                 \
  LNGVALUE(MiscZoneConfirmation,                                                 \
           L"Erase old snap layout and save current Test Windows "               \
           "positions as the new snap layout?",                                  \
           NULL)                                                                 \
  LNGVALUE(MiscZoneTestWinHelp,                                                  \
           L"To setup Snap layout:\n1) Open several of those Test "              \
           "Windows\n2) Dispose them as you please\n3) Hit the *&Save "          \
           "test windows as snap layout* option in the tray menu",               \
           NULL)                                                                 \
  /* Basic Verbs */                                                              \
  LNGVALUE(WayLeft, L"Left", NULL)                                               \
  LNGVALUE(WayUp, L"Up", NULL)                                                   \
  LNGVALUE(WayRight, L"Right", NULL)                                             \
  LNGVALUE(WayDown, L"Down", NULL)                                               \
  LNGVALUE(KwdSetTo, L"Set to", NULL)                                            \
  LNGVALUE(KwdToggle, L"Toggle", NULL)                                           \
  LNGVALUE(TtlAction, L"&Action", NULL)                                          \
  LNGVALUE(TtlAdvancedAction, L"Advanced Action", NULL)                          \
                                                                                 \
  /* Extended character list  for each virtual key */                            \
  LNGVALUE(a, L"àáâäæãåª%āăąǎǟǡǣǻǽȁȃȧ|Ȧḁ%ⱥ|Ⱥɐ|Ɐɑ|Ɑɒ|Ɒⲁ|Ⲁⓐ", NULL)                \
  LNGVALUE(b, L"%ƀɓƃƅɃ%ɓḃḅḇⓑ", NULL)                                             \
  LNGVALUE(c, L"ç¢©%ćĉċčƈḉȼ|Ȼɕⓒ", NULL)                                          \
  LNGVALUE(d, L"ð%ďđɖɗƌƍḋḍḏḑḓǆǅǳǲȡȸⓓ", NULL)                                     \
  LNGVALUE(e, L"èéêë€%ēĕėęěǝəɛȅȇḕḗḙḛȩ|Ȩḝɇ|Ɇⱸⓔ", NULL)                            \
  LNGVALUE(f, L"ƒ%ḟɸⱷⓕ%♩♪♮♭♯♬♫", NULL)                                           \
  LNGVALUE(g, L"%ǵǧḡɠɣǥⓖ", NULL)                                                 \
  LNGVALUE(h, L"%ĥħƕǶḣḥḧḩḫȟ|Ȟⱨ|Ⱨⱶ|Ⱶẖⓗ", NULL)                                    \
  LNGVALUE(i, L"ìíîï%ĩīĭǐȉȋįİıĳɩɨḭḯⓘ", NULL)                                     \
  LNGVALUE(j, L"%ĵǰȷɉ|Ɉⓙ", NULL)                                                 \
  LNGVALUE(k, L"%ķĸƙǩḱḳⱪ|Ⱪꝁ|Ꝁʞ|Ʞⓚ", NULL)                                        \
  LNGVALUE(l, L"£%ĺļľŀłƛǉǈȴƚ|Ƚⱡ|Ⱡɫ|Ɫḷḹḻḽⓛ", NULL)                                \
  LNGVALUE(m, L"µ%ḿṁṃɱ|Ɱɯⓜ", NULL)                                               \
  LNGVALUE(n, L"ñ%ńņňŉŋɲƞ|Ƞǌǋǹȵ%ṅṇṉṋⓝ", NULL)                                    \
  LNGVALUE(o, L"òóôöœõø°%ōŏő%ɔɵơƣǒǫǭǿȍȏȣ|Ȣȫ|Ȫȭ|Ȭȯ|Ȯȱ|Ȱṍṏṑṓ%ⱺⓞ", NULL)            \
  LNGVALUE(p, L"¶þ·•%ƥᵽ|Ᵽṕṗⓟ", NULL)                                             \
  LNGVALUE(q, L"¿¤‰‘’“”„…–—«»‹›%ȹɋ|Ɋⓠ", NULL)                                    \
  LNGVALUE(r, L"®%ŕŗřƦȑȓṙṛṝṟɍ|Ɍɽ|Ɽⱹⓡ", NULL)                                     \
  LNGVALUE(s, L"šß§%śŝşſ%ƨʃƪș|Șȿ|Ȿ%ṡṣṥṧṩⓢ", NULL)                                \
  LNGVALUE(t, L"†‡™%ţťŧƫƭʈț|Țȶⱦ|Ⱦ%ṫṭṯṱẗⓣ", NULL)                                 \
  LNGVALUE(u, L"ùúûü%ũūůŭűų%ưʊǔǖǘǚǜȕȗʉ|Ʉ%ṳṵṷṹṻⓤ", NULL)                          \
  LNGVALUE(v, L"%ʋɅⱱⱴ%ṽṿⓥ", NULL)                                                \
  LNGVALUE(w, L"%ẁẃŵẅⱳ|Ⱳ%ẇẉⓦ", NULL)                                             \
  LNGVALUE(x, L"±×÷¬%ẋẍⓧ", NULL)                                                 \
  LNGVALUE(y, L"ýÿ¥%ŷẏȳ|Ȳƴɏ|Ɏⓨ", NULL)                                           \
  LNGVALUE(z, L"ž%źẑżẓẕ%ƶʒƹƺǯȥ|Ȥɀ|Ɀⱬ|Ⱬⓩ", NULL)

// One string and its tooltip per LNGVALUE() entry of LANGUAGE_MAP.
#define LNGVALUE(x, y, z) const wchar_t *x, *x##_T_T_;
struct strings {
  LANGUAGE_MAP

  // String number `index` (see L10nId); a tooltip follows each string.
  [[nodiscard]] const wchar_t* Get(short index) const;
};
#undef LNGVALUE

// Index of every string (and of its tooltip, named X_T_T_) in struct strings.
// Upstream: L10NIDX(X) macro, now L10nId::X.
#define LNGVALUE(x, y, z) x, x##_T_T_,
namespace L10nId {
enum : short { LANGUAGE_MAP };
}  // namespace L10nId
#undef LNGVALUE

// Members of struct strings in L10nId order.
#define LNGVALUE(x, y, z) &strings::x, &strings::x##_T_T_,
inline constexpr const wchar_t* strings::*kL10nMembers[] = {LANGUAGE_MAP};
#undef LNGVALUE

inline const wchar_t* strings::Get(short index) const {
  return this->*kL10nMembers[index];
}

extern const char* const l10n_inimapping[];  // ini entry names
extern const struct strings en_US;           // Built-in English strings

// Shared with other translation units (defined in languages.cc).
void ListAllTranslations();
void UpdateLanguage();
void FreeAllLangRelated();
int GetUserLanguageCode(wchar_t txt[16]);
void LoadTranslation(const wchar_t* ini);
extern const struct strings* l10n;
extern std::vector<langinfoitem> langinfo;
size_t CopyWithoutAccelerator(wchar_t* dest, size_t destlen,
                              const wchar_t* source);

}  // namespace altsnap::app

#endif
