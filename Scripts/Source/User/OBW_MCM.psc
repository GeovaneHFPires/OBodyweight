Scriptname OBW_MCM extends SKI_ConfigBase

int _modeOption     = -1
int _bodyOption     = -1
int _femaleOption   = -1
int _maleOption     = -1
int _maleBuildOption = -1
int _scaleOption    = -1
int _orientOption   = -1
int _fantasyOption  = -1
int _unusualOption  = -1
int _bUnusualOption = -1
int _athleticOption = -1
int _keyOption      = -1
int _reprocessOption = -1
int _biasOption     = -1
int _seedOption     = -1
int _reseedOption   = -1
int _debugOption    = -1

string[] _modeLabels
string[] _bodyLabels

string[] _exclPlugins   ; Exclusions page: plugin name per toggle (parallel to _exclOptions)
int[]    _exclOptions   ; Exclusions page: option ID per plugin

Event OnConfigInit()
    ModName = "OBodyNG Weight"

    Pages = new string[2]
    Pages[0] = "Settings"
    Pages[1] = "Exclusions"

    _modeLabels = new string[3]
    _modeLabels[0] = "Random"
    _modeLabels[1] = "Seeded (Deterministic)"
    _modeLabels[2] = "NPC Default (disabled)"

    _bodyLabels = new string[3]
    _bodyLabels[0] = "Procedural Morphs"
    _bodyLabels[1] = "OBody Presets"
    _bodyLabels[2] = "Procedural Oriented"
EndEvent

Event OnPlayerLoadGame()
EndEvent

