Scriptname OBW_MCM extends SKI_ConfigBase

int _modeOption     = -1
int _bodyOption     = -1
int _scaleOption    = -1
int _fantasyOption  = -1
int _unusualOption  = -1
int _bUnusualOption = -1
int _athleticOption = -1
int _keyOption      = -1
int _biasOption     = -1
int _seedOption     = -1
int _reseedOption   = -1

string[] _modeLabels
string[] _bodyLabels

Event OnConfigInit()
    ModName = "OBodyNG Weight"

    _modeLabels = new string[3]
    _modeLabels[0] = "Random"
    _modeLabels[1] = "Seeded (Deterministic)"
    _modeLabels[2] = "NPC Default (disabled)"

    _bodyLabels = new string[2]
    _bodyLabels[0] = "Procedural Morphs"
    _bodyLabels[1] = "OBody Presets"
EndEvent

Event OnPlayerLoadGame()
EndEvent

Event OnPageReset(string page)
    SetCursorFillMode(TOP_TO_BOTTOM)

    AddHeaderOption("Distribution Mode")
    _modeOption = AddMenuOption("Weight mode", _modeLabels[OBW_Native.GetMode()])
    AddEmptyOption()

    AddHeaderOption("Body Shape")
    _bodyOption = AddMenuOption("Body shape", _bodyLabels[OBW_Native.GetBodyMode()])
    _scaleOption = AddSliderOption("Morph intensity", OBW_Native.GetMorphScale(), "{2}x")
    _fantasyOption = AddSliderOption("Fantasy NPCs", OBW_Native.GetFantasyRatio() * 100.0, "{0}%")
    _unusualOption = AddSliderOption("Unusual bodies", OBW_Native.GetUnusualRatio() * 100.0, "{0}%")
    _bUnusualOption = AddSliderOption("Unusual breasts", OBW_Native.GetBreastUnusualRatio() * 100.0, "{0}%")
    _athleticOption = AddSliderOption("Athletic women", OBW_Native.GetAthleticRatio() * 100.0, "{0}%")
    _keyOption = AddKeyMapOption("Re-roll key", OBW_Native.GetReRollKey())
    AddEmptyOption()

    AddHeaderOption("Weight Adjustment")
    _biasOption = AddSliderOption("Bias", OBW_Native.GetBias(), "{0}")
    AddEmptyOption()

    int seedDisabled = OPTION_FLAG_DISABLED
    if OBW_Native.GetMode() == 1
        seedDisabled = OPTION_FLAG_NONE
    endif

    AddHeaderOption("Seed (Seeded Mode)")
    int curSeed = OBW_Native.GetSeed()
    _seedOption   = AddTextOption("Current seed", curSeed as string, seedDisabled)
    _reseedOption = AddTextOption("Generate new seed", "[ Click ]", seedDisabled)
EndEvent

Event OnOptionMenuOpen(int option)
    if option == _modeOption
        SetMenuDialogOptions(_modeLabels)
        SetMenuDialogStartIndex(OBW_Native.GetMode())
        SetMenuDialogDefaultIndex(1)
    elseif option == _bodyOption
        SetMenuDialogOptions(_bodyLabels)
        SetMenuDialogStartIndex(OBW_Native.GetBodyMode())
        SetMenuDialogDefaultIndex(0)
    endif
EndEvent

Event OnOptionMenuAccept(int option, int index)
    if option == _modeOption
        OBW_Native.SetMode(index)
        SetMenuOptionValue(_modeOption, _modeLabels[index])
        int seedFlag = OPTION_FLAG_NONE
        if index != 1
            seedFlag = OPTION_FLAG_DISABLED
        endif
        SetOptionFlags(_seedOption,   seedFlag)
        SetOptionFlags(_reseedOption, seedFlag)
    elseif option == _bodyOption
        OBW_Native.SetBodyMode(index)
        SetMenuOptionValue(_bodyOption, _bodyLabels[index])
    endif
EndEvent

Event OnOptionSliderOpen(int option)
    if option == _biasOption
        SetSliderDialogStartValue(OBW_Native.GetBias())
        SetSliderDialogDefaultValue(0.0)
        SetSliderDialogRange(-50.0, 50.0)
        SetSliderDialogInterval(1.0)
    elseif option == _scaleOption
        SetSliderDialogStartValue(OBW_Native.GetMorphScale())
        SetSliderDialogDefaultValue(1.0)
        SetSliderDialogRange(0.0, 2.5)
        SetSliderDialogInterval(0.05)
    elseif option == _fantasyOption
        SetSliderDialogStartValue(OBW_Native.GetFantasyRatio() * 100.0)
        SetSliderDialogDefaultValue(15.0)
        SetSliderDialogRange(0.0, 100.0)
        SetSliderDialogInterval(5.0)
    elseif option == _unusualOption
        SetSliderDialogStartValue(OBW_Native.GetUnusualRatio() * 100.0)
        SetSliderDialogDefaultValue(6.0)
        SetSliderDialogRange(0.0, 50.0)
        SetSliderDialogInterval(1.0)
    elseif option == _bUnusualOption
        SetSliderDialogStartValue(OBW_Native.GetBreastUnusualRatio() * 100.0)
        SetSliderDialogDefaultValue(6.0)
        SetSliderDialogRange(0.0, 50.0)
        SetSliderDialogInterval(1.0)
    elseif option == _athleticOption
        SetSliderDialogStartValue(OBW_Native.GetAthleticRatio() * 100.0)
        SetSliderDialogDefaultValue(15.0)
        SetSliderDialogRange(0.0, 100.0)
        SetSliderDialogInterval(5.0)
    endif
