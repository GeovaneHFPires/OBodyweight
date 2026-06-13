Scriptname OBodyNative

Function GenActor(Actor a_actor) Global Native
Function ApplyPresetByFile(Actor a_actor, string a_pathToFile) Global Native
Function ApplyPresetByName(Actor a_actor, String a_name) Global Native
Function RegisterForOBodyEvent(Quest a_quest) Global Native
Function RegisterForOBodyNakedEvent(Quest a_quest) Global Native
Function RegisterForOBodyRemovingClothesEvent(Quest a_quest) Global Native
Function RemoveClothesOverlay_(Actor a_actor) Global Native
Function AddClothesOverlay(Actor a_actor) Global Native
String[] Function GetAllPossiblePresets(actor a_actor) Global Native
Int Function GetFemaleDatabaseSize() Global Native
Int Function GetMaleDatabaseSize() Global Native
Function SetORefit(Bool a_enabled) Global Native
Function SetNippleSlidersORefitEnabled(Bool a_enabled) Global Native
Function SetNippleRand(Bool a_enabled) Global Native
Function SetGenitalRand(Bool a_enabled) Global Native
Function SetPerformanceMode(Bool a_enabled) Global Native
Function SetRespectfulMorphApplication(Bool a_enabled) Global Native
Function SetLegacyStorageUtilUsageEnabled(Bool a_enabled) Global Native
Function SetDistributionKey(String a_distributionKey) Global Native
Function ResetActorOBodyMorphs(Actor a_actor) Global Native
Function ReapplyActorOBodyMorphs(Actor a_actor) Global Native
String Function GetPresetAssignedToActor(Actor a_actor) Global Native
Bool Function AssignPresetToActor(Actor a_actor, String a_presetName, Bool a_forceImmediateApplicationOfMorphs = True, Bool a_doNotApplyMorphs = False) Global Native
