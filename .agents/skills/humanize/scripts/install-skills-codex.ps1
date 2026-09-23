# Project-local Windows installation using Git Bash for Humanize's runtime.
# No user-level skills, hooks, configuration, marketplace or PATH changes.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ProjectPath,
    [string]$BashPath = 'C:/Program Files/Git/bin/bash.exe',
    [Parameter(Mandatory)][string]$PythonPath,
    [Parameter(Mandatory)][string]$JqPath,
    [string]$CodexPath = (Get-Command codex -ErrorAction Stop).Source
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$utf8 = New-Object System.Text.UTF8Encoding($false)
function Write-Utf8([string]$Path, [string]$Content) {
    [IO.File]::WriteAllText($Path, $Content.Replace("`r`n", "`n"), $utf8)
}
function Shell-Quote([string]$Value) {
    # POSIX single-quote escaping; never interpolate user arguments into shell code.
    return "'" + $Value.Replace("'", "'\''") + "'"
}
foreach ($dependency in @($BashPath, $PythonPath, $JqPath, $CodexPath)) {
    if (-not (Test-Path -LiteralPath $dependency -PathType Leaf)) { throw "Missing dependency: $dependency" }
}
$features = & $CodexPath features list 2>$null
if ($LASTEXITCODE -ne 0 -or -not ($features -match '^hooks\s+.*true$')) {
    throw 'This Windows installer requires a current Codex CLI with hooks enabled.'
}
& $PythonPath --version
if ($LASTEXITCODE -ne 0) { throw 'Python is not runnable.' }
& $JqPath --version
if ($LASTEXITCODE -ne 0) { throw 'jq is not runnable.' }

$ProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
if (-not (Test-Path -LiteralPath $ProjectPath -PathType Container)) { throw 'ProjectPath must be an existing directory.' }
$CodexConfigDir = Join-Path $ProjectPath '.codex'
$HumanizeConfigDir = Join-Path $ProjectPath '.humanize'
$skillsDir = Join-Path $ProjectPath '.agents/skills'
$runtime = (Join-Path $skillsDir 'humanize').Replace('\', '/')
$names = @('humanize', 'humanize-gen-plan', 'humanize-refine-plan', 'humanize-rlcr', 'humanize-gen-idea', 'ask-codex')
# Never overwrite an existing skill installation or unrelated hooks.
foreach ($name in $names) {
    if (Test-Path -LiteralPath (Join-Path $skillsDir $name)) { throw "Skill already exists: $name. Back it up before reinstalling." }
}
$hooksFile = Join-Path $CodexConfigDir 'hooks.json'
$hooks = if (Test-Path -LiteralPath $hooksFile) { Get-Content -LiteralPath $hooksFile -Raw | ConvertFrom-Json } else { [pscustomobject]@{ hooks = [pscustomobject]@{} } }
if (-not $hooks.hooks -or $hooks.hooks -isnot [pscustomobject]) { throw 'Invalid existing hooks.json hooks object.' }
$configFile = Join-Path $HumanizeConfigDir 'config.json'
$config = if (Test-Path -LiteralPath $configFile) { Get-Content -LiteralPath $configFile -Raw | ConvertFrom-Json } else { [pscustomobject]@{} }
if ($config -isnot [pscustomobject]) { throw 'Invalid existing Humanize configuration.' }

New-Item -ItemType Directory -Path $skillsDir, $CodexConfigDir -Force | Out-Null
foreach ($name in $names) {
    $destination = Join-Path $skillsDir $name
    if ($name -eq 'humanize-gen-idea') {
        New-Item -ItemType Directory -Path $destination | Out-Null
        $idea = Get-Content (Join-Path $repo 'commands/gen-idea.md') -Raw
        $idea = [regex]::Replace($idea, '\A---\r?\n.*?\r?\n---', "---`nname: humanize-gen-idea`ndescription: Generate a repository-grounded idea draft through exploration of alternative designs, without implementing it.`n---", 'Singleline')
        Write-Utf8 (Join-Path $destination 'SKILL.md') $idea
    } else {
        Copy-Item -LiteralPath (Join-Path $repo "skills/$name") -Destination $destination -Recurse
    }
}
foreach ($component in @('scripts', 'hooks', 'prompt-template', 'templates', 'config', 'agents')) {
    $componentTarget = Join-Path $runtime $component
    New-Item -ItemType Directory -Path $componentTarget | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $repo $component) | Where-Object { $_.Name -ne 'ask-gemini.sh' } | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $componentTarget -Recurse
    }
}
$bin = Join-Path $runtime 'windows-bin'
New-Item -ItemType Directory -Path $bin | Out-Null
Copy-Item -LiteralPath $JqPath -Destination (Join-Path $bin 'jq.exe')
Write-Utf8 (Join-Path $bin 'jq') '#!/usr/bin/env bash
exec "$(dirname "${BASH_SOURCE[0]}")/jq.exe" --binary "$@"
'
Write-Utf8 (Join-Path $bin 'bitlesson-selector') '#!/usr/bin/env bash
exec bash "$(dirname "${BASH_SOURCE[0]}")/../scripts/bitlesson-select.sh" "$@"
'
$pythonShim = "#!/usr/bin/env bash`nexec " + (Shell-Quote $PythonPath.Replace('\', '/')) + ' "$@"' + "`n"
Write-Utf8 (Join-Path $bin 'python3') $pythonShim
$codexShim = "#!/usr/bin/env bash`nexec " + (Shell-Quote $CodexPath.Replace('\', '/')) + ' "$@"' + "`n"
Write-Utf8 (Join-Path $bin 'codex') $codexShim
$runner = @'
#!/usr/bin/env bash
set -euo pipefail
RUNTIME_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="$RUNTIME_ROOT/windows-bin:/usr/bin:/bin:$PATH"
export HUMANIZE_PROVIDER_MODE=codex-only
export PYTHONUTF8=1
# Keep ancillary prompts and diagnostics scoped to this installation's project.
PROJECT_ROOT="$(cd "$RUNTIME_ROOT/../../.." && pwd)"
export HUMANIZE_CONFIG="$PROJECT_ROOT/.humanize/config.json"
export XDG_CACHE_HOME="$PROJECT_ROOT/.humanize/cache"
if [[ $# -eq 0 ]]; then
    echo 'Usage: run-humanize.sh scripts/<script>.sh [arguments...]' >&2
    exit 1
fi
if [[ "$1" == monitor ]]; then
    shift
    source "$RUNTIME_ROOT/scripts/humanize.sh"
    humanize monitor "$@"
    exit $?
fi
script="$1"
shift
if [[ "$script" != scripts/*.sh && "$script" != hooks/*.sh ]]; then
    echo 'Expected a Humanize scripts/*.sh or hooks/*.sh entrypoint.' >&2
    exit 1
fi
exec bash "$RUNTIME_ROOT/$script" "$@"
'@
Write-Utf8 (Join-Path $runtime 'run-humanize.sh') $runner

# Normalize copied scripts for Bash.
foreach ($file in Get-ChildItem -LiteralPath $runtime -Recurse -File | Where-Object { $_.Extension -in @('.sh', '.py') }) {
    $content = [IO.File]::ReadAllText($file.FullName)
    Write-Utf8 $file.FullName $content
}
$windowsNote = @'
## Codex on Windows

Run this skill from the user's target project directory. Execute runtime scripts
with Git Bash, using this PowerShell form (pass each argument separately):

```powershell
& '__BASH__' --noprofile --norc '__RUNTIME__/run-humanize.sh' 'scripts/<script>.sh' 'argument'
```

Use this launcher for every runtime script shown below; it supplies jq, Python,
GNU utilities and Codex without changing the global PATH. For monitoring replace
the script argument with `monitor` and pass `rlcr`, `skill`, or `codex`.
Replace `$ARGUMENTS` with parsed user arguments, preserving quoted text as a single
argument. Do not paste user text into shell source. Runtime file paths below are
absolute and may also be read directly from PowerShell.

Use Codex's available file/search/edit and user-question tools in place of Claude
tool names. `Task`/`Explore` means read-only Codex subagents, within the available
concurrency limit; queue additional directions in batches. Read AGENTS.md for
repository instructions. Claude Agent Teams is not a Codex feature.
Invoke flows using `$humanize-gen-idea`, `$humanize-gen-plan`,
`$humanize-refine-plan`, or `$humanize-rlcr`, not `/flow:` or Claude slash commands.
Codex implements and a separate Codex CLI process independently reviews it.
Use only Codex providers. Ask Codex requires a logged-in Codex CLI.
Never substitute fabricated reviews.
Before starting RLCR, the user must review/trust its Stop hook in Codex's `/hooks`
browser and use a session that has loaded it. Hook trust is not installed here.
The setup script uses CODEX_THREAD_ID to bind the loop to this Codex session.
If unavailable, pass --session-id with this session's actual ID; never invent one.
After code review passes, preserve the reviewed code during finalization.
Use this Codex session for the optional local methodology analysis.
For BitLesson selection, run scripts/bitlesson-select.sh through this launcher.

'@
$windowsNote = $windowsNote.Replace('__BASH__', $BashPath.Replace('\', '/')).Replace('__RUNTIME__', $runtime)
foreach ($name in $names) {
    $skillFile = Join-Path $skillsDir "$name/SKILL.md"
    $content = Get-Content -LiteralPath $skillFile -Raw
    $content = $content.Replace('{{HUMANIZE_RUNTIME_ROOT}}', $runtime).Replace('${CLAUDE_PLUGIN_ROOT}', $runtime).Replace('codex_hooks', 'hooks')
    $content = $content.Replace('Claude implements', 'Codex implements').Replace("Claude's work", "the implementing Codex agent's work")
    $content = [regex]::Replace($content, '(?m)^(user-invocable|disable-model-invocation|hide-from-slash-command-tool|type|allowed-tools|argument-hint):[^\r\n]*\r?\n', '')
    $frontmatter = [regex]::Match($content, '\A---\r?\n.*?\r?\n---\r?\n', 'Singleline')
    if (-not $frontmatter.Success) { throw "Invalid skill frontmatter: $name" }
    $content = $frontmatter.Value + "`n" + $windowsNote + $content.Substring($frontmatter.Length)
    Write-Utf8 $skillFile $content
}
$hook = [pscustomobject]@{
    type = 'command'
    command = "bash `"$runtime/run-humanize.sh`" hooks/loop-codex-stop-hook.sh"
    commandWindows = "`"$($BashPath.Replace('\', '/'))`" --noprofile --norc `"$runtime/run-humanize.sh`" hooks/loop-codex-stop-hook.sh"
    timeout = 7200
    statusMessage = 'Humanize RLCR independent review'
}
$stopGroups = @()
if ($hooks.hooks.PSObject.Properties['Stop']) { $stopGroups = @($hooks.hooks.Stop) }
$stopGroups += [pscustomobject]@{ hooks = @($hook) }
$hooks.hooks | Add-Member -NotePropertyName Stop -NotePropertyValue $stopGroups -Force
if (Test-Path -LiteralPath $hooksFile) { Copy-Item -LiteralPath $hooksFile -Destination "$hooksFile.humanize-$(Get-Date -Format yyyyMMddHHmmss).bak" }
Write-Utf8 $hooksFile ($hooks | ConvertTo-Json -Depth 50)
if (-not $config.PSObject.Properties['bitlesson_model'] -or $config.bitlesson_model -notmatch '^(gpt-|o[0-9])') {
    $model = if ($config.PSObject.Properties['codex_model'] -and $config.codex_model) { $config.codex_model } else { 'gpt-5.5' }
    $config | Add-Member -NotePropertyName bitlesson_model -NotePropertyValue $model -Force
}
$config | Add-Member -NotePropertyName provider_mode -NotePropertyValue 'codex-only' -Force
$config | Add-Member -NotePropertyName agent_teams -NotePropertyValue $false -Force
New-Item -ItemType Directory -Path $HumanizeConfigDir -Force | Out-Null
if (Test-Path -LiteralPath $configFile) { Copy-Item -LiteralPath $configFile -Destination "$configFile.humanize-$(Get-Date -Format yyyyMMddHHmmss).bak" }
Write-Utf8 $configFile ($config | ConvertTo-Json -Depth 50)
Write-Output "Installed skills: $($names -join ', ')"
Write-Output "Runtime: $runtime"
Write-Output "Hook definition: $hooksFile"
Write-Output 'Start a fresh Codex session and review/trust the Humanize Stop hook with /hooks.'