EndEvent

; Re-roll key rebind. SkyUI handles the "press a key" prompt.
Event OnOptionKeyMapChange(int option, int keyCode, string conflictControl, string conflictName)
    if option == _keyOption
        OBW_Native.SetReRollKey(keyCode)
        SetKeyMapOptionValue(_keyOption, keyCode)
        int h = ModEvent.Create("OBW_RebindKey")
        ModEvent.Send(h)
    endif
EndEvent

Event OnOptionSliderAccept(int option, float value)
    if option == _biasOption
        OBW_Native.SetBias(value)
        SetSliderOptionValue(_biasOption, value, "{0}")
    elseif option == _scaleOption
        OBW_Native.SetMorphScale(value)
        SetSliderOptionValue(_scaleOption, value, "{2}x")
    elseif option == _fantasyOption
        OBW_Native.SetFantasyRatio(value / 100.0)
        SetSliderOptionValue(_fantasyOption, value, "{0}%")
    elseif option == _unusualOption
        OBW_Native.SetUnusualRatio(value / 100.0)
        SetSliderOptionValue(_unusualOption, value, "{0}%")
    elseif option == _bUnusualOption
        OBW_Native.SetBreastUnusualRatio(value / 100.0)
        SetSliderOptionValue(_bUnusualOption, value, "{0}%")
    elseif option == _athleticOption
        OBW_Native.SetAthleticRatio(value / 100.0)
        SetSliderOptionValue(_athleticOption, value, "{0}%")
    endif
EndEvent

Event OnOptionSelect(int option)
    if option == _reseedOption
        OBW_Native.RegenerateSeed()
        int newSeed = OBW_Native.GetSeed()
        SetTextOptionValue(_seedOption, newSeed as string)
        Debug.Notification("OBodyNG Weight: new seed generated.")
    endif
EndEvent

Event OnOptionHighlight(int option)
    if option == _modeOption
        SetInfoText("Random: NPCs are consistent within a session, but get new values each time you reload a save.\nSeeded: same NPC always gets the same weight for this entire playthrough.\nNPC Default: disables weight randomization.")
    elseif option == _bodyOption
        SetInfoText("Procedural Morphs: generates body shape directly via SKEE — no BodySlide preset library needed.\nOBody Presets: OBody picks the body shape; this mod only randomizes weight.")
    elseif option == _scaleOption
        SetInfoText("Master multiplier for procedural morph intensity. 1.0 = calibrated to real BodySlide presets. Affects both realistic and fantasy NPCs. Applies to newly generated/re-rolled NPCs.")
    elseif option == _fantasyOption
        SetInfoText("Percentage of NPCs given exaggerated 'fantasy' bodies (1.3-2.2x). The rest are realistic (~1.0x). 15% = mostly realistic with occasional bombshells; 100% = everyone exaggerated.")
    elseif option == _unusualOption
        SetInfoText("Percentage of NPCs with an 'unusual body' — out of the normal distribution. Each is either ultra-petite (tiny) or ultra-thick (huge), with disproportionate, atypical proportions. Rare by default (6%). 0% disables.")
    elseif option == _bUnusualOption
        SetInfoText("Percentage of NPCs with 'unusual breasts' — an extreme outlier: either very saggy or very perky, beyond the normal size-based amount. Independent from Unusual bodies. Rare by default (6%). 0% disables.")
    elseif option == _athleticOption
        SetInfoText("Percentage of women with athletic muscle tone — visible abs, arm and leg definition. The rest are soft/normal. Belly/softness fades the definition. Default 15%. 0% disables. (Males get muscle tone automatically from their build.)")
    elseif option == _keyOption
        SetInfoText("Hotkey to re-roll the body of the NPC under your crosshair (Procedural mode). Default is the [ / { key. If it clashes with OBody, set OBody's selection hotkey to None.")
    elseif option == _biasOption
        SetInfoText("Added to the generated weight before clamping to [0-100]. Positive skews heavier, negative lighter.")
    elseif option == _seedOption
        SetInfoText("Unique seed for this playthrough. Same seed = same weight distribution across sessions.")
    elseif option == _reseedOption
        SetInfoText("Generates a new random seed. Already-processed NPCs keep their current values — only new NPCs use the new seed.")
    endif
EndEvent
