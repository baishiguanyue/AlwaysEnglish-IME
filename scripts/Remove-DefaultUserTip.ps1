<#
.SYNOPSIS
    Removes AlwaysEnglish-IME (formerly KeyboardMethod) TIP entries from the .Default user profile
    (HKEY_USERS\.DEFAULT), i.e. what Setup's "available on logon / lock screen"
    option (InstallLayoutOrTip with ILOT_DEFUSER4) added.

.DESCRIPTION
    Use this when:
      * you reinstalled with the logon/lock-screen option unchecked, but the
        input method still appears on the logon / lock screen or for new users;
      * the input method still shows up on the logon / lock screen after uninstall.

    Without -Apply the script only lists what it would remove (dry run).
    With -Apply (administrator required) it:
      1. asks Windows to remove the entry via input.dll!InstallLayoutOrTip
         (ILOT_UNINSTALL | ILOT_DEFUSER4) for every language it was found under;
      2. rescans and deletes any remaining entries directly from the registry.
    Only entries that reference this TIP's CLSID / profile GUIDs are touched.
    -WhatIf / -Confirm are supported together with -Apply (-Apply -WhatIf does
    not need administrator rights and only prints what would be done).

    Registry locations handled (all under HKEY_USERS\.DEFAULT):
      Control Panel\International\User Profile\<language tag>
          value "<LANGID>:{CLSID}{ProfileGUID}"   (later entries are renumbered)
      Control Panel\International\User Profile
          value InputMethodOverride               (only if it points to this TIP)
      Software\Microsoft\CTF\SortOrder\AssemblyItem\<0xLANGID>\{34745C63-...}\NNNNNNNN
          numbered key with CLSID = this TIP      (later keys are renumbered)
      Software\Microsoft\CTF\Assemblies\<0xLANGID>\{34745C63-...}
          key whose Default = this TIP's CLSID
      Software\Microsoft\CTF\TIP\{CLSID}          (whole key)

.PARAMETER Apply
    Actually remove the entries. Without it the script is a dry run.

.EXAMPLE
    .\Remove-DefaultUserTip.ps1
    Lists what would be removed.

.EXAMPLE
    .\Remove-DefaultUserTip.ps1 -Apply
    Removes the entries (run from an elevated PowerShell).
#>
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$Apply
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# Keep in sync with include/Globals.h.
$TipIds = @(
    @{ Name = 'AlwaysEnglish-IME';               Clsid = '{FC452B85-19F4-47E5-AD40-FD523298A8C7}'; Profile = '{0BF7DD25-41DC-400F-887D-0A80AC37F062}' },
    @{ Name = 'AlwaysEnglish-IME (legacy test)'; Clsid = '{A1B2C3D4-E5F6-7890-ABCD-EF0123456789}'; Profile = '{B1C2D3E4-F5A6-7890-CDEF-0123456789AB}' }
)

$DefaultRoot    = 'Registry::HKEY_USERS\.DEFAULT'
$UserProfileKey = "$DefaultRoot\Control Panel\International\User Profile"
$CtfKey         = "$DefaultRoot\Software\Microsoft\CTF"
$KeyboardCat    = '{34745C63-B2F0-4784-8B67-5E12C8701A31}'   # GUID_TFCAT_TIP_KEYBOARD

$ILOT_UNINSTALL = 0x00000001
$ILOT_DEFUSER4  = 0x00000004

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-OurTip([string]$Clsid) {
    foreach ($t in $TipIds) { if ($Clsid -and $Clsid.Trim() -ieq $t.Clsid) { return $t } }
    return $null
}

function Test-OurClsid([string]$Value) {
    return ($null -ne (Get-OurTip $Value))
}

# "0x00000804" -> "0804"; returns $null for anything that is not a hex number.
function ConvertTo-LangHex([string]$Name) {
    try { return ('{0:X4}' -f ([Convert]::ToInt32($Name, 16) -band 0xFFFF)) } catch { return $null }
}

