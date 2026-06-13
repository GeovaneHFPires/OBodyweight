Scriptname SKI_ConfigBase extends Quest Hidden

; Fill modes
int Property TOP_TO_BOTTOM = 0 AutoReadOnly
int Property LEFT_TO_RIGHT = 1 AutoReadOnly

; Option flags
int Property OPTION_FLAG_NONE     = 0 AutoReadOnly
int Property OPTION_FLAG_DISABLED = 1 AutoReadOnly
int Property OPTION_FLAG_HIDDEN   = 2 AutoReadOnly

; Mod name shown in MCM list — must be set in OnConfigInit()
string Property ModName auto

; Page list — set in OnConfigInit() for multi-page MCMs
string[] Property Pages auto

; Called once when the menu is first registered
Event OnConfigInit()
EndEvent

; Called when a page tab is selected
Event OnPageReset(string page)
EndEvent

; Called when cursor moves over an option
Event OnOptionHighlight(int option)
EndEvent

; Called when a toggle or text-button option is activated
Event OnOptionSelect(int option)
EndEvent

; Called to populate the slider dialog
Event OnOptionSliderOpen(int option)
EndEvent

; Called when the slider dialog is accepted
Event OnOptionSliderAccept(int option, float value)
EndEvent

; Called to populate the menu/dropdown dialog
Event OnOptionMenuOpen(int option)
EndEvent

; Called when a menu item is chosen
Event OnOptionMenuAccept(int option, int index)
EndEvent

; Called when a key-map option changes
Event OnOptionKeyMapChange(int option, int keyCode, string conflictControl, string conflictMod)
EndEvent

; Called when a color option dialog is accepted
Event OnOptionColorAccept(int option, int color)
EndEvent

; Called when an input option is accepted
Event OnOptionInputAccept(int option, string text)
EndEvent

; ── Adding options ──────────────────────────────────────────────────────────
int Function AddHeaderOption(string text, int flags = 0) native
int Function AddTextOption(string text, string value, int flags = 0) native
int Function AddToggleOption(string text, bool value, int flags = 0) native
int Function AddSliderOption(string text, float value, string formatString = "{0}", int flags = 0) native
int Function AddMenuOption(string text, string value, int flags = 0) native
int Function AddColorOption(string text, int color, int flags = 0) native
int Function AddKeyMapOption(string text, int keyCode, int flags = 0) native
int Function AddInputOption(string text, string value, int flags = 0) native
Function AddEmptyOption() native

; ── Cursor control ──────────────────────────────────────────────────────────
Function SetCursorPosition(int position) native
Function SetCursorFillMode(int fillMode) native

; ── Updating option values in-place ──────────────────────────────────────────
Function SetOptionFlags(int option, int flags, bool noUpdate = false) native
Function SetTextOptionValue(int option, string value, bool noUpdate = false) native
Function SetToggleOptionValue(int option, bool value, bool noUpdate = false) native
Function SetSliderOptionValue(int option, float value, string formatString = "{0}", bool noUpdate = false) native
Function SetMenuOptionValue(int option, string value, bool noUpdate = false) native
Function SetColorOptionValue(int option, int color, bool noUpdate = false) native
Function SetKeyMapOptionValue(int option, int keyCode, bool noUpdate = false) native
Function SetInputOptionValue(int option, string value, bool noUpdate = false) native

; ── Slider dialog setup ───────────────────────────────────────────────────────
Function SetSliderDialogStartValue(float value) native
Function SetSliderDialogDefaultValue(float value) native
Function SetSliderDialogRange(float minValue, float maxValue) native
Function SetSliderDialogInterval(float interval) native

; ── Menu/dropdown dialog setup ───────────────────────────────────────────────
Function SetMenuDialogStartIndex(int index) native
Function SetMenuDialogDefaultIndex(int index) native
Function SetMenuDialogOptions(string[] options) native

; ── Input dialog setup ───────────────────────────────────────────────────────
Function SetInputDialogStartText(string text) native

; ── Color dialog setup ───────────────────────────────────────────────────────
Function SetColorDialogStartColor(int color) native
Function SetColorDialogDefaultColor(int color) native

; ── Tooltip (call inside OnOptionHighlight) ───────────────────────────────────
Function SetInfoText(string text) native

; ── Misc ─────────────────────────────────────────────────────────────────────
Function ForcePageReset() native
bool Function ShowMessage(string message, bool withCancel = true, string acceptLabel = "$Accept", string cancelLabel = "$Cancel") native