; SkyUI calls this on EVERY game load (including existing saves). OnPlayerLoadGame does NOT fire on
; Quest scripts, so this is the reliable place to (re)start OBW_Quest's persistent manual-assign poll.
Event OnGameReload()
    parent.OnGameReload()
    OBW_Quest q = Game.GetFormFromFile(0x000800, "OBodyNGWeight.esp") as OBW_Quest
    if q
        q.StartPoll()    ; arms the poll first (no Log dependency inside)
    endif
    OBW_Native.Log("OnGameReload fired, q=" + q)   ; diagnostic LAST (a Log failure can't block the poll)
EndEvent

Event OnPageReset(string page)
    if page == "Exclusions"
        BuildExclusionsPage()
        return
    endif

    SetCursorFillMode(TOP_TO_BOTTOM)

    int gm = OBW_Native.GetBodyMode()    ; 0 Procedural, 1 OBody Presets, 2 Procedural Oriented
    int wm = OBW_Native.GetMode()        ; 0 Random, 1 Seeded, 2 NPC Default
    bool fem = OBW_Native.GetFemaleBodies()
    bool male = OBW_Native.GetMaleBodies()

    ; Grey out what the current mode ignores, so only the relevant options are active.
    ; Procedural distribution dials: procedural modes (0/2) only — OBody Presets (1) applies presets faithfully.
    int procFlag = OPTION_FLAG_DISABLED
    if gm != 1
        procFlag = OPTION_FLAG_NONE
    endif
    ; Female-only procedural dials also need Female bodies on.
    int femProcFlag = OPTION_FLAG_DISABLED
    if gm != 1 && fem
        femProcFlag = OPTION_FLAG_NONE
    endif
    ; Male build: procedural male tuning -> procedural mode + Male bodies on.
    int maleBuildFlag = OPTION_FLAG_DISABLED
    if gm != 1 && male
        maleBuildFlag = OPTION_FLAG_NONE
    endif
    ; Preset orientation: only Procedural Oriented (2) blends toward the preset.
    int orientFlag = OPTION_FLAG_DISABLED
    if gm == 2
        orientFlag = OPTION_FLAG_NONE
    endif
    ; Seed: only meaningful in Seeded weight mode (1).
    int seedFlag = OPTION_FLAG_DISABLED
    if wm == 1
        seedFlag = OPTION_FLAG_NONE
    endif

    ; ── LEFT COLUMN: global settings first ──────────────────────────────
    ; Generation: the two mode selectors + the global size dial (apply to everything).
    AddHeaderOption("Generation")
    _bodyOption = AddMenuOption("Generation mode", _bodyLabels[gm])
    _modeOption = AddMenuOption("Weight mode", _modeLabels[wm])
    _biasOption = AddSliderOption("Bias (size)", OBW_Native.GetBias(), "{0}")
    AddEmptyOption()

    ; Sexes: which bodies OBW manages + male-only tuning.
    AddHeaderOption("Sexes")
    _femaleOption = AddToggleOption("Female bodies", fem)
    _maleOption = AddToggleOption("Male bodies", male)
    _maleBuildOption = AddSliderOption("Male build", OBW_Native.GetMaleBuild(), "{2}x", maleBuildFlag)
    AddEmptyOption()

    ; Apply & hotkey.
    AddHeaderOption("Apply")
    _reprocessOption = AddTextOption("Reprocess all loaded NPCs", "[ Click ]")
    _keyOption = AddKeyMapOption("Re-roll key", OBW_Native.GetReRollKey())

    ; ── RIGHT COLUMN: per-mode / distribution settings ──────────────────
    ; Procedural variety: the distribution dials for the procedural modes (0 / 2).
    AddHeaderOption("Procedural Variety")
    _scaleOption = AddSliderOption("Morph intensity", OBW_Native.GetMorphScale(), "{2}x", procFlag)
    _fantasyOption = AddSliderOption("Fantasy NPCs", OBW_Native.GetFantasyRatio() * 100.0, "{0}%", procFlag)
    _unusualOption = AddSliderOption("Unusual bodies", OBW_Native.GetUnusualRatio() * 100.0, "{0}%", procFlag)
    _bUnusualOption = AddSliderOption("Unusual breasts", OBW_Native.GetBreastUnusualRatio() * 100.0, "{0}%", femProcFlag)
    _athleticOption = AddSliderOption("Athletic women", OBW_Native.GetAthleticRatio() * 100.0, "{0}%", femProcFlag)
    AddEmptyOption()

    ; OBody preset mode (Procedural Oriented blend strength).
    AddHeaderOption("OBody Preset Mode")
    _orientOption = AddSliderOption("Preset orientation", OBW_Native.GetPresetOrient() * 100.0, "{0}%", orientFlag)
    AddEmptyOption()

    ; Seed (only meaningful in Seeded weight mode).
    AddHeaderOption("Seed (Seeded Mode)")
    int curSeed = OBW_Native.GetSeed()
    _seedOption   = AddTextOption("Current seed", curSeed as string, seedFlag)
    _reseedOption = AddTextOption("Generate new seed", "[ Click ]", seedFlag)
    AddEmptyOption()

    AddHeaderOption("Debug")
    _debugOption = AddToggleOption("Debug logging", OBW_Native.GetDebugLog())
EndEvent

; Exclusions page: one checkbox per plugin that adds NPCs. Checked = OBW leaves that mod's NPCs alone.
Function BuildExclusionsPage()
    SetCursorFillMode(TOP_TO_BOTTOM)
    AddHeaderOption("Exclude NPCs by plugin")
    _exclPlugins = OBW_Native.GetNpcPlugins()
    if _exclPlugins.Length == 0
        AddTextOption("(no NPC-adding plugins found)", "", OPTION_FLAG_DISABLED)
        return
    endif
    _exclOptions = new int[128]
    int i = 0
    while i < _exclPlugins.Length
        _exclOptions[i] = AddToggleOption(_exclPlugins[i], OBW_Native.IsPluginExcluded(_exclPlugins[i]))
        i += 1
    endwhile
    if _exclPlugins.Length >= 128
        AddEmptyOption()
        AddHeaderOption("List capped at 128 - use OBodyNGWeight_Exclusions.txt for more")
    endif
EndFunction

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
        ForcePageReset()   ; weight mode gates the Seed group -> redraw to recompute flags
    elseif option == _bodyOption
        OBW_Native.SetBodyMode(index)
        ForcePageReset()   ; generation mode gates the procedural / preset / orientation groups
    endif
EndEvent

Event OnOptionSliderOpen(int option)
    if option == _biasOption
        SetSliderDialogStartValue(OBW_Native.GetBias())
        SetSliderDialogDefaultValue(0.0)
        SetSliderDialogRange(-50.0, 50.0)
        SetSliderDialogInterval(1.0)
    elseif option == _maleBuildOption
        SetSliderDialogStartValue(OBW_Native.GetMaleBuild())
        SetSliderDialogDefaultValue(1.0)
        SetSliderDialogRange(0.0, 2.0)
        SetSliderDialogInterval(0.05)
    elseif option == _scaleOption
        SetSliderDialogStartValue(OBW_Native.GetMorphScale())
        SetSliderDialogDefaultValue(1.0)
        SetSliderDialogRange(0.0, 2.5)
        SetSliderDialogInterval(0.05)
    elseif option == _orientOption
        SetSliderDialogStartValue(OBW_Native.GetPresetOrient() * 100.0)
        SetSliderDialogDefaultValue(50.0)
        SetSliderDialogRange(0.0, 100.0)
        SetSliderDialogInterval(5.0)
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
        ; Robust path: re-register the key DIRECTLY on the handler quest. The old
        ; ModEvent route could silently fail (OBW_Quest.OnPlayerLoadGame doesn't fire on
        ; a Quest script, so its mod-event registration may be missing after a reload,
        ; leaving the rebind with no listener). 0x800 = OBW_QuestRecord in the plugin.
        OBW_Quest q = Game.GetFormFromFile(0x000800, "OBodyNGWeight.esp") as OBW_Quest
        if q
            q.BindReRollKey()
        endif
        ; Keep the ModEvent as a fallback for any setup where the form lookup fails.
        int h = ModEvent.Create("OBW_RebindKey")
        ModEvent.Send(h)
    endif
EndEvent

Event OnOptionSliderAccept(int option, float value)
    if option == _biasOption
        OBW_Native.SetBias(value)
        SetSliderOptionValue(_biasOption, value, "{0}")
    elseif option == _maleBuildOption
        OBW_Native.SetMaleBuild(value)
        SetSliderOptionValue(_maleBuildOption, value, "{2}x")
    elseif option == _scaleOption
        OBW_Native.SetMorphScale(value)
        SetSliderOptionValue(_scaleOption, value, "{2}x")
    elseif option == _orientOption
        OBW_Native.SetPresetOrient(value / 100.0)
        SetSliderOptionValue(_orientOption, value, "{0}%")
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
    elseif option == _femaleOption
        bool newFem = !OBW_Native.GetFemaleBodies()
        OBW_Native.SetFemaleBodies(newFem)
        SetToggleOptionValue(_femaleOption, newFem)
        ; Female-only procedural dials follow this toggle (and the mode).
        int ff = OPTION_FLAG_DISABLED
        if newFem && OBW_Native.GetBodyMode() != 1
            ff = OPTION_FLAG_NONE
        endif
        SetOptionFlags(_bUnusualOption, ff)
        SetOptionFlags(_athleticOption, ff)
    elseif option == _maleOption
        bool newVal = !OBW_Native.GetMaleBodies()
        OBW_Native.SetMaleBodies(newVal)
        SetToggleOptionValue(_maleOption, newVal)
        ; Male build follows this toggle (and the mode).
        int mf = OPTION_FLAG_DISABLED
        if newVal && OBW_Native.GetBodyMode() != 1
            mf = OPTION_FLAG_NONE
        endif
        SetOptionFlags(_maleBuildOption, mf)
    elseif option == _debugOption
        bool newDbg = !OBW_Native.GetDebugLog()
        OBW_Native.SetDebugLog(newDbg)
        SetToggleOptionValue(_debugOption, newDbg)
    elseif option == _reprocessOption
        SendModEvent("OBW_Reprocess")   ; OBW_Quest re-queues all loaded NPCs + arms the drain
    elseif _exclPlugins
        ; Exclusions page: a per-plugin checkbox.
        int i = 0
        while i < _exclPlugins.Length
            if option == _exclOptions[i]
                bool nx = !OBW_Native.IsPluginExcluded(_exclPlugins[i])
                OBW_Native.SetPluginExcluded(_exclPlugins[i], nx)
                SetToggleOptionValue(_exclOptions[i], nx)
                return
            endif
            i += 1
        endwhile
    endif
EndEvent

Event OnOptionHighlight(int option)
    if option == _modeOption
        SetInfoText("Drives the mock weight (body size in procedural modes, preset interpolation in OBody mode).\nRandom: consistent within a session, new values each reload.\nSeeded: same NPC always gets the same weight this playthrough.\nNPC Default: uses the NPC's real weight. In OBody Presets mode this means plain OBody (no OBW weight re-application).")
    elseif option == _bodyOption
        SetInfoText("Procedural Morphs: fully generated bodies via SKEE; OBody presets ignored.\nOBody Presets: OBody picks the preset, then OBW re-applies it at each NPC's mock weight so NPCs sharing a preset VARY (and presets with no weight range get a synthesized lean<->full axis). Set 'Weight mode' to NPC Default for plain OBody instead.\nProcedural Oriented: generated bodies BLENDED toward each NPC's OBody preset (set the blend with 'Preset orientation').")
    elseif option == _femaleOption
        SetInfoText("Master switch for female NPCs. ON: women get OBW's procedural bodies. OFF: OBW ignores women entirely (no morphs) — OBody or vanilla handle them. Lets you run OBW for one sex only, or disable both. Applies to newly generated/re-rolled NPCs.")
    elseif option == _maleOption
        SetInfoText("Master switch for male NPCs. ON: men get procedural HIMBO bodies + weight, like women. OFF: OBW ignores men entirely (no weight, no morphs) — OBody or vanilla handle them. Lets you run OBW for one sex only, or disable both. Applies to newly generated/re-rolled NPCs.")
    elseif option == _maleBuildOption
        SetInfoText("Scales the overall male build (muscle + mass). 1.0 = default. Lower for slimmer men, higher for beefier. All body parts scale together, so proportions stay intact. Applies to newly generated/re-rolled NPCs.")
    elseif option == _scaleOption
        SetInfoText("Master multiplier for procedural morph intensity. 1.0 = calibrated to real BodySlide presets. Affects both realistic and fantasy NPCs. Applies to newly generated/re-rolled NPCs.")
    elseif option == _orientOption
        SetInfoText("Only in body mode 'Procedural Oriented': how strongly each NPC's OBody preset pulls the procedural body toward it. 0% = pure procedural (preset ignored), 100% = the preset itself, in between = a blend (preset character + procedural variation & weight). No effect in the other two modes.")
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
        SetInfoText("Global heavier(+)/leaner(-) dial. Shifts the mock weight before clamping to [0-100], so it nudges body size everywhere: procedural frame size (modes Procedural / Oriented) and the OBody-preset interpolation (OBody Presets mode).")
    elseif option == _seedOption
        SetInfoText("Unique seed for this playthrough. Same seed = same weight distribution across sessions.")
    elseif option == _reseedOption
        SetInfoText("Generates a new random seed. Already-processed NPCs keep their current values — only new NPCs use the new seed.")
    elseif option == _debugOption
        SetInfoText("Writes detailed per-NPC diagnostics to OBodyNGWeight.log (preset applied, load/poll events). OFF by default. Turn it ON only when troubleshooting — it bloats the log in crowded areas. Warnings and errors are always logged.")
    elseif option == _reprocessOption
        SetInfoText("Re-applies the current generation + physics to every NPC loaded around you, without waiting for cell reloads. Handy after changing settings.")
    elseif _exclPlugins
        SetInfoText("Check to EXCLUDE NPCs from this plugin: OBW leaves them to OBody / vanilla / the source mod (no procedural body, no preset re-application). Saved globally across saves. Applies to newly generated NPCs (use Reprocess, or it takes hold as NPCs load).")
    endif
EndEvent