function Get-ParentPath([string]$Path) {
    return $Path.Substring(0, $Path.LastIndexOf('\'))
}

# "<LANGID>:{CLSID}{Profile}" -> matching TIP entry or $null
function Get-TipFromString([string]$Value) {
    if (-not $Value) { return $null }
    foreach ($t in $TipIds) {
        if ($Value -match ('^[0-9A-Fa-f]{4}:' + [regex]::Escape($t.Clsid) + [regex]::Escape($t.Profile) + '$')) {
            return $t
        }
    }
    return $null
}

# Returns a list of found items. Each item: Kind, Path, Name, LangId, Clsid, Detail.
function Find-DefaultEntries {
    $found = New-Object System.Collections.Generic.List[object]

    # 1. User Profile\<tag> values + InputMethodOverride
    if (Test-Path -LiteralPath $UserProfileKey) {
        $root = Get-Item -LiteralPath $UserProfileKey
        $override = $root.GetValue('InputMethodOverride', $null)
        $t = Get-TipFromString $override
        if ($t) {
            $found.Add([pscustomobject]@{ Kind = 'OverrideValue'; Path = $UserProfileKey; Name = 'InputMethodOverride';
                                          LangId = $override.Substring(0, 4); Clsid = $t.Clsid; Detail = $override })
        }
        foreach ($sub in Get-ChildItem -LiteralPath $UserProfileKey -ErrorAction SilentlyContinue) {
            foreach ($valueName in $sub.GetValueNames()) {
                $t = Get-TipFromString $valueName
                if ($t) {
                    $found.Add([pscustomobject]@{ Kind = 'UserProfileValue'; Path = $sub.PSPath; Name = $valueName;
                                                  LangId = $valueName.Substring(0, 4); Clsid = $t.Clsid; Detail = "language $($sub.PSChildName)" })
                }
            }
        }
    }

    # 2. CTF\SortOrder\AssemblyItem\<lang>\<cat>\NNNNNNNN
    $assemblyItem = "$CtfKey\SortOrder\AssemblyItem"
    if (Test-Path -LiteralPath $assemblyItem) {
        foreach ($langKey in Get-ChildItem -LiteralPath $assemblyItem -ErrorAction SilentlyContinue) {
            foreach ($catKey in Get-ChildItem -LiteralPath $langKey.PSPath -ErrorAction SilentlyContinue) {
                foreach ($item in Get-ChildItem -LiteralPath $catKey.PSPath -ErrorAction SilentlyContinue) {
                    $t = Get-OurTip ($item.GetValue('CLSID', $null))
                    if ($t) {
                        $found.Add([pscustomobject]@{ Kind = 'AssemblyItemKey'; Path = $item.PSPath; Name = $item.PSChildName;
                                                      LangId = (ConvertTo-LangHex $langKey.PSChildName); Clsid = $t.Clsid;
                                                      Detail = "sort order slot $($item.PSChildName)" })
                    }
                }
            }
        }
    }

    # 3. CTF\Assemblies\<lang>\<cat> where Default = our CLSID
    $assemblies = "$CtfKey\Assemblies"
    if (Test-Path -LiteralPath $assemblies) {
        foreach ($langKey in Get-ChildItem -LiteralPath $assemblies -ErrorAction SilentlyContinue) {
            foreach ($catKey in Get-ChildItem -LiteralPath $langKey.PSPath -ErrorAction SilentlyContinue) {
                $t = Get-OurTip ($catKey.GetValue('Default', $null))
                if ($t) {
                    $found.Add([pscustomobject]@{ Kind = 'AssemblyDefaultKey'; Path = $catKey.PSPath; Name = $catKey.PSChildName;
                                                  LangId = (ConvertTo-LangHex $langKey.PSChildName); Clsid = $t.Clsid;
                                                  Detail = 'default input method for this language' })
                }
            }
        }
    }

    # 4. CTF\TIP\{CLSID}
    foreach ($t in $TipIds) {
        $tipKey = "$CtfKey\TIP\$($t.Clsid)"
        if (Test-Path -LiteralPath $tipKey) {
            $langs = @()
            $lp = "$tipKey\LanguageProfile"
            if (Test-Path -LiteralPath $lp) {
                $langs = @(Get-ChildItem -LiteralPath $lp -ErrorAction SilentlyContinue |
                           ForEach-Object { ConvertTo-LangHex $_.PSChildName } | Where-Object { $_ })
            }
            $found.Add([pscustomobject]@{ Kind = 'TipKey'; Path = $tipKey; Name = $t.Clsid;
                                          LangId = ($langs -join ','); Clsid = $t.Clsid; Detail = "$($t.Name) per-user TIP state" })
        }
    }

    return ,$found
}

function Show-Entries($entries, [string]$Title) {
    Write-Host $Title
    if ($entries.Count -eq 0) {
        Write-Host '  (none)'
        return
    }
    foreach ($e in $entries) {
        $p = $e.Path -replace '^Microsoft\.PowerShell\.Core\\Registry::', '' -replace '^Registry::', ''
        Write-Host ("  [{0}] {1}" -f $e.Kind, $p)
        Write-Host ("      {0}  ({1})" -f $e.Name, $e.Detail)
    }
}

function Invoke-InstallLayoutOrTipRemoval($Pairs) {
    if (-not ('KmInputDll' -as [type])) {
        Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class KmInputDll {
    [DllImport("input.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool InstallLayoutOrTip(string psz, uint dwFlags);
}
'@
    }
    foreach ($pair in $Pairs) {
        $lang = $pair.LangId
        $t = Get-OurTip $pair.Clsid
        if ($t) {
            $tip = "0x$($lang):$($t.Clsid)$($t.Profile)"
            if ($PSCmdlet.ShouldProcess(".Default profile", "InstallLayoutOrTip('$tip', ILOT_UNINSTALL|ILOT_DEFUSER4)")) {
                $ok = $false
                try { $ok = [KmInputDll]::InstallLayoutOrTip($tip, [uint32]($ILOT_UNINSTALL -bor $ILOT_DEFUSER4)) }
                catch { Write-Warning "InstallLayoutOrTip threw: $($_.Exception.Message)" }
                Write-Host ("  InstallLayoutOrTip {0} -> {1}" -f $tip, $(if ($ok) { 'OK' } else { 'failed (registry fallback will handle leftovers)' }))
            }
        }
    }
}

# Renumber "<LANGID>:..." DWORD order values in a User Profile\<tag> key after one was removed.
function Compress-UserProfileOrder([string]$KeyPath, [int]$RemovedOrder) {
    $key = Get-Item -LiteralPath $KeyPath
    foreach ($n in $key.GetValueNames()) {
        if ($n -match '^[0-9A-Fa-f]{4}:' -and $key.GetValueKind($n) -eq 'DWord') {
            $v = [int]$key.GetValue($n)
            if ($v -gt $RemovedOrder) {
                Set-ItemProperty -LiteralPath $KeyPath -Name $n -Value ($v - 1) -Type DWord
            }
        }
    }
}

# Renumber NNNNNNNN sibling keys after one was removed so the sort order has no gap.
function Compress-AssemblyItemSlots([string]$CategoryPath) {
    $items = @(Get-ChildItem -LiteralPath $CategoryPath -ErrorAction SilentlyContinue |
               Where-Object { $_.PSChildName -match '^[0-9A-Fa-f]{8}$' } |
               Sort-Object { [Convert]::ToInt64($_.PSChildName, 16) })
    for ($i = 0; $i -lt $items.Count; $i++) {
        $want = '{0:D8}' -f $i
        if ($items[$i].PSChildName -ne $want) {
            Rename-Item -LiteralPath $items[$i].PSPath -NewName $want
        }
    }
}

function Remove-RegistryEntries($entries) {
    # Value removals first, then keys; renumber after each removal.
    foreach ($e in $entries) {
        switch ($e.Kind) {
            'OverrideValue' {
                if ($PSCmdlet.ShouldProcess("$($e.Path)", "Remove value $($e.Name)")) {
                    Remove-ItemProperty -LiteralPath $e.Path -Name $e.Name
                    Write-Host "  removed value $($e.Name)"
                }
            }
            'UserProfileValue' {
                if ($PSCmdlet.ShouldProcess("$($e.Path)", "Remove value $($e.Name)")) {
                    $order = $null
                    $k = Get-Item -LiteralPath $e.Path
                    if ($k.GetValueKind($e.Name) -eq 'DWord') { $order = [int]$k.GetValue($e.Name) }
                    Remove-ItemProperty -LiteralPath $e.Path -Name $e.Name
                    if ($order -ne $null) { Compress-UserProfileOrder $e.Path $order }
                    Write-Host "  removed value $($e.Name)"
                }
            }
        }
    }
    foreach ($e in $entries) {
        switch ($e.Kind) {
            'AssemblyItemKey' {
                if ($PSCmdlet.ShouldProcess("$($e.Path)", 'Remove key')) {
                    $parent = Get-ParentPath $e.Path
                    Remove-Item -LiteralPath $e.Path -Recurse
                    Compress-AssemblyItemSlots $parent
                    Write-Host "  removed key $($e.Path -replace '^.*Registry::', '')"
                }
            }
            { $_ -in 'AssemblyDefaultKey', 'TipKey' } {
                if ($PSCmdlet.ShouldProcess("$($e.Path)", 'Remove key')) {
                    Remove-Item -LiteralPath $e.Path -Recurse
                    Write-Host "  removed key $($e.Path -replace '^.*Registry::', '')"
                }
            }
        }
    }
}

# ---------------------------------------------------------------------------

$entries = Find-DefaultEntries
Show-Entries $entries 'AlwaysEnglish-IME entries found in HKEY_USERS\.DEFAULT:'

if (-not $Apply) {
    Write-Host ''
    if ($entries.Count -gt 0) {
        Write-Host 'Dry run: nothing was changed. Re-run from an elevated PowerShell with -Apply to remove the entries above.'
    } else {
        Write-Host 'Dry run: nothing to remove.'
    }
    return
}

if ($entries.Count -eq 0) {
    Write-Host 'Nothing to remove.'
    return
}

if (-not $WhatIfPreference -and -not (Test-IsAdmin)) {
    [Console]::Error.WriteLine('Removing .Default entries requires administrator rights. Re-run from an elevated PowerShell.')
    exit 1
}

# Step 1: let Windows remove it the supported way.
$pairs = @(foreach ($e in $entries) {
              foreach ($l in ($e.LangId -split ',')) {
                  if ($l -match '^[0-9A-Fa-f]{4}$') { '{0}|{1}' -f $l.ToUpperInvariant(), $e.Clsid.ToUpperInvariant() }
              }
          }) | Sort-Object -Unique | ForEach-Object {
              $p = $_ -split '\|'; [pscustomobject]@{ LangId = $p[0]; Clsid = $p[1] } }
$pairs = @($pairs)
if ($pairs.Count -gt 0) {
    Write-Host ''
    Write-Host 'Step 1: InstallLayoutOrTip (ILOT_UNINSTALL | ILOT_DEFUSER4)'
    Invoke-InstallLayoutOrTipRemoval $pairs
}

# Step 2: delete whatever is still there.
$left = Find-DefaultEntries
Write-Host ''
Write-Host 'Step 2: registry cleanup of remaining entries'
if ($left.Count -eq 0) {
    Write-Host '  (none left)'
} else {
    Remove-RegistryEntries $left
}

if (-not $WhatIfPreference) {
    $final = Find-DefaultEntries
    Write-Host ''
    Show-Entries $final 'Remaining AlwaysEnglish-IME entries in HKEY_USERS\.DEFAULT:'
    if ($final.Count -gt 0) { exit 2 }
}
